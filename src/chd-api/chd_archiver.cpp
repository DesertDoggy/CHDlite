// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_archiver.cpp - Archive (create CHD) implementation

#include "chd_archiver.hpp"
#include "chd_reader.hpp"
#include "detect_system.hpp"

#include "chd.h"
#include "cdrom.h"
#include "chdcodec.h"
#include "corefile.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace chdlite {

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

// PS2 CD: cdzl (CD ZLIB)
static const chd_codec_type s_ps2_cd_compression[4] =
    { CHD_CODEC_CD_ZLIB, CHD_CODEC_NONE, CHD_CODEC_NONE, CHD_CODEC_NONE };

// Other DVD: zstd
static const chd_codec_type s_smart_dvd_compression[4] =
    { CHD_CODEC_ZSTD, CHD_CODEC_NONE, CHD_CODEC_NONE, CHD_CODEC_NONE };

// Other CD / GD-ROM: cdzs (CD ZSTD)
static const chd_codec_type s_smart_cd_compression[4] =
    { CHD_CODEC_CD_ZSTD, CHD_CODEC_NONE, CHD_CODEC_NONE, CHD_CODEC_NONE };

// Pick the best default compression array based on detected system and content format.
// Returns null if no smart default applies (caller falls back to chdman legacy defaults).
static const chd_codec_type* smart_compression_for(CdSystem system, const std::string& format)
{
    if (system == CdSystem::PS2) {
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

// Pick the default hunk size based on system and format (0 = let each archive_* decide)
static uint32_t smart_hunk_for(CdSystem system, const std::string& format)
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
                        throw ChdException("Error opening input file: " + m_lastfile);
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
                            throw ChdException("Error opening input file: " + m_lastfile);
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
                                throw ChdException("Error reading input file: " + m_lastfile);
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
                    throw ChdException("Archive cancelled by user");
            }
        }

        if (err)
            throw ChdException("Compression error: " + err.message());

        result.compression_ratio = ratio;
    }

    // Resolve compression codecs from options or defaults.
    // Priority: options.compression[] > options.codec > defaults parameter.
    static void resolve_compression(const ArchiveOptions& options,
                                    const chd_codec_type* defaults,
                                    chd_codec_type (&compression)[4])
    {
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
            throw ChdException("Cannot open input file: " + input_path + " (" + filerr.message() + ")");

        // Get input file size
        uint64_t input_size = 0;
        input_file->length(input_size);
        if (input_size == 0)
            throw ChdException("Input file is empty: " + input_path);

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
        Impl::resolve_compression(options, s_default_raw_compression, compression);

        // Handle parent CHD
        chd_file parent;
        if (!options.parent_chd_path.empty())
        {
            std::error_condition perr = parent.open(options.parent_chd_path);
            if (perr)
                throw ChdException("Cannot open parent CHD: " + options.parent_chd_path);
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
            throw ChdException("Cannot create output CHD: " + output_path + " (" + cerr.message() + ")");

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
        result.success = false;
        result.error_message = e.what();
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.error_message = e.what();
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
            throw ChdException("Error parsing input: " + input_path + " (" + parse_err.message() + ")");

        if (toc.numtrks == 0)
            throw ChdException("No tracks found in input: " + input_path);

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
        Impl::resolve_compression(options, s_default_cd_compression, compression);

        // Handle parent
        chd_file parent;
        if (!options.parent_chd_path.empty())
        {
            std::error_condition perr = parent.open(options.parent_chd_path);
            if (perr)
                throw ChdException("Cannot open parent CHD: " + options.parent_chd_path);
        }

        // Create CD compressor
        auto chd = std::make_unique<cd_compressor>(toc, track_info);

        std::error_condition cerr;
        if (parent.opened())
            cerr = chd->create(output_path, logical_size, hunk_size, compression, parent);
        else
            cerr = chd->create(output_path, logical_size, hunk_size, cdrom_file::FRAME_SIZE, compression);
        if (cerr)
            throw ChdException("Cannot create output CHD: " + output_path + " (" + cerr.message() + ")");

        // Write CD metadata (track types, sizes, etc.)
        std::error_condition merr = cdrom_file::write_metadata(chd.get(), toc);
        if (merr)
            throw ChdException("Error writing CD metadata: " + merr.message());

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
        result.success = false;
        result.error_message = e.what();
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.error_message = e.what();
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
            throw ChdException("Cannot open input file: " + input_path + " (" + filerr.message() + ")");

        uint64_t input_size = 0;
        input_file->length(input_size);
        if (input_size == 0)
            throw ChdException("Input file is empty: " + input_path);

        // DVD sector size is 2048
        if (input_size % 2048 != 0)
            throw ChdException("DVD ISO size is not a multiple of 2048 bytes");

        result.input_bytes = input_size;

        // Hunk size: default 2 * 2048 = 4096
        uint32_t hunk_size = options.hunk_bytes > 0 ? options.hunk_bytes : (2 * 2048);

        // Resolve compression
        chd_codec_type compression[4];
        Impl::resolve_compression(options, s_default_dvd_compression, compression);

        // Handle parent
        chd_file parent;
        if (!options.parent_chd_path.empty())
        {
            std::error_condition perr = parent.open(options.parent_chd_path);
            if (perr)
                throw ChdException("Cannot open parent CHD: " + options.parent_chd_path);
        }

        // Create compressor
        auto chd = std::make_unique<rawfile_compressor>(*input_file, 0, input_size);

        std::error_condition cerr;
        if (parent.opened())
            cerr = chd->create(output_path, input_size, hunk_size, compression, parent);
        else
            cerr = chd->create(output_path, input_size, hunk_size, 2048, compression);
        if (cerr)
            throw ChdException("Cannot create output CHD: " + output_path + " (" + cerr.message() + ")");

        // Write DVD metadata tag
        std::error_condition merr = chd->write_metadata(DVD_METADATA_TAG, 0, "");
        if (merr)
            throw ChdException("Error writing DVD metadata: " + merr.message());

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
        result.success = false;
        result.error_message = e.what();
    }
    catch (const std::exception& e)
    {
        result.success = false;
        result.error_message = e.what();
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
    // Run pre-archive detection
    bool need_title = options.detect_title || options.rename_to_title || options.rename_to_gameid;
    DetectionResult detection = detect_input(input_path, need_title);

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
        const chd_codec_type* smart = smart_compression_for(detection.system, smart_fmt);
        if (smart) {
            // Convert mame codec types back to our Codec enum for the effective options
            // Actually, we'll inject them directly in the archive_* calls via a different path.
            // Simpler: store the smart defaults in the compression[] array.
            for (int i = 0; i < 4; i++) {
                // Map back from chd_codec_type to Codec
                switch (smart[i]) {
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
        uint32_t smart_hunk = smart_hunk_for(detection.system, fmt);
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
    result.detected_system = detection.system;
    result.detected_title = detection.title;
    result.detected_gameid = detection.game_id;

    // Rename output to title or game ID if requested and successful
    std::string rename_label;
    if (result.success && options.rename_to_gameid && !detection.game_id.empty())
        rename_label = sanitize_filename(detection.game_id);
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

} // namespace chdlite
