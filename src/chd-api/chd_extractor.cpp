// license:BSD-3-Clause
// CHDlite - Cross-platform CHD library
// chd_extractor.cpp - Extraction implementation using ChdProcessor

#include "chd_extractor.hpp"
#include "chd_processor.hpp"

#include "chd.h"
#include "cdrom.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace chdlite {

// ======================> Path helpers

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

static std::string msf_from_frames(uint32_t frames)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                  frames / (75 * 60), (frames / 75) % 60, frames % 75);
    return buf;
}

static const char* cue_track_type_string(TrackType trktype, uint32_t datasize)
{
    switch (trktype)
    {
    case TrackType::Mode1:
        return (datasize == 2048) ? "MODE1/2048" : "MODE1/2352";
    case TrackType::Mode1Raw:
        return "MODE1/2352";
    case TrackType::Mode2:
    case TrackType::Mode2Form1:
    case TrackType::Mode2Form2:
    case TrackType::Mode2FormMix:
        return (datasize == 2048) ? "MODE2/2048" : "MODE2/2352";
    case TrackType::Mode2Raw:
        return "MODE2/2352";
    case TrackType::Audio:
        return "AUDIO";
    default:
        return "MODE1/2352";
    }
}

// ======================> File-writing sink for CD/GD-ROM

class CdFileSink : public ProcessorSink
{
public:
    CdFileSink(const std::string& out_dir, const std::string& stem,
               bool gdi_mode, bool split_bin,
               const std::string& bin_template = {})
        : m_out_dir(out_dir), m_stem(stem)
        , m_gdi_mode(gdi_mode), m_split_bin(split_bin)
        , m_bin_template(bin_template) {}

    void on_begin(ContentType type, uint32_t num_tracks) override
    {
        m_num_tracks = num_tracks;
        if (m_gdi_mode)
            m_meta << num_tracks << "\n";
    }

    void on_track_begin(uint32_t track_num, TrackType track_type,
                        bool is_audio, uint32_t data_size,
                        uint32_t frames,
                        uint32_t pregap,
                        uint32_t pgdatasize,
                        uint32_t postgap) override
    {
        m_cur_track = track_num;
        m_cur_type = track_type;
        m_cur_audio = is_audio;
        m_cur_datasize = data_size;
        m_cur_toc_frames = frames;
        m_cur_pregap = pregap;
        m_cur_pgdatasize = pgdatasize;
        m_cur_postgap = postgap;
        m_track_frames = 0;

        // Determine bin filename
        if (!m_bin_template.empty())
        {
            // User-specified template: replace %t with track number
            m_cur_bin_name = m_bin_template;
            char tbuf[8];
            std::snprintf(tbuf, sizeof(tbuf), "%02u", track_num);
            auto pos = m_cur_bin_name.find("%t");
            if (pos != std::string::npos)
                m_cur_bin_name.replace(pos, 2, tbuf);
        }
        else if (!m_split_bin && !m_gdi_mode)
        {
            // Single bin file for all tracks
            m_cur_bin_name = m_stem + ".bin";
        }
        else if (m_num_tracks == 1 && !m_gdi_mode)
        {
            m_cur_bin_name = m_stem + ".bin";
        }
        else if (m_gdi_mode)
        {
            // GDI: stemNN.bin / stemNN.raw (chdman convention)
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02u", track_num);
            m_cur_bin_name = m_stem + buf + (is_audio ? ".raw" : ".bin");
        }
        else
        {
            // CUE: stem (Track N).bin if <10 tracks, stem (Track NN).bin if >=10
            char buf[8];
            if (m_num_tracks >= 10)
                std::snprintf(buf, sizeof(buf), "%02u", track_num);
            else
                std::snprintf(buf, sizeof(buf), "%u", track_num);
            m_cur_bin_name = m_stem + " (Track " + buf + ").bin";
        }

        std::string bin_path = path_join(m_out_dir, m_cur_bin_name);
        bool is_same_file = (!m_split_bin && !m_gdi_mode && m_cur_file.is_open());
        if (!is_same_file)
        {
            if (m_cur_file.is_open()) m_cur_file.close();
            auto mode = std::ios::binary;
            if (!m_split_bin && !m_gdi_mode && m_cur_track > 1)
                mode |= std::ios::app;
            m_cur_file.open(bin_path, mode);
            if (!m_cur_file)
                m_error = "Cannot create track file: " + bin_path;
            // Only add to output_files once for single-bin mode
            bool already_listed = false;
            for (auto& f : m_output_files) if (f == bin_path) { already_listed = true; break; }
            if (!already_listed)
                m_output_files.push_back(bin_path);
        }
        m_cur_track_byte_ofs = m_cumulative_bytes;
    }

