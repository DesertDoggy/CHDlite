// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_archiver.cpp - Archive (create CHD) implementation

#include "chd_archiver.hpp"
#include "chd_reader.hpp"
#include "detect_game_platform.hpp"

#include "chd.h"
#include "cdrom.h"
#include "chdcodec.h"
#include "corefile.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace chdlite {

// ======================> CUE fix helpers

// Fix CUE files where the FILE line references a bin with "(Track 1)" but the
// actual file on disk doesn't have that suffix (or vice versa).
static bool fix_cue_track1(const std::string& cue_path)
{
    namespace fs = std::filesystem;
    std::ifstream in(cue_path);
    if (!in.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;
    bool changed = false;

    while (std::getline(in, line)) {
        // Look for FILE "..." BINARY lines
        auto fpos = line.find("FILE \"");
        if (fpos != std::string::npos) {
            auto qstart = line.find('"', fpos);
            auto qend = line.find('"', qstart + 1);
            if (qstart != std::string::npos && qend != std::string::npos) {
                std::string ref_name = line.substr(qstart + 1, qend - qstart - 1);
                fs::path cue_dir = fs::path(cue_path).parent_path();
                fs::path ref_path = cue_dir / ref_name;

                if (!fs::exists(ref_path)) {
                    // Try adding/removing " (Track 1)" before extension
                    fs::path stem = fs::path(ref_name).stem();
                    fs::path ext  = fs::path(ref_name).extension();
                    std::string s = stem.string();
                    std::string alt;

                    const std::string tag = " (Track 1)";
                    if (s.size() > tag.size() && s.substr(s.size() - tag.size()) == tag) {
                        // Has "(Track 1)" — try without
                        alt = s.substr(0, s.size() - tag.size()) + ext.string();
                    } else {
                        // Doesn't have it — try with
                        alt = s + tag + ext.string();
                    }

                    if (!alt.empty() && fs::exists(cue_dir / alt)) {
                        line = line.substr(0, qstart + 1) + alt + line.substr(qend);
                        changed = true;
                    }
                }
            }
        }
        lines.push_back(line);
    }
    in.close();

    if (changed) {
        std::ofstream out(cue_path, std::ios::trunc);
        if (!out.is_open()) return false;
        for (size_t i = 0; i < lines.size(); i++) {
            out << lines[i];
            if (i + 1 < lines.size()) out << '\n';
        }
        return true;
    }
    return false;
}

// ======================> Helpers

static std::string path_ext_lower(const std::string& path)
{
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

static chd_codec_type chdlite_codec_to_mame(Codec c)
{
    switch (c) {
    case Codec::None:     return CHD_CODEC_NONE;
    case Codec::Zlib:     return CHD_CODEC_ZLIB;
    case Codec::Zstd:     return CHD_CODEC_ZSTD;
    case Codec::LZMA:     return CHD_CODEC_LZMA;
    case Codec::Huffman:  return CHD_CODEC_HUFFMAN;
    case Codec::FLAC:     return CHD_CODEC_FLAC;
    case Codec::CD_Zlib:  return CHD_CODEC_CD_ZLIB;
    case Codec::CD_Zstd:  return CHD_CODEC_CD_ZSTD;
    case Codec::CD_LZMA:  return CHD_CODEC_CD_LZMA;
    case Codec::CD_FLAC:  return CHD_CODEC_CD_FLAC;
    case Codec::AVHUFF:   return CHD_CODEC_AVHUFF;
    default:              return CHD_CODEC_NONE;
    }
}

// Default compression codec sets (chdman legacy defaults — used when no smart defaults apply)
static const chd_codec_type s_default_raw_compression[4] =
    { CHD_CODEC_LZMA, CHD_CODEC_ZLIB, CHD_CODEC_HUFFMAN, CHD_CODEC_FLAC };

static const chd_codec_type s_default_cd_compression[4] =
    { CHD_CODEC_CD_LZMA, CHD_CODEC_CD_ZLIB, CHD_CODEC_CD_FLAC, CHD_CODEC_NONE };

static const chd_codec_type s_default_dvd_compression[4] =
    { CHD_CODEC_LZMA, CHD_CODEC_ZLIB, CHD_CODEC_HUFFMAN, CHD_CODEC_FLAC };

// Smart system-aware compression defaults
// PS2 DVD: zlib (best compat/speed balance for PS2 content)
static const chd_codec_type s_ps2_dvd_compression[4] =
    { CHD_CODEC_ZLIB, CHD_CODEC_NONE, CHD_CODEC_NONE, CHD_CODEC_NONE };

// PS2 CD: cdzl, cdfl (CD ZLIB + FLAC for audio — Android emulator compatible)
static const chd_codec_type s_ps2_cd_compression[4] =
    { CHD_CODEC_CD_ZLIB, CHD_CODEC_CD_FLAC, CHD_CODEC_NONE, CHD_CODEC_NONE };

// Other DVD: zstd
static const chd_codec_type s_smart_dvd_compression[4] =
    { CHD_CODEC_ZSTD, CHD_CODEC_NONE, CHD_CODEC_NONE, CHD_CODEC_NONE };

// Other CD / GD-ROM: cdzs, cdfl (ZSTD wins data sectors; FLAC wins audio sectors)
static const chd_codec_type s_smart_cd_compression[4] =
    { CHD_CODEC_CD_ZSTD, CHD_CODEC_CD_FLAC, CHD_CODEC_NONE, CHD_CODEC_NONE };

// --best: maximum compression ratio (slower compress + decompress)
// DVD: zstd, lzma, zlib, huff — all 4 slots filled for best ratio
static const chd_codec_type s_best_dvd_compression[4] =
    { CHD_CODEC_ZSTD, CHD_CODEC_LZMA, CHD_CODEC_ZLIB, CHD_CODEC_HUFFMAN };

// CD: cdlz, cdzs, cdzl, cdfl — all 4 compound codecs for best ratio
static const chd_codec_type s_best_cd_compression[4] =
    { CHD_CODEC_CD_LZMA, CHD_CODEC_CD_ZSTD, CHD_CODEC_CD_ZLIB, CHD_CODEC_CD_FLAC };

// Pick the best default compression array based on detected system and content format.
// Returns null if no smart default applies (caller falls back to chdman legacy defaults).
static const chd_codec_type* smart_compression_for(GamePlatform system, const std::string& format)
{
    if (system == GamePlatform::PS2) {
        if (format == "dvd")
            return s_ps2_dvd_compression;
        else
            return s_ps2_cd_compression;
    }
    if (format == "dvd")
        return s_smart_dvd_compression;
    if (format == "cd" || format == "gd")
        return s_smart_cd_compression;
    return nullptr;  // raw / unknown — use chdman defaults
}

// --best: maximum compression ratio arrays
static const chd_codec_type* best_compression_for(const std::string& format)
{
    if (format == "dvd")
        return s_best_dvd_compression;
    if (format == "cd" || format == "gd")
        return s_best_cd_compression;
    return s_default_raw_compression;  // raw/HD: legacy defaults already maximal
}

// Pick the default hunk size based on system and format (0 = let each archive_* decide)
static uint32_t smart_hunk_for(GamePlatform system, const std::string& format)
{
    if (format == "dvd")
        return 2048;  // 1 DVD sector per hunk
    return 0;  // CD/GD/raw use their own defaults
}


// ======================> Raw file compressor (reads from a binary file)

class rawfile_compressor : public chd_file_compressor
{
public:
    rawfile_compressor(util::random_read& file, uint64_t offset, uint64_t maxoffset)
        : m_file(file), m_offset(offset), m_maxoffset(maxoffset)
    {
    }

    uint32_t read_data(void* dest, uint64_t offset, uint32_t length) override
    {
        offset += m_offset;
        if (offset >= m_maxoffset)
            return 0;
        if (offset + length > m_maxoffset)
            length = static_cast<uint32_t>(m_maxoffset - offset);
        if (m_file.seek(offset, SEEK_SET))
            return 0;
        auto const [err, actual] = read(m_file, dest, length);
        return static_cast<uint32_t>(actual);
    }

private:
    util::random_read& m_file;
    uint64_t m_offset;
    uint64_t m_maxoffset;
};


// ======================> CD compressor (reads from CUE/GDI track files)

class cd_compressor : public chd_file_compressor
{
public:
    cd_compressor(cdrom_file::toc& toc, cdrom_file::track_input_info& info)
        : m_toc(toc), m_info(info)
    {
    }

    uint32_t read_data(void* _dest, uint64_t offset, uint32_t length) override
    {
        // verify frame alignment
        assert(offset % cdrom_file::FRAME_SIZE == 0);
        assert(length % cdrom_file::FRAME_SIZE == 0);

        uint8_t* dest = reinterpret_cast<uint8_t*>(_dest);
        std::memset(dest, 0, length);

        uint64_t startoffs = 0;
        uint32_t length_remaining = length;

        for (uint32_t tracknum = 0; tracknum < m_toc.numtrks; tracknum++)
        {
            const cdrom_file::track_info& trackinfo = m_toc.tracks[tracknum];
            uint64_t endoffs = startoffs + static_cast<uint64_t>(trackinfo.frames + trackinfo.extraframes) * cdrom_file::FRAME_SIZE;

            if (offset >= startoffs && offset < endoffs)
            {
                // open track file if needed
                if (!m_file || m_lastfile != m_info.track[tracknum].fname)
                {
                    m_file.reset();
                    m_lastfile = m_info.track[tracknum].fname;
                    std::error_condition filerr = util::core_file::open(m_lastfile, OPEN_FLAG_READ, m_file);
                    if (filerr)
                        throw ChdInputException("Cannot open track file '" + m_lastfile + "': " + filerr.message());
                }

                uint64_t bytesperframe = trackinfo.datasize + trackinfo.subsize;
                uint64_t src_track_start = m_info.track[tracknum].offset;
                uint64_t src_track_end = src_track_start + bytesperframe * static_cast<uint64_t>(trackinfo.frames);
                uint64_t split_track_start = src_track_end - (static_cast<uint64_t>(trackinfo.splitframes) * bytesperframe);
                uint64_t pad_track_start = split_track_start - (static_cast<uint64_t>(trackinfo.padframes) * bytesperframe);

                if (static_cast<uint64_t>(trackinfo.splitframes) == 0)
                    split_track_start = UINT64_MAX;

                while (length_remaining != 0 && offset < endoffs)
                {
                    uint64_t src_frame_start = src_track_start + ((offset - startoffs) / cdrom_file::FRAME_SIZE) * bytesperframe;

                    // auto-advance for split-bin reads
                    if (src_frame_start >= split_track_start && src_frame_start < src_track_end &&
                        m_lastfile != m_info.track[tracknum + 1].fname)
                    {
                        m_file.reset();
                        m_lastfile = m_info.track[tracknum + 1].fname;
                        std::error_condition filerr = util::core_file::open(m_lastfile, OPEN_FLAG_READ, m_file);
                        if (filerr)
                            throw ChdInputException("Cannot open split track file '" + m_lastfile + "': " + filerr.message());
                    }

                    if (src_frame_start < src_track_end)
                    {
                        if (src_frame_start >= pad_track_start && src_frame_start < split_track_start)
                        {
                            std::memset(dest, 0, bytesperframe);
                        }
                        else
                        {
                            std::error_condition err = m_file->seek(
                                (src_frame_start >= split_track_start)
                                    ? src_frame_start - split_track_start
                                    : src_frame_start,
                                SEEK_SET);
                            std::size_t count = 0;
                            if (!err)
                                std::tie(err, count) = read(*m_file, dest, bytesperframe);
                            if (err || count != bytesperframe)
                                throw ChdInputException("Read error in track file '" + m_lastfile + "': unexpected end of data");
                        }

                        // byte-swap audio data if needed
                        if (m_info.track[tracknum].swap)
                            for (uint32_t i = 0; i < 2352; i += 2)
                                std::swap(dest[i], dest[i + 1]);
                    }

                    offset += cdrom_file::FRAME_SIZE;
                    dest += cdrom_file::FRAME_SIZE;
                    length_remaining -= cdrom_file::FRAME_SIZE;
                    if (length_remaining == 0)
                        break;
                }
            }

            startoffs = endoffs;
        }
        return length - length_remaining;
    }

private:
    std::string m_lastfile;
    util::core_file::ptr m_file;
    cdrom_file::toc& m_toc;
    cdrom_file::track_input_info& m_info;
};


// ======================> Implementation

struct ChdArchiver::Impl
{
    // Perform the compress loop with optional progress callback
    static void compress_with_progress(chd_file_compressor& chd, const ArchiveOptions& options,
                                       uint64_t logical_bytes, ArchiveResult& result)
    {
        chd.compress_begin();

        double complete = 0.0, ratio = 0.0;
        std::error_condition err;
        while ((err = chd.compress_continue(complete, ratio)) == chd_file::error::WALKING_PARENT ||
               err == chd_file::error::COMPRESSING)
        {
            if (options.progress_callback && err == chd_file::error::COMPRESSING)
            {
                uint64_t done = static_cast<uint64_t>(complete * logical_bytes);
                if (!options.progress_callback(done, logical_bytes))
                    throw ChdCancelledException("Archive cancelled by user");
            }
        }

        if (err)
            throw ChdCompressionException("Compression failed: " + err.message());

        result.compression_ratio = ratio;
    }

    // Resolve compression codecs from options or defaults.
    // Priority: options.compression[] > options.codec > defaults parameter.
    static void resolve_compression(const ArchiveOptions& options,
                                    const chd_codec_type* defaults,
                                    chd_codec_type (&compression)[4])
    {
        // Explicit uncompressed mode
        if (options.uncompressed) {
            for (int i = 0; i < 4; i++)
                compression[i] = CHD_CODEC_NONE;
            return;
        }

        // Check if user specified the full 4-slot array
        bool has_array = false;
        for (int i = 0; i < 4; i++) {
            if (options.compression[i] != Codec::None) { has_array = true; break; }
        }

        if (has_array)
        {
            for (int i = 0; i < 4; i++)
                compression[i] = chdlite_codec_to_mame(options.compression[i]);
        }
        else if (options.codec != Codec::None)
        {
            // User specified a single codec — fill slot 0, rest NONE
            compression[0] = chdlite_codec_to_mame(options.codec);
            compression[1] = CHD_CODEC_NONE;
            compression[2] = CHD_CODEC_NONE;
            compression[3] = CHD_CODEC_NONE;
        }
        else
        {
            std::memcpy(compression, defaults, sizeof(chd_codec_type) * 4);
        }
    }
};


// ======================> Construction / Destruction

ChdArchiver::ChdArchiver() : m_impl(std::make_unique<Impl>()) {}
ChdArchiver::~ChdArchiver() = default;
ChdArchiver::ChdArchiver(ChdArchiver&&) noexcept = default;
ChdArchiver& ChdArchiver::operator=(ChdArchiver&&) noexcept = default;


// ======================> archive_raw

ArchiveResult ChdArchiver::archive_raw(const std::string& input_path,
                                       const std::string& output_path,
                                       const ArchiveOptions& options)
{
    ArchiveResult result{};
    result.output_path = output_path;

    try
    {
        // Open input file
        util::core_file::ptr input_file;
        std::error_condition filerr = util::core_file::open(input_path, OPEN_FLAG_READ, input_file);
        if (filerr)
            throw ChdInputException("Cannot open input file '" + input_path + "': " + filerr.message());

        // Get input file size
        uint64_t input_size = 0;
        input_file->length(input_size);
        if (input_size == 0)
            throw ChdInputException("Input file is empty: '" + input_path + "'");

        result.input_bytes = input_size;

        // Determine unit and hunk sizes
        uint32_t unit_size = options.unit_bytes > 0 ? options.unit_bytes : 512;
        uint32_t hunk_size = options.hunk_bytes > 0 ? options.hunk_bytes :
            std::max(4096u, (4096u / unit_size) * unit_size);

        // Ensure hunk_size is a multiple of unit_size
        if (hunk_size % unit_size != 0)
            hunk_size = ((hunk_size / unit_size) + 1) * unit_size;

        // Resolve compression
        chd_codec_type compression[4];
        Impl::resolve_compression(options,
            options.best ? s_best_dvd_compression : s_default_raw_compression, compression);

        // Handle parent CHD
        chd_file parent;
        if (!options.parent_chd_path.empty())
        {
            std::error_condition perr = parent.open(options.parent_chd_path);
            if (perr)
                throw ChdParentException("Cannot open parent CHD '" + options.parent_chd_path + "': " + perr.message());
            unit_size = parent.unit_bytes();
        }

        // Create compressor and output CHD
        auto chd = std::make_unique<rawfile_compressor>(*input_file, 0, input_size);

        std::error_condition cerr;
        if (parent.opened())
            cerr = chd->create(output_path, input_size, hunk_size, compression, parent);
        else
            cerr = chd->create(output_path, input_size, hunk_size, unit_size, compression);
        if (cerr)
            throw ChdOutputException("Cannot create output CHD '" + output_path + "': " + cerr.message());

        // Clone metadata from parent if applicable
        if (parent.opened())
            chd->clone_all_metadata(parent);

        // Compress
        Impl::compress_with_progress(*chd, options, input_size, result);

        // Get output file size
        std::ifstream outf(output_path, std::ios::binary | std::ios::ate);
        if (outf.is_open())
            result.output_bytes = static_cast<uint64_t>(outf.tellg());

        result.success = true;
    }
    catch (const ChdException& e)
    {
        if (options.log_callback) options.log_callback(e.severity(), e.what());
        throw;
    }
    catch (const std::exception& e)
    {
        if (options.log_callback) options.log_callback(LogLevel::Error, e.what());
        throw;
    }

    return result;
}


// ======================> archive_cd

ArchiveResult ChdArchiver::archive_cd(const std::string& input_path,
                                      const std::string& output_path,
                                      const ArchiveOptions& options)
{
    ArchiveResult result{};
    result.output_path = output_path;

    try
    {
        // Parse the TOC from input CUE/GDI/TOC/ISO/NRG
        cdrom_file::toc toc = {};
        cdrom_file::track_input_info track_info;

        std::error_condition parse_err = cdrom_file::parse_toc(input_path, toc, track_info);
        if (parse_err)
            throw ChdInputException("Failed to parse TOC from '" + input_path + "': " + parse_err.message());

        if (toc.numtrks == 0)
            throw ChdInputException("No tracks found in '" + input_path + "' — file may be empty or unsupported");

        // Pad each track to 4-frame boundary (same as chdman)
        uint32_t total_sectors = 0;
        for (int i = 0; i < static_cast<int>(toc.numtrks); i++)
        {
            cdrom_file::track_info& ti = toc.tracks[i];
            int padded = (ti.frames + cdrom_file::TRACK_PADDING - 1) / cdrom_file::TRACK_PADDING;
            ti.extraframes = padded * cdrom_file::TRACK_PADDING - ti.frames;
            total_sectors += ti.frames + ti.extraframes;
        }

        uint64_t logical_size = static_cast<uint64_t>(total_sectors) * cdrom_file::FRAME_SIZE;
        result.input_bytes = logical_size;

        // Hunk size: default is FRAMES_PER_HUNK * FRAME_SIZE
        uint32_t hunk_size = options.hunk_bytes > 0 ? options.hunk_bytes :
            cdrom_file::FRAMES_PER_HUNK * cdrom_file::FRAME_SIZE;

        // Resolve compression
        chd_codec_type compression[4];
        Impl::resolve_compression(options,
            options.best ? s_best_cd_compression : s_default_cd_compression, compression);

        // Handle parent
        chd_file parent;
        if (!options.parent_chd_path.empty())
        {
            std::error_condition perr = parent.open(options.parent_chd_path);
            if (perr)
                throw ChdParentException("Cannot open parent CHD '" + options.parent_chd_path + "': " + perr.message());
        }

        // Create CD compressor
        auto chd = std::make_unique<cd_compressor>(toc, track_info);

        std::error_condition cerr;
        if (parent.opened())
            cerr = chd->create(output_path, logical_size, hunk_size, compression, parent);
        else
            cerr = chd->create(output_path, logical_size, hunk_size, cdrom_file::FRAME_SIZE, compression);
        if (cerr)
            throw ChdOutputException("Cannot create output CHD '" + output_path + "': " + cerr.message());

        // Write CD metadata (track types, sizes, etc.)
        std::error_condition merr = cdrom_file::write_metadata(chd.get(), toc);
        if (merr)
            throw ChdMetadataException("Failed to write CD track metadata to '" + output_path + "': " + merr.message());

        // Compress
        Impl::compress_with_progress(*chd, options, logical_size, result);

        // Get output file size
        std::ifstream outf(output_path, std::ios::binary | std::ios::ate);
        if (outf.is_open())
            result.output_bytes = static_cast<uint64_t>(outf.tellg());

        result.success = true;
    }
    catch (const ChdException& e)
    {
        if (options.log_callback) options.log_callback(e.severity(), e.what());
        throw;
    }
    catch (const std::exception& e)
    {
        if (options.log_callback) options.log_callback(LogLevel::Error, e.what());
        throw;
    }

    return result;
}


// ======================> archive_dvd

ArchiveResult ChdArchiver::archive_dvd(const std::string& input_path,
                                       const std::string& output_path,
                                       const ArchiveOptions& options)
{
    ArchiveResult result{};
    result.output_path = output_path;

    try
    {
        // Open input ISO
        util::core_file::ptr input_file;
        std::error_condition filerr = util::core_file::open(input_path, OPEN_FLAG_READ, input_file);
        if (filerr)
            throw ChdInputException("Cannot open input file '" + input_path + "': " + filerr.message());

        uint64_t input_size = 0;
        input_file->length(input_size);
        if (input_size == 0)
            throw ChdInputException("Input file is empty: '" + input_path + "'");

        // DVD sector size is 2048
        if (input_size % 2048 != 0)
            throw ChdInputException("DVD ISO '" + input_path + "' size (" + std::to_string(input_size) + " bytes) is not a multiple of 2048");

        result.input_bytes = input_size;

        // Hunk size: default 2 * 2048 = 4096
        uint32_t hunk_size = options.hunk_bytes > 0 ? options.hunk_bytes : (2 * 2048);

        // Resolve compression
        chd_codec_type compression[4];
        Impl::resolve_compression(options,
            options.best ? s_best_dvd_compression : s_default_dvd_compression, compression);

        // Handle parent
        chd_file parent;
        if (!options.parent_chd_path.empty())
        {
            std::error_condition perr = parent.open(options.parent_chd_path);
            if (perr)
                throw ChdParentException("Cannot open parent CHD '" + options.parent_chd_path + "': " + perr.message());
        }

        // Create compressor
        auto chd = std::make_unique<rawfile_compressor>(*input_file, 0, input_size);

        std::error_condition cerr;
        if (parent.opened())
            cerr = chd->create(output_path, input_size, hunk_size, compression, parent);
        else
            cerr = chd->create(output_path, input_size, hunk_size, 2048, compression);
        if (cerr)
            throw ChdOutputException("Cannot create output CHD '" + output_path + "': " + cerr.message());

        // Write DVD metadata tag
        std::error_condition merr = chd->write_metadata(DVD_METADATA_TAG, 0, "");
        if (merr)
            throw ChdMetadataException("Failed to write DVD metadata tag to '" + output_path + "': " + merr.message());

        // Compress
        Impl::compress_with_progress(*chd, options, input_size, result);

        // Get output file size
        std::ifstream outf(output_path, std::ios::binary | std::ios::ate);
        if (outf.is_open())
            result.output_bytes = static_cast<uint64_t>(outf.tellg());

        result.success = true;
    }
    catch (const ChdException& e)
    {
        if (options.log_callback) options.log_callback(e.severity(), e.what());
        throw;
    }
    catch (const std::exception& e)
    {
        if (options.log_callback) options.log_callback(LogLevel::Error, e.what());
        throw;
    }

    return result;
}


// ======================> Sanitize filename for rename

static std::string sanitize_filename(const std::string& title)
{
    std::string out;
    out.reserve(title.size());
    for (char c : title) {
        // Replace characters not safe for filenames
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            out += '_';
        else if (static_cast<unsigned char>(c) >= 0x20)
            out += c;
    }
    // Trim trailing dots/spaces (Windows compat)
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    return out;
}


// ======================> archive (auto-detect)

ArchiveResult ChdArchiver::archive(const std::string& input_path,
                                   const std::string& output_path,
                                   const ArchiveOptions& options)
{
    // Apply CUE fixes before anything else
    if (has_fix_cue(options.fix_cue, FixCue::Single) && path_ext_lower(input_path) == ".cue")
        fix_cue_track1(input_path);

    // Run pre-archive detection
    bool need_title = options.detect_title || options.rename_to_title || options.rename_to_gameid;
    DetectionResult detection = detect_input(input_path, need_title);

    // Strict format check: if both CD and DVD platforms were identified, the image is ambiguous.
    if (m_strict && detection.format_conflict) {
        throw ChdFormatException(
            "Ambiguous format in '" + input_path + "': " + detection.format_conflict_detail +
            " — use set_strict(false) to force conversion with best-guess format");
    }

    // Determine format from explicit option or detection result
    std::string fmt = options.input_format;
    if (fmt.empty()) {
        std::string ext = path_ext_lower(input_path);
        if (ext == ".cue" || ext == ".gdi" || ext == ".toc" || ext == ".nrg")
            fmt = "cd";
        else if (ext == ".iso" || ext == ".bin" || ext == ".img")
            fmt = detection.format;  // "cd" or "dvd" from detect_input
        else
            fmt = "raw";
    }

    // GD-ROM detection (Dreamcast GDI files)
    std::string smart_fmt = fmt;
    if (path_ext_lower(input_path) == ".gdi")
        smart_fmt = "gd";
    else
        smart_fmt = fmt;

    // Apply smart defaults when user hasn't specified codecs
    ArchiveOptions effective = options;
    if (!options.has_custom_compression()) {
        const chd_codec_type* chosen = nullptr;
        if (options.best)
            chosen = best_compression_for(smart_fmt);
        else if (!options.chdman_compat)
            chosen = smart_compression_for(detection.game_platform, smart_fmt);
        if (chosen) {
            // Convert mame codec types back to our Codec enum for the effective options
            // Actually, we'll inject them directly in the archive_* calls via a different path.
            // Simpler: store the smart defaults in the compression[] array.
            for (int i = 0; i < 4; i++) {
                // Map back from chd_codec_type to Codec
                switch (chosen[i]) {
                case CHD_CODEC_ZLIB:     effective.compression[i] = Codec::Zlib;     break;
                case CHD_CODEC_ZSTD:     effective.compression[i] = Codec::Zstd;     break;
                case CHD_CODEC_LZMA:     effective.compression[i] = Codec::LZMA;     break;
                case CHD_CODEC_HUFFMAN:  effective.compression[i] = Codec::Huffman;  break;
                case CHD_CODEC_FLAC:     effective.compression[i] = Codec::FLAC;     break;
                case CHD_CODEC_CD_ZLIB:  effective.compression[i] = Codec::CD_Zlib;  break;
                case CHD_CODEC_CD_ZSTD:  effective.compression[i] = Codec::CD_Zstd;  break;
                case CHD_CODEC_CD_LZMA:  effective.compression[i] = Codec::CD_LZMA;  break;
                case CHD_CODEC_CD_FLAC:  effective.compression[i] = Codec::CD_FLAC;  break;
                default:                 effective.compression[i] = Codec::None;     break;
                }
            }
        }
    }

    // Apply smart hunk size when user hasn't specified one
    if (effective.hunk_bytes == 0) {
        uint32_t smart_hunk = smart_hunk_for(detection.game_platform, fmt);
        if (smart_hunk > 0)
            effective.hunk_bytes = smart_hunk;
    }

    ArchiveResult result;

    if (fmt == "cd" || fmt == "cue" || fmt == "gdi" || fmt == "toc" || fmt == "nrg" || fmt == "iso")
        result = archive_cd(input_path, output_path, effective);
    else if (fmt == "dvd")
        result = archive_dvd(input_path, output_path, effective);
    else
        result = archive_raw(input_path, output_path, effective);

    // Populate detection results
    result.detected_game_platform    = detection.game_platform;
    result.detected_title            = detection.title;
    result.detected_manufacturer_id  = detection.manufacturer_id;
    result.detected_format_source    = detection.format_source;
    result.detected_platform_source  = detection.platform_source;

    // Emit detection summary at Debug level if a log callback is installed
    if (options.log_callback) {
        auto fs_name = [](FormatSource s) -> const char* {
            switch (s) {
            case FormatSource::Extension:   return "extension";
            case FormatSource::SyncBytes:   return "sync-bytes (raw CD sectors)";
            case FormatSource::DvdMatch:    return "dvd-match (platform identified)";
            case FormatSource::DvdFallback: return "dvd-fallback (no specific match)";
            case FormatSource::CdOverride:  return "cd-override (CD platform beat DVD fallback)";
            default:                        return "unknown";
            }
        };
        auto ps_name = [](PlatformSource s) -> const char* {
            switch (s) {
            case PlatformSource::Sector0:   return "sector-0 magic";
            case PlatformSource::Iso9660:   return "ISO 9660 filesystem";
            case PlatformSource::Heuristic: return "heuristic";
            case PlatformSource::Default:   return "default (no match)";
            default:                        return "unknown";
            }
        };
        std::string msg = std::string("detect: format-source=") + fs_name(detection.format_source);
        if (detection.game_platform != GamePlatform::Unknown)
            msg += std::string(" platform=") + game_platform_name(detection.game_platform)
                 + " platform-source=" + ps_name(detection.platform_source);
        options.log_callback(LogLevel::Debug, msg);
    }

    // Rename output to title or game ID if requested and successful
    std::string rename_label;
    if (result.success && options.rename_to_gameid && !detection.manufacturer_id.empty())
        rename_label = sanitize_filename(detection.manufacturer_id);
    else if (result.success && options.rename_to_title && !detection.title.empty())
        rename_label = sanitize_filename(detection.title);

    if (!rename_label.empty()) {
        namespace fs = std::filesystem;
        fs::path out(result.output_path);
        fs::path new_path = out.parent_path() / (rename_label + ".chd");
        try {
            if (!fs::exists(new_path)) {
                fs::rename(out, new_path);
                result.output_path = new_path.string();
            }
        } catch (...) {
            // Rename failed — keep original path, not a fatal error
        }
    }

    return result;
}


// ======================> CHD-to-CHD compressor (reads from an existing chd_file)

class chdfile_compressor : public chd_file_compressor
{
public:
    chdfile_compressor(chd_file& source, uint64_t maxoffset)
        : m_source(source), m_maxoffset(maxoffset)
    {
    }

    uint32_t read_data(void* dest, uint64_t offset, uint32_t length) override
    {
        if (offset >= m_maxoffset)
            return 0;
        if (offset + length > m_maxoffset)
            length = static_cast<uint32_t>(m_maxoffset - offset);
        auto err = m_source.read_bytes(offset, dest, length);
        if (err)
            return 0;
        return length;
    }

private:
    chd_file& m_source;
    uint64_t m_maxoffset;
};


// ======================> copy (re-compress CHD→CHD)

ArchiveResult ChdArchiver::copy(const std::string& input_path,
                                const std::string& output_path,
                                const ArchiveOptions& options)
{
    ArchiveResult result{};
    result.output_path = output_path;

    try
    {
        // Open input CHD
        chd_file input_chd;
        auto err = input_chd.open(input_path);
        if (err)
            throw ChdInputException("Cannot open input CHD '" + input_path + "': " + err.message());

        uint64_t logical_bytes = input_chd.logical_bytes();
        uint32_t src_hunk_bytes = input_chd.hunk_bytes();
        uint32_t src_unit_bytes = input_chd.unit_bytes();
        result.input_bytes = logical_bytes;

        // Determine output parameters
        uint32_t hunk_bytes = options.hunk_bytes > 0 ? options.hunk_bytes : src_hunk_bytes;
        uint32_t unit_bytes = options.unit_bytes > 0 ? options.unit_bytes : src_unit_bytes;

        // Ensure hunk_size is a multiple of unit_size
        if (hunk_bytes % unit_bytes != 0)
            hunk_bytes = ((hunk_bytes / unit_bytes) + 1) * unit_bytes;

        // Detect content type for smart codec defaults
        ContentType ct = ContentType::Raw;
        if (!input_chd.check_is_gd())      ct = ContentType::GDROM;
        else if (!input_chd.check_is_cd())  ct = ContentType::CDROM;
        else if (!input_chd.check_is_dvd()) ct = ContentType::DVD;
        else if (!input_chd.check_is_hd())  ct = ContentType::HardDisk;

        bool is_cd_type = (ct == ContentType::CDROM || ct == ContentType::GDROM);

        // Resolve compression codecs
        chd_codec_type compression[4];
        const chd_codec_type* defaults = is_cd_type
            ? (options.best ? s_best_cd_compression : s_default_cd_compression)
            : (options.best ? s_best_dvd_compression : s_default_raw_compression);
        Impl::resolve_compression(options, defaults, compression);

        // Handle parent CHD for output
        chd_file output_parent;
        if (!options.parent_chd_path.empty())
        {
            auto perr = output_parent.open(options.parent_chd_path);
            if (perr)
                throw ChdParentException("Cannot open output parent CHD '" + options.parent_chd_path + "': " + perr.message());
        }

        // Create compressor
        auto chd = std::make_unique<chdfile_compressor>(input_chd, logical_bytes);

        std::error_condition cerr;
        if (output_parent.opened())
            cerr = chd->create(output_path, logical_bytes, hunk_bytes, compression, output_parent);
        else
            cerr = chd->create(output_path, logical_bytes, hunk_bytes, unit_bytes, compression);
        if (cerr)
            throw ChdOutputException("Cannot create output CHD '" + output_path + "': " + cerr.message());

        // Clone all metadata from input
        chd->clone_all_metadata(input_chd);

        // Compress
        Impl::compress_with_progress(*chd, options, logical_bytes, result);

        // Get output file size
        std::ifstream outf(output_path, std::ios::binary | std::ios::ate);
        if (outf.is_open())
            result.output_bytes = static_cast<uint64_t>(outf.tellg());

        result.success = true;
    }
    catch (const ChdException& e)
    {
        if (options.log_callback) options.log_callback(e.severity(), e.what());
        throw;
    }
    catch (const std::exception& e)
    {
        if (options.log_callback) options.log_callback(LogLevel::Error, e.what());
        throw;
    }

    return result;
}

} // namespace chdlite
