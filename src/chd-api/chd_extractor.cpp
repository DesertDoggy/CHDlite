// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_extractor.cpp - Extraction implementation

#include "chd_extractor.hpp"
#include "chd_reader.hpp"

#include "chd.h"
#include "cdrom.h"
#include "chdcodec.h"
#include "corefile.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace chdlite {

// ======================> Helpers

static std::string path_stem(const std::string& path)
{
    auto sep = path.find_last_of("/\\");
    auto name = (sep == std::string::npos) ? path : path.substr(sep + 1);
    auto dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

static std::string path_dir(const std::string& path)
{
    auto sep = path.find_last_of("/\\");
    return (sep == std::string::npos) ? "." : path.substr(0, sep);
}

static std::string path_ext(const std::string& path)
{
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

static std::string path_join(const std::string& dir, const std::string& file)
{
    if (dir.empty()) return file;
    char last = dir.back();
    if (last == '/' || last == '\\') return dir + file;
    return dir + "/" + file;
}

static const char* cue_track_type_string(uint32_t trktype, uint32_t datasize)
{
    switch (trktype)
    {
    case cdrom_file::CD_TRACK_MODE1:
        return (datasize == 2048) ? "MODE1/2048" : "MODE1/2352";
    case cdrom_file::CD_TRACK_MODE1_RAW:
        return "MODE1/2352";
    case cdrom_file::CD_TRACK_MODE2:
    case cdrom_file::CD_TRACK_MODE2_FORM1:
    case cdrom_file::CD_TRACK_MODE2_FORM2:
    case cdrom_file::CD_TRACK_MODE2_FORM_MIX:
        return (datasize == 2048) ? "MODE2/2048" : "MODE2/2352";
    case cdrom_file::CD_TRACK_MODE2_RAW:
        return "MODE2/2352";
    case cdrom_file::CD_TRACK_AUDIO:
        return "AUDIO";
    default:
        return "MODE1/2352";
    }
}

// ======================> Impl

struct ChdExtractor::Impl
{
    // Nothing stored persistently: each extract call is self-contained
};

ChdExtractor::ChdExtractor() : m_impl(std::make_unique<Impl>()) {}
ChdExtractor::~ChdExtractor() = default;
ChdExtractor::ChdExtractor(ChdExtractor&&) noexcept = default;
ChdExtractor& ChdExtractor::operator=(ChdExtractor&&) noexcept = default;

ExtractionResult ChdExtractor::extract(const std::string& chd_path,
                                       const ExtractOptions& options)
{
    return extract(chd_path, std::string(), options);
}

ExtractionResult ChdExtractor::extract(const std::string& chd_path,
                                       const std::string& parent_chd_path,
                                       const ExtractOptions& options)
{
    ExtractionResult result = {};
    result.success = false;

    try
    {
        // Open CHD to detect content type
        chd_file input_chd;
        auto err = input_chd.open(chd_path, false);
        if (err)
        {
            result.error_message = "Failed to open CHD: " + err.message();
            return result;
        }

        // Detect content type
        ContentType type = ContentType::Raw;
        if (!input_chd.check_is_gd())        type = ContentType::GDROM;
        else if (!input_chd.check_is_cd())    type = ContentType::CDROM;
        else if (!input_chd.check_is_dvd())   type = ContentType::DVD;
        else if (!input_chd.check_is_av())    type = ContentType::LaserDisc;
        else if (!input_chd.check_is_hd())    type = ContentType::HardDisk;

        result.detected_type = type;
        input_chd.close();

        // Route to appropriate extractor
        if (type == ContentType::CDROM || type == ContentType::GDROM)
        {
            if (!options.force_raw)
                return do_extract_cd(chd_path, parent_chd_path, options);
        }

        // Raw/HD/DVD/LD: extract as raw binary
        return do_extract_raw(chd_path, parent_chd_path, options);
    }
    catch (const std::exception& e)
    {
        result.error_message = e.what();
        return result;
    }
}

// ======================> Raw extraction (HD, DVD, raw, or forced-raw CD)

ExtractionResult ChdExtractor::do_extract_raw(const std::string& chd_path,
                                              const std::string& parent_path,
                                              const ExtractOptions& options)
{
    ExtractionResult result = {};
    result.success = false;

    chd_file input_chd;
    auto err = input_chd.open(chd_path, false);
    if (err)
    {
        result.error_message = "Failed to open CHD: " + err.message();
        return result;
    }

    // Determine output path
    std::string out_dir = options.output_dir.empty() ? path_dir(chd_path) : options.output_dir;
    std::string out_name = options.output_filename.empty()
        ? (path_stem(chd_path) + ".bin")
        : options.output_filename;
    std::string out_path = path_join(out_dir, out_name);

    std::ofstream outfile(out_path, std::ios::binary);
    if (!outfile)
    {
        result.error_message = "Cannot create output file: " + out_path;
        return result;
    }

    const uint64_t total_bytes = input_chd.logical_bytes();
    const uint32_t hunk_bytes = input_chd.hunk_bytes();
    constexpr uint32_t BUFFER_SIZE = 32 * 1024 * 1024; // 32 MB
    const uint32_t hunks_per_read = std::max(1u, BUFFER_SIZE / hunk_bytes);
    std::vector<uint8_t> buffer(hunks_per_read * hunk_bytes);

    uint64_t bytes_written = 0;
    uint32_t hunk_num = 0;
    const uint32_t total_hunks = input_chd.hunk_count();

    while (hunk_num < total_hunks)
    {
        uint32_t hunks_this_read = std::min(hunks_per_read, total_hunks - hunk_num);
        uint64_t bytes_to_read = std::min(
            uint64_t(hunks_this_read) * hunk_bytes,
            total_bytes - bytes_written);

        err = input_chd.read_bytes(bytes_written, buffer.data(), static_cast<uint32_t>(bytes_to_read));
        if (err)
        {
            result.error_message = "Read error at offset " + std::to_string(bytes_written);
            return result;
        }

        outfile.write(reinterpret_cast<const char*>(buffer.data()), bytes_to_read);
        if (!outfile)
        {
            result.error_message = "Write error at offset " + std::to_string(bytes_written);
            return result;
        }

        bytes_written += bytes_to_read;
        hunk_num += hunks_this_read;

        if (options.progress_callback)
        {
            if (!options.progress_callback(bytes_written, total_bytes))
            {
                result.error_message = "Extraction cancelled";
                return result;
            }
        }
    }

    outfile.close();
    result.success = true;
    result.output_path = out_path;
    result.output_files.push_back(out_path);
    result.bytes_written = bytes_written;
    return result;
}

// ======================> CD extraction (CUE/BIN or GDI)

ExtractionResult ChdExtractor::do_extract_cd(const std::string& chd_path,
                                             const std::string& parent_path,
                                             const ExtractOptions& options)
{
    ExtractionResult result = {};
    result.success = false;

    chd_file input_chd;
    auto err = input_chd.open(chd_path, false);
    if (err)
    {
        result.error_message = "Failed to open CHD: " + err.message();
        return result;
    }

    // Parse CD TOC from metadata
    cdrom_file::toc toc = {};
    err = cdrom_file::parse_metadata(&input_chd, toc);
    if (err)
    {
        result.error_message = "Failed to parse CD metadata: " + err.message();
        return result;
    }

    // Open the cdrom_file for reading sectors
    cdrom_file cd(&input_chd);
    bool is_gdrom = cd.is_gdrom();

    // Determine output format
    std::string out_dir = options.output_dir.empty() ? path_dir(chd_path) : options.output_dir;
    std::string stem = path_stem(chd_path);

    // Decide format: GDI for GD-ROM, CUE/BIN otherwise (unless forced)
    std::string meta_ext;
    bool gdi_mode;

    if (!options.output_filename.empty())
    {
        meta_ext = path_ext(options.output_filename);
        gdi_mode = (meta_ext == ".gdi");
    }
    else
    {
        gdi_mode = is_gdrom && !options.force_bin_cue;
        meta_ext = gdi_mode ? ".gdi" : ".cue";
    }

    std::string meta_filename = options.output_filename.empty()
        ? (stem + meta_ext)
        : options.output_filename;
    std::string meta_path = path_join(out_dir, meta_filename);

    // Track extraction
    uint64_t total_frames = 0;
    for (uint32_t t = 0; t < toc.numtrks; t++)
        total_frames += toc.tracks[t].frames + toc.tracks[t].extraframes;

    uint64_t frames_done = 0;
    uint64_t total_bytes_written = 0;
    std::ostringstream meta_content;

    if (gdi_mode)
        meta_content << toc.numtrks << "\n";

    uint32_t cue_bin_offset = 0; // running offset for single-bin CUE mode

    // For CUE: single BIN file unless split
    bool split_bins = gdi_mode; // GDI always splits; CUE uses single BIN
    std::string single_bin_name = stem + ".bin";
    std::string single_bin_path = path_join(out_dir, single_bin_name);
    std::ofstream single_bin;

    if (!split_bins)
    {
        single_bin.open(single_bin_path, std::ios::binary);
        if (!single_bin)
        {
            result.error_message = "Cannot create BIN file: " + single_bin_path;
            return result;
        }
        result.output_files.push_back(single_bin_path);
    }

    for (uint32_t t = 0; t < toc.numtrks; t++)
    {
        auto& track = toc.tracks[t];
        const uint32_t sector_size = track.datasize;
        const bool is_audio = (track.trktype == cdrom_file::CD_TRACK_AUDIO);

        // Per-track BIN file for GDI
        std::string track_bin_name;
        std::string track_bin_path;
        std::ofstream track_bin;

        if (split_bins)
        {
            std::ostringstream fname;
            if (is_audio)
                fname << stem << " (Track " << (t + 1) << ").raw";
            else
                fname << stem << " (Track " << (t + 1) << ").bin";
            track_bin_name = fname.str();
            track_bin_path = path_join(out_dir, track_bin_name);
            track_bin.open(track_bin_path, std::ios::binary);
            if (!track_bin)
            {
                result.error_message = "Cannot create track file: " + track_bin_path;
                return result;
            }
            result.output_files.push_back(track_bin_path);
        }

        std::ofstream& bin_out = split_bins ? track_bin : single_bin;

        // Write pregap (zeros) if CUE mode
        uint32_t pregap_frames = track.pregap;

        // For CUE mode: write INDEX 00 (pregap) before actual data
        // Position tracking for CUE
        uint32_t track_file_offset = split_bins ? 0 : cue_bin_offset;

        // Extract track frames
        uint32_t phys_offset = track.physframeofs;
        std::vector<uint8_t> sector_buf(cdrom_file::MAX_SECTOR_DATA);

        for (uint32_t f = 0; f < track.frames; f++)
        {
            if (!cd.read_data(phys_offset + f, sector_buf.data(),
                              track.trktype, true))
            {
                // Fill with zeros on read error
                std::memset(sector_buf.data(), 0, sector_size);
            }

            // Byte-swap audio for CUE/GDI output (MAME stores in big-endian)
            if (is_audio && !(toc.flags & cdrom_file::CD_FLAG_GDROMLE))
            {
                for (uint32_t i = 0; i < sector_size - 1; i += 2)
                    std::swap(sector_buf[i], sector_buf[i + 1]);
            }

            bin_out.write(reinterpret_cast<const char*>(sector_buf.data()), sector_size);
            total_bytes_written += sector_size;
            frames_done++;

            if (options.progress_callback && (frames_done % 1000 == 0))
            {
                if (!options.progress_callback(frames_done, total_frames))
                {
                    result.error_message = "Extraction cancelled";
                    return result;
                }
            }
        }

        cue_bin_offset += track.frames * sector_size;

        // Write metadata entry
        if (gdi_mode)
        {
            // GDI format: track# startlba type sectorsize filename offset
            int gdi_type = is_audio ? 0 : 4;
            meta_content << (t + 1) << " "
                         << track.logframeofs << " "
                         << gdi_type << " "
                         << sector_size << " "
                         << "\"" << track_bin_name << "\" "
                         << 0 << "\n";
        }
        else
        {
            // CUE format
            if (split_bins || t == 0)
            {
                meta_content << "FILE \"" << (split_bins ? track_bin_name : single_bin_name)
                             << "\" BINARY\n";
            }

            char track_num[8];
            std::snprintf(track_num, sizeof(track_num), "%02u", t + 1);
            meta_content << "  TRACK " << track_num << " "
                         << cue_track_type_string(track.trktype, sector_size) << "\n";

            if (pregap_frames > 0)
            {
                uint32_t m = pregap_frames / (75 * 60);
                uint32_t s = (pregap_frames / 75) % 60;
                uint32_t f = pregap_frames % 75;
                char msf[16];
                std::snprintf(msf, sizeof(msf), "%02u:%02u:%02u", m, s, f);
                meta_content << "    PREGAP " << msf << "\n";
            }

            // INDEX 01 position (in the output file)
            uint32_t idx_frame = split_bins ? 0 : (track_file_offset / sector_size);
            uint32_t m = idx_frame / (75 * 60);
            uint32_t s = (idx_frame / 75) % 60;
            uint32_t f_idx = idx_frame % 75;
            char msf[16];
            std::snprintf(msf, sizeof(msf), "%02u:%02u:%02u", m, s, f_idx);
            meta_content << "    INDEX 01 " << msf << "\n";
        }
    }

    // Write metadata file
    {
        std::ofstream meta_out(meta_path);
        if (!meta_out)
        {
            result.error_message = "Cannot create metadata file: " + meta_path;
            return result;
        }
        meta_out << meta_content.str();
        result.output_files.insert(result.output_files.begin(), meta_path);
    }

    result.success = true;
    result.output_path = meta_path;
    result.bytes_written = total_bytes_written;
    result.detected_type = is_gdrom ? ContentType::GDROM : ContentType::CDROM;
    return result;
}

} // namespace chdlite