    void on_data(const void* data, uint32_t len) override
    {
        if (m_cur_file)
        {
            m_cur_file.write(reinterpret_cast<const char*>(data), len);
            m_bytes_written += len;
            m_cumulative_bytes += len;
        }
        m_track_frames++;
    }

    void on_track_end() override
    {
        if (m_split_bin || m_gdi_mode)
            m_cur_file.close();

        // Write metadata entry
        if (m_gdi_mode)
        {
            int gdi_type = m_cur_audio ? 0 : 4;
            m_meta << m_cur_track << " "
                   << m_cur_logframeofs << " "
                   << gdi_type << " "
                   << m_cur_datasize << " "
                   << "\"" << m_cur_bin_name << "\" "
                   << 0 << "\n";
            m_cur_logframeofs += m_cur_toc_frames;
        }
        else
        {
            // In single-bin mode, only emit FILE once (first track)
            if (m_split_bin || m_cur_track == 1)
                m_meta << "FILE \"" << m_cur_bin_name << "\" BINARY\n";
            char tnum[8];
            std::snprintf(tnum, sizeof(tnum), "%02u", m_cur_track);
            m_meta << "  TRACK " << tnum << " "
                   << cue_track_type_string(m_cur_type, m_cur_datasize) << "\n";

            // For single-bin: INDEX offsets are cumulative frames from start of file
            // For split-bin: each file starts at offset 0
            uint32_t base_frames = m_split_bin ? 0
                : static_cast<uint32_t>(m_cur_track_byte_ofs / m_cur_datasize);

            if (m_cur_pregap > 0 && m_cur_pgdatasize == 0)
            {
                m_meta << "    PREGAP " << msf_from_frames(m_cur_pregap) << "\n";
                m_meta << "    INDEX 01 " << msf_from_frames(base_frames) << "\n";
            }
            else if (m_cur_pregap > 0 && m_cur_pgdatasize > 0)
            {
                m_meta << "    INDEX 00 " << msf_from_frames(base_frames) << "\n";
                m_meta << "    INDEX 01 " << msf_from_frames(base_frames + m_cur_pregap) << "\n";
            }
            else
            {
                m_meta << "    INDEX 01 " << msf_from_frames(base_frames) << "\n";
            }
            if (m_cur_postgap > 0)
                m_meta << "    POSTGAP " << msf_from_frames(m_cur_postgap) << "\n";
        }
    }

    void on_complete() override {}

    const std::string& error() const { return m_error; }
    const std::string& metadata() const { m_meta_str = m_meta.str(); return m_meta_str; }
    const std::vector<std::string>& output_files() const { return m_output_files; }
    uint64_t bytes_written() const { return m_bytes_written; }

private:
    std::string m_out_dir;
    std::string m_stem;
    bool m_gdi_mode;
    bool m_split_bin;
    std::string m_bin_template;
    uint32_t m_num_tracks = 0;

    uint32_t m_cur_track = 0;
    TrackType m_cur_type = TrackType::Mode1;
    bool m_cur_audio = false;
    uint32_t m_cur_datasize = 0;
    uint32_t m_cur_toc_frames = 0;
    uint32_t m_cur_pregap = 0;
    uint32_t m_cur_pgdatasize = 0;
    uint32_t m_cur_postgap = 0;
    uint32_t m_track_frames = 0;
    uint32_t m_cur_logframeofs = 0;
    std::string m_cur_bin_name;
    std::ofstream m_cur_file;
    std::string m_error;

    std::ostringstream m_meta;
    mutable std::string m_meta_str;
    std::vector<std::string> m_output_files;
    uint64_t m_bytes_written = 0;
    uint64_t m_cumulative_bytes = 0;
    uint64_t m_cur_track_byte_ofs = 0;
};

// ======================> File-writing sink for raw/DVD/HD

class RawFileSink : public ProcessorSink
{
public:
    RawFileSink(const std::string& out_path) : m_out_path(out_path) {}

    void on_track_begin(uint32_t, TrackType, bool, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t) override
    {
        m_file.open(m_out_path, std::ios::binary);
        if (!m_file)
            m_error = "Cannot create output file: " + m_out_path;
    }

    void on_data(const void* data, uint32_t len) override
    {
        if (m_file)
        {
            m_file.write(reinterpret_cast<const char*>(data), len);
            m_bytes_written += len;
        }
    }

    void on_track_end() override { m_file.close(); }

    const std::string& error() const { return m_error; }
    uint64_t bytes_written() const { return m_bytes_written; }

private:
    std::string m_out_path;
    std::ofstream m_file;
    std::string m_error;
    uint64_t m_bytes_written = 0;
};

// ======================> ChdExtractor

struct ChdExtractor::Impl {};

ChdExtractor::ChdExtractor() : m_impl(std::make_unique<Impl>()) {}
ChdExtractor::~ChdExtractor() = default;
ChdExtractor::ChdExtractor(ChdExtractor&&) noexcept = default;
ChdExtractor& ChdExtractor::operator=(ChdExtractor&&) noexcept = default;

ExtractionResult ChdExtractor::extract(const std::string& chd_path,
                                       const ExtractOptions& options)
{
    auto result = extract_impl(chd_path, options.parent_chd_path, options);
    if (!result.success) {
        if (options.log_callback) options.log_callback(LogLevel::Error, result.error_message);
        throw ChdException(result.error_message);
    }
    return result;
}

ExtractionResult ChdExtractor::extract(const std::string& chd_path,
                                       const std::string& parent_chd_path,
                                       const ExtractOptions& options)
{
    auto result = extract_impl(chd_path, parent_chd_path, options);
    if (!result.success) {
        if (options.log_callback) options.log_callback(LogLevel::Error, result.error_message);
        throw ChdException(result.error_message);
    }
    return result;
}

ExtractionResult ChdExtractor::extract_impl(const std::string& chd_path,
                                       const std::string& parent_chd_path,
                                       const ExtractOptions& options)
{
    ExtractionResult result = {};
    result.success = false;

    std::string out_dir = options.output_dir.empty() ? path_dir(chd_path) : options.output_dir;
    std::string stem = path_stem(chd_path);

    // Quick open/detect/close to determine content type for sink selection.
    ContentType type;
    bool is_gdrom = false;
    {
        chd_file chd;
        auto err = chd.open(chd_path, false);
        if (err)
        {
            result.error_message = "Failed to open CHD: " + err.message();
            return result;
        }
        if (!chd.check_is_gd())        { type = ContentType::GDROM; is_gdrom = true; }
        else if (!chd.check_is_cd())    type = ContentType::CDROM;
        else if (!chd.check_is_dvd())   type = ContentType::DVD;
        else if (!chd.check_is_av())    type = ContentType::LaserDisc;
        else if (!chd.check_is_hd())    type = ContentType::HardDisk;
        else                            type = ContentType::Raw;
    }
    result.detected_type = type;

    bool use_cd = (type == ContentType::CDROM || type == ContentType::GDROM) && !options.force_raw;

    if (use_cd)
    {
        bool gdi_mode;
        std::string meta_ext;
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
            ? (stem + meta_ext) : options.output_filename;
        std::string meta_path = path_join(out_dir, meta_filename);

        // GDI and GD-ROM CUE always force split
        bool split = options.split_bin || gdi_mode;
        CdFileSink sink(out_dir, stem, gdi_mode, split, options.output_bin);
        std::vector<ProcessorSink*> sinks = { &sink };

        auto proc_result = ChdProcessor::process(chd_path, sinks, options.progress_callback);

        if (!proc_result.success)
        {
            result.error_message = proc_result.error_message;
            return result;
        }
        if (!sink.error().empty())
        {
            result.error_message = sink.error();
            return result;
        }

        // Write metadata file
        {
            std::ofstream meta_out(meta_path);
            if (!meta_out)
            {
                if (m_strict)
                    throw ChdMetadataException("Cannot create metadata file '" + meta_path + "': check output directory permissions");
                // Non-strict: log warning, return partial success with BIN data only
                if (options.log_callback)
                    options.log_callback(LogLevel::Warning, "Cannot create metadata file '" + meta_path + "' — BIN data extracted but .cue/.gdi not written");
                result.success = true;
                result.output_files = sink.output_files();
                result.bytes_written = sink.bytes_written();
                result.detected_type = proc_result.content_type;
                return result;
            }
            meta_out << sink.metadata();
        }

        result.success = true;
        result.output_path = meta_path;
        result.output_files = sink.output_files();
        result.output_files.insert(result.output_files.begin(), meta_path);
        result.bytes_written = sink.bytes_written();
        result.detected_type = proc_result.content_type;
    }
    else
    {
        std::string ext = (type == ContentType::DVD) ? ".iso" : ".bin";
        std::string out_name = options.output_filename.empty()
            ? (stem + ext) : options.output_filename;
        std::string out_path = path_join(out_dir, out_name);

        RawFileSink sink(out_path);
        std::vector<ProcessorSink*> sinks = { &sink };

        auto proc_result = ChdProcessor::process(chd_path, sinks, options.progress_callback);

        if (!proc_result.success)
        {
            result.error_message = proc_result.error_message;
            return result;
        }
        if (!sink.error().empty())
        {
            result.error_message = sink.error();
            return result;
        }

        result.success = true;
        result.output_path = out_path;
        result.output_files.push_back(out_path);
        result.bytes_written = sink.bytes_written();
        result.detected_type = proc_result.content_type;
    }

    return result;
}

} // namespace chdlite
