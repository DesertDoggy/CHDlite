// license:GPLv3
// CHDlite - Cross-platform CHD library
// chdlite_c.cpp - C wrapper implementation

#ifndef CHDLITE_FFI_BUILD
#define CHDLITE_FFI_BUILD
#endif

#include "chdlite_c.h"
#include "chd_api.hpp"

#include <atomic>
#include <cctype>
#include <filesystem>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

// ========================================================================
// Internal state
// ========================================================================

static chdlite_progress_fn g_progress_cb = nullptr;
static chdlite_log_fn      g_log_cb      = nullptr;
static std::atomic<bool>   g_cancel{false};
static std::mutex          g_mutex;       // one operation at a time

// Helpers -----------------------------------------------------------------

static char* dup_str(const std::string& s)
{
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) {
        std::memcpy(p, s.c_str(), s.size() + 1);
    }
    return p;
}

static std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// Format a uint64 with comma thousands separators (e.g. 1,234,567)
static std::string big_int_string(uint64_t v)
{
    std::string s = std::to_string(v);
    int ins = static_cast<int>(s.size()) - 3;
    while (ins > 0) { s.insert(ins, ","); ins -= 3; }
    return s;
}

static char* json_error(const std::string& msg)
{
    return dup_str("{\"success\":false,\"error\":\"" + json_escape(msg) + "\"}");
}

/// Build the progress callback for C++ API from the C callback.
static std::function<bool(uint64_t, uint64_t)> make_progress()
{
    if (!g_progress_cb && !g_cancel.load())
        return nullptr;

    return [](uint64_t current, uint64_t total) -> bool {
        if (g_cancel.load()) return false;
        if (g_progress_cb) return g_progress_cb(current, total) != 0;
        return true;
    };
}

/// Build the log callback for C++ API from the C callback.
static chdlite::LogCallback make_log()
{
    if (!g_log_cb) return nullptr;
    return [](chdlite::LogLevel level, const std::string& msg) {
        g_log_cb(static_cast<int>(level), msg.c_str());
    };
}

static const char* codec_name(chdlite::Codec c)
{
    using C = chdlite::Codec;
    switch (c) {
        case C::None:     return "none";
        case C::Zlib:     return "zlib";
        case C::ZlibPlus: return "zlib+";
        case C::Zstd:     return "zstd";
        case C::LZMA:     return "lzma";
        case C::FLAC:     return "flac";
        case C::Huffman:  return "huffman";
        case C::AVHUFF:   return "avhuff";
        case C::CD_Zlib:  return "cdzl";
        case C::CD_Zstd:  return "cdzs";
        case C::CD_LZMA:  return "cdlz";
        case C::CD_FLAC:  return "cdfl";
    }
    return "unknown";
}

static const char* track_type_str(chdlite::TrackType tt)
{
    using T = chdlite::TrackType;
    switch (tt) {
        case T::Mode1:        return "MODE1/2048";
        case T::Mode1Raw:     return "MODE1/2352";
        case T::Mode2:        return "MODE2/2336";
        case T::Mode2Form1:   return "MODE2_FORM1";
        case T::Mode2Form2:   return "MODE2_FORM2";
        case T::Mode2FormMix: return "MODE2_MIX";
        case T::Mode2Raw:     return "MODE2/2352";
        case T::Audio:        return "AUDIO";
        default:              return "unknown";
    }
}

static const char* content_type_name(chdlite::ContentType t)
{
    using T = chdlite::ContentType;
    switch (t) {
        case T::Unknown:   return "unknown";
        case T::HardDisk:  return "harddisk";
        case T::CDROM:     return "cdrom";
        case T::GDROM:     return "gdrom";
        case T::DVD:       return "dvd";
        case T::LaserDisc: return "laserdisc";
        case T::Raw:       return "raw";
    }
    return "unknown";
}

// If input is a .bin that is referenced by a sibling .cue/.gdi,
// prefer the sheet path for archive creation (CLI-compatible behavior).
static std::string resolve_sheet_for_bin(const std::string& input_path)
{
    namespace fs = std::filesystem;

    fs::path p(input_path);
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".bin")
        return input_path;

    std::string bin_name = p.filename().string();
    std::string bin_name_lower = bin_name;
    for (auto& c : bin_name_lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::error_code ec;
    fs::path dir = p.parent_path();
    if (!fs::exists(dir, ec) || ec)
        return input_path;

    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        std::string sext = e.path().extension().string();
        for (auto& c : sext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (sext != ".cue" && sext != ".gdi") continue;

        std::ifstream sf(e.path().string());
        if (!sf.good()) continue;

        std::string line;
        while (std::getline(sf, line)) {
            std::string lower = line;
            for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find(bin_name_lower) != std::string::npos)
                return e.path().string();
        }
    }

    return input_path;
}

static chdlite::Codec parse_codec(const std::string& name)
{
    using C = chdlite::Codec;
    if (name == "zstd")    return C::Zstd;
    if (name == "zlib")    return C::Zlib;
    if (name == "lzma")    return C::LZMA;
    if (name == "flac")    return C::FLAC;
    if (name == "cdzs")    return C::CD_Zstd;
    if (name == "cdzl")    return C::CD_Zlib;
    if (name == "cdlz")    return C::CD_LZMA;
    if (name == "cdfl")    return C::CD_FLAC;
    if (name == "huffman") return C::Huffman;
    if (name == "avhuff")  return C::AVHUFF;
    return C::None;
}

static std::string trim_copy(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static std::string lower_copy(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string normalize_codec_arg(std::string s)
{
    s = lower_copy(trim_copy(s));
    while (!s.empty() && s.front() == '-')
        s.erase(s.begin());
    return s;
}

static void clear_codecs(std::array<chdlite::Codec, 4>& codecs)
{
    codecs.fill(chdlite::Codec::None);
}

static bool parse_codec_list(const std::string& input, std::array<chdlite::Codec, 4>& codecs)
{
    clear_codecs(codecs);

    std::istringstream ss(input);
    std::string token;
    int index = 0;
    while (std::getline(ss, token, ',')) {
        token = lower_copy(trim_copy(token));
        if (token.empty())
            continue;
        if (token == "huff")
            token = "huffman";
        chdlite::Codec parsed = parse_codec(token);
        if (parsed == chdlite::Codec::None)
            return false;
        if (index >= 4)
            return false;
        codecs[index++] = parsed;
    }

    return codecs[0] != chdlite::Codec::None;
}

static std::string codec_list_string(const std::array<chdlite::Codec, 4>& codecs)
{
    std::ostringstream out;
    bool first = true;
    for (chdlite::Codec codec : codecs) {
        if (codec == chdlite::Codec::None)
            continue;
        if (!first)
            out << ", ";
        out << codec_name(codec);
        first = false;
    }
    return first ? "none" : out.str();
}

static std::string detect_archive_format(const std::string& input_path, const chdlite::DetectionResult& detection)
{
    namespace fs = std::filesystem;

    std::string ext = lower_copy(fs::path(input_path).extension().string());
    if (ext == ".cue" || ext == ".gdi" || ext == ".toc" || ext == ".nrg")
        return "cd";
    if (ext == ".iso" || ext == ".bin" || ext == ".img")
        return detection.format;
    return "raw";
}

static std::array<chdlite::Codec, 4> default_codecs_for(const std::string& format)
{
    using C = chdlite::Codec;
    if (format == "cd" || format == "gd")
        return { C::CD_LZMA, C::CD_Zlib, C::CD_FLAC, C::None };
    return { C::LZMA, C::Zlib, C::Huffman, C::FLAC };
}

static std::array<chdlite::Codec, 4> best_codecs_for(const std::string& format)
{
    using C = chdlite::Codec;
    if (format == "cd" || format == "gd")
        return { C::CD_LZMA, C::CD_Zstd, C::CD_Zlib, C::CD_FLAC };
    if (format == "dvd")
        return { C::Zstd, C::LZMA, C::Zlib, C::Huffman };
    return default_codecs_for(format);
}

static std::array<chdlite::Codec, 4> smart_codecs_for(chdlite::GamePlatform platform,
                                                       const std::string& format)
{
    using C = chdlite::Codec;
    if (platform == chdlite::GamePlatform::PS2) {
        if (format == "dvd")
            return { C::Zlib, C::None, C::None, C::None };
        return { C::CD_Zlib, C::CD_FLAC, C::None, C::None };
    }
    if (format == "dvd")
        return { C::Zstd, C::None, C::None, C::None };
    if (format == "cd" || format == "gd")
        return { C::CD_Zstd, C::CD_FLAC, C::None, C::None };
    return default_codecs_for(format);
}

struct CompressionSummary {
    std::string mode;
    std::string media;
    std::string platform;
    std::array<chdlite::Codec, 4> codecs;
    bool show_detection = false;
};

static CompressionSummary build_compression_summary(const std::string& input_path,
                                                    const chdlite::ArchiveOptions& options)
{
    CompressionSummary summary;
    clear_codecs(summary.codecs);

    bool need_detection = !options.best && !options.chdman_compat && !options.has_custom_compression();
    chdlite::DetectionResult detection = need_detection
        ? chdlite::detect_input(input_path, false)
        : chdlite::DetectionResult{};

    std::string format = detect_archive_format(input_path, detection);
    std::string smart_format = lower_copy(std::filesystem::path(input_path).extension().string()) == ".gdi"
        ? "gd"
        : format;

    if (options.best) {
        summary.mode = "--best";
        summary.codecs = best_codecs_for(smart_format);
    } else if (options.chdman_compat) {
        summary.mode = "--chdman";
        summary.codecs = default_codecs_for(smart_format);
    } else if (options.has_custom_compression()) {
        summary.mode = "custom";
        for (int i = 0; i < 4; i++)
            summary.codecs[i] = options.compression[i];
    } else {
        summary.mode = "auto";
        summary.show_detection = true;
        summary.media = smart_format == "gd" ? "gdrom" : format;
        summary.platform = chdlite::game_platform_name(detection.game_platform);
        summary.codecs = smart_codecs_for(detection.game_platform, smart_format);
    }

    return summary;
}

static chdlite::HashFlags parse_hash_flags(const char* algorithms)
{
    if (!algorithms || !*algorithms)
        return chdlite::HashFlags::SHA1;

    uint32_t flags = 0;
    std::istringstream ss(algorithms);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
        while (!token.empty() && token.back() == ' ') token.pop_back();

        if (token == "sha1")   flags |= static_cast<uint32_t>(chdlite::HashFlags::SHA1);
        if (token == "md5")    flags |= static_cast<uint32_t>(chdlite::HashFlags::MD5);
        if (token == "crc32")  flags |= static_cast<uint32_t>(chdlite::HashFlags::CRC32);
        if (token == "sha256") flags |= static_cast<uint32_t>(chdlite::HashFlags::SHA256);
        if (token == "xxh3")   flags |= static_cast<uint32_t>(chdlite::HashFlags::XXH3_128);
        if (token == "all")    flags  = static_cast<uint32_t>(chdlite::HashFlags::All);
    }
    return static_cast<chdlite::HashFlags>(flags ? flags : static_cast<uint32_t>(chdlite::HashFlags::SHA1));
}

// ========================================================================
// Public API
// ========================================================================

extern "C" {

CHDLITE_API void chdlite_set_progress_callback(chdlite_progress_fn cb)
{
    g_progress_cb = cb;
}

CHDLITE_API void chdlite_set_log_callback(chdlite_log_fn cb)
{
    g_log_cb = cb;
}

CHDLITE_API void chdlite_cancel(void)
{
    g_cancel.store(true);
}

// -----------------------------------------------------------------------
// READ
// -----------------------------------------------------------------------
CHDLITE_API char* chdlite_read(const char* chd_path)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cancel.store(false);

    try {
        if (!chdlite::ChdReader::is_chd_file(chd_path)) {
            namespace fs = std::filesystem;
            auto det = chdlite::detect_input(chd_path, true);

            uint64_t file_size = 0;
            std::error_code ec;
            if (fs::exists(chd_path, ec)) {
                file_size = static_cast<uint64_t>(fs::file_size(chd_path, ec));
                if (ec) file_size = 0;
            }

            const char* detected_type = "raw";
            if (det.format == "cd") detected_type = "cdrom";
            else if (det.format == "dvd") detected_type = "dvd";

            // Build CLI-style formatted output
            std::ostringstream fmt;
            fmt << "File size:    " << big_int_string(file_size) << " bytes\n";
            if (det.format == "cd")  fmt << "Format:       CD-ROM\n";
            else if (det.format == "dvd") fmt << "Format:       DVD\n";
            if (det.game_platform != chdlite::GamePlatform::Unknown) {
                fmt << "Platform:     " << chdlite::game_platform_name(det.game_platform) << "\n";
                fmt << "Title:        " << (det.title.empty() ? "N/A" : det.title) << "\n";
                fmt << "Manufacturer ID: " << (det.manufacturer_id.empty() ? "N/A" : det.manufacturer_id) << "\n";
            }
            if (!det.detection_source.empty())
                fmt << "Detection:    " << det.detection_source << "\n";

            std::ostringstream js;
            js << "{\"success\":true"
               << ",\"version\":0"
               << ",\"logical_bytes\":" << file_size
               << ",\"content_type\":\"" << detected_type << "\""
               << ",\"platform\":\"" << json_escape(chdlite::game_platform_name(det.game_platform)) << "\""
               << ",\"title\":\"" << json_escape(det.title) << "\""
               << ",\"manufacturer_id\":\"" << json_escape(det.manufacturer_id) << "\""
               << ",\"detection_source\":\"" << json_escape(det.detection_source) << "\""
               << ",\"formatted\":\"" << json_escape(fmt.str()) << "\""
               << "}";

            return dup_str(js.str());
        }

        chdlite::ChdReader reader;
        reader.open(chd_path);
        auto hdr = reader.read_header();

        // Platform detection
        auto det = reader.detect_game_platform(chdlite::DetectFlags::All, true);

        // Track list (CD/GD only, matching CLI behaviour)
        std::vector<chdlite::TrackInfo> tracks;
        if (hdr.content_type == chdlite::ContentType::CDROM ||
            hdr.content_type == chdlite::ContentType::GDROM)
        {
            tracks = reader.get_tracks();
        }

        // Build CLI-style formatted output
        std::ostringstream fmt;
        fmt << "File Version: " << hdr.version << "\n";
        fmt << "Logical size: " << big_int_string(hdr.logical_bytes) << " bytes\n";
        fmt << "Hunk Size:    " << big_int_string(hdr.hunk_bytes) << " bytes\n";
        fmt << "Total Hunks:  " << big_int_string(hdr.hunk_count) << "\n";
        fmt << "Unit Size:    " << big_int_string(hdr.unit_bytes) << " bytes\n";
        // Codecs
        {
            std::string codec_list;
            for (int i = 0; i < 4; ++i) {
                if (hdr.codecs[i] == chdlite::Codec::None) continue;
                if (!codec_list.empty()) codec_list += ", ";
                codec_list += codec_name(hdr.codecs[i]);
            }
            fmt << "Compression:  " << (codec_list.empty() ? "none" : codec_list) << "\n";
        }
        fmt << "Compressed:   " << (hdr.compressed ? "yes" : "no") << "\n";
        fmt << "Has Parent:   " << (hdr.has_parent ? "yes" : "no") << "\n";
        fmt << "Content Type: " << content_type_name(hdr.content_type) << "\n";
        if (!hdr.sha1.empty())        fmt << "SHA-1:        " << hdr.sha1 << "\n";
        if (!hdr.raw_sha1.empty())    fmt << "Data SHA-1:   " << hdr.raw_sha1 << "\n";
        if (!hdr.parent_sha1.empty()) fmt << "Parent SHA-1: " << hdr.parent_sha1 << "\n";
        if (!tracks.empty()) {
            fmt << "Tracks:       " << tracks.size();
            if (hdr.is_gdrom) fmt << "  (GD-ROM)";
            fmt << "\n";
            for (auto& t : tracks) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                    "  Track %02u: %-14s  frames=%u  data=%u sub=%u%s\n",
                    t.track_number,
                    t.is_audio ? "AUDIO" : track_type_str(t.type),
                    t.frames, t.data_size, t.sub_size,
                    t.is_audio ? " [AUDIO]" : "");
                fmt << buf;
            }
        }
        if (det.game_platform != chdlite::GamePlatform::Unknown) {
            fmt << "Platform:     " << chdlite::game_platform_name(det.game_platform) << "\n";
            fmt << "Title:        " << (det.title.empty() ? "N/A" : det.title) << "\n";
            fmt << "Manufacturer ID: " << (det.manufacturer_id.empty() ? "N/A" : det.manufacturer_id) << "\n";
        }
        if (!det.detection_source.empty())
            fmt << "Detection:    " << det.detection_source << "\n";

        std::ostringstream js;
        js << "{\"success\":true"
           << ",\"version\":" << hdr.version
           << ",\"logical_bytes\":" << hdr.logical_bytes
           << ",\"hunk_bytes\":" << hdr.hunk_bytes
           << ",\"hunk_count\":" << hdr.hunk_count
           << ",\"unit_bytes\":" << hdr.unit_bytes
           << ",\"unit_count\":" << hdr.unit_count
           << ",\"compressed\":" << (hdr.compressed ? "true" : "false")
           << ",\"has_parent\":" << (hdr.has_parent ? "true" : "false")
           << ",\"content_type\":\"" << content_type_name(hdr.content_type) << "\""
           << ",\"codecs\":[";
        for (int i = 0; i < 4; ++i) {
            if (i) js << ",";
            js << "\"" << codec_name(hdr.codecs[i]) << "\"";
        }
        js << "]"
           << ",\"sha1\":\"" << json_escape(hdr.sha1) << "\""
           << ",\"raw_sha1\":\"" << json_escape(hdr.raw_sha1) << "\""
           << ",\"parent_sha1\":\"" << json_escape(hdr.parent_sha1) << "\""
           << ",\"num_tracks\":" << hdr.num_tracks
           << ",\"is_gdrom\":" << (hdr.is_gdrom ? "true" : "false")
           << ",\"platform\":\"" << json_escape(chdlite::game_platform_name(det.game_platform)) << "\""
           << ",\"title\":\"" << json_escape(det.title) << "\""
           << ",\"manufacturer_id\":\"" << json_escape(det.manufacturer_id) << "\""
           << ",\"detection_source\":\"" << json_escape(det.detection_source) << "\""
           << ",\"formatted\":\"" << json_escape(fmt.str()) << "\""
           << "}";
        return dup_str(js.str());
    }
    catch (const chdlite::ChdCancelledException&) {
        return json_error("Cancelled");
    }
    catch (const std::exception& e) {
        return json_error(e.what());
    }
}

// -----------------------------------------------------------------------
// HASH
// -----------------------------------------------------------------------
CHDLITE_API char* chdlite_hash(const char* chd_path, const char* algorithms)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cancel.store(false);

    try {
        chdlite::ChdReader reader;
        reader.open(chd_path);

        chdlite::HashFlags flags = parse_hash_flags(algorithms);
        auto result = reader.hash_content(flags);

        if (!result.success)
            return json_error(result.error_message);

        // Use format_hash for a readable output, then wrap in JSON
        std::string text = chdlite::ChdReader::format_hash(
            result, chdlite::HashOutputFormat::Hashes, "");

        std::ostringstream js;
        js << "{\"success\":true"
           << ",\"content_type\":\"" << content_type_name(result.content_type) << "\""
           << ",\"chd_sha1\":\"" << json_escape(result.chd_sha1) << "\""
           << ",\"chd_raw_sha1\":\"" << json_escape(result.chd_raw_sha1) << "\""
           << ",\"tracks\":[";
        for (size_t i = 0; i < result.tracks.size(); ++i) {
            auto& t = result.tracks[i];
            if (i) js << ",";
            js << "{\"track\":" << t.track_number
               << ",\"type\":\"" << (t.is_audio ? "audio" : "data") << "\""
               << ",\"bytes\":" << t.data_bytes;
            if (!t.sha1.hex_string.empty())   js << ",\"sha1\":\"" << json_escape(t.sha1.hex_string) << "\"";
            if (!t.md5.hex_string.empty())    js << ",\"md5\":\"" << json_escape(t.md5.hex_string) << "\"";
            if (!t.crc32.hex_string.empty())  js << ",\"crc32\":\"" << json_escape(t.crc32.hex_string) << "\"";
            if (!t.sha256.hex_string.empty()) js << ",\"sha256\":\"" << json_escape(t.sha256.hex_string) << "\"";
            if (!t.xxh3_128.hex_string.empty()) js << ",\"xxh3\":\"" << json_escape(t.xxh3_128.hex_string) << "\"";
            js << "}";
        }
        js << "]"
           << ",\"formatted\":\"" << json_escape(text) << "\""
           << "}";

        return dup_str(js.str());
    }
    catch (const chdlite::ChdCancelledException&) {
        return json_error("Cancelled");
    }
    catch (const std::exception& e) {
        return json_error(e.what());
    }
}

// -----------------------------------------------------------------------
// EXTRACT
// -----------------------------------------------------------------------
CHDLITE_API char* chdlite_extract(const char* chd_path,
                                  const char* output_dir,
                                  int split_bin)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cancel.store(false);

    try {
        if (!chdlite::ChdReader::is_chd_file(chd_path)) {
            chdlite::ArchiveOptions aopts;
            aopts.progress_callback = make_progress();
            aopts.log_callback = make_log();

            std::string out;
            if (output_dir && *output_dir) {
                out = output_dir;
            } else {
                out = chd_path;
                auto dot = out.rfind('.');
                if (dot != std::string::npos) out = out.substr(0, dot);
                out += ".chd";
            }

            chdlite::ChdArchiver archiver;
            auto ares = archiver.archive(chd_path, out, aopts);
            if (!ares.success)
                return json_error(ares.error_message);

            std::ostringstream js;
            js << "{\"success\":true"
               << ",\"output_path\":\"" << json_escape(ares.output_path) << "\""
               << ",\"bytes_written\":" << ares.output_bytes
               << ",\"detected_type\":\"create\""
               << ",\"files\":[\"" << json_escape(ares.output_path) << "\"]"
               << "}";
            return dup_str(js.str());
        }

        chdlite::ExtractOptions opts;
        if (output_dir && *output_dir) opts.output_dir = output_dir;
        opts.split_bin = (split_bin != 0);
        opts.progress_callback = make_progress();
        opts.log_callback = make_log();

        chdlite::ChdExtractor extractor;
        auto result = extractor.extract(chd_path, opts);

        if (!result.success)
            return json_error(result.error_message);

        std::ostringstream js;
        js << "{\"success\":true"
           << ",\"output_path\":\"" << json_escape(result.output_path) << "\""
           << ",\"bytes_written\":" << result.bytes_written
           << ",\"detected_type\":\"" << content_type_name(result.detected_type) << "\""
           << ",\"files\":[";
        for (size_t i = 0; i < result.output_files.size(); ++i) {
            if (i) js << ",";
            js << "\"" << json_escape(result.output_files[i]) << "\"";
        }
        js << "]}";

        return dup_str(js.str());
    }
    catch (const chdlite::ChdCancelledException&) {
        return json_error("Cancelled");
    }
    catch (const std::exception& e) {
        return json_error(e.what());
    }
}

// -----------------------------------------------------------------------
// COMPRESS
// -----------------------------------------------------------------------
CHDLITE_API char* chdlite_compress(const char* input_path,
                                   const char* output_path,
                                   const char* codec,
                                   int hunk_size,
                                   int unit_size,
                                   int threads)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cancel.store(false);

    try {
        std::string in = input_path ? input_path : "";
        in = resolve_sheet_for_bin(in);

        // If the input is already a CHD, treat as extract instead of create.
        if (chdlite::ChdReader::is_chd_file(in)) {
            chdlite::ExtractOptions eopts;
            if (output_path && *output_path) eopts.output_dir = output_path;
            eopts.split_bin = false;
            eopts.progress_callback = make_progress();
            eopts.log_callback = make_log();

            chdlite::ChdExtractor extractor;
            auto eres = extractor.extract(in, eopts);
            if (!eres.success)
                return json_error(eres.error_message);

            std::ostringstream efmt;
            efmt << "Operation:    Extract (CHD input)\n";
            efmt << "Output:       " << eres.output_path << "\n";
            efmt << "Written:      " << big_int_string(eres.bytes_written) << " bytes\n";
            efmt << "Type:         " << content_type_name(eres.detected_type) << "\n";
            for (auto& f : eres.output_files)
                efmt << "  " << f << "\n";

            std::ostringstream ejs;
            ejs << "{\"success\":true"
                << ",\"operation\":\"extract\""
                << ",\"output_path\":\"" << json_escape(eres.output_path) << "\""
                << ",\"bytes_written\":" << eres.bytes_written
                << ",\"detected_type\":\"" << content_type_name(eres.detected_type) << "\""
                << ",\"codec_used\":\"extract\""
                << ",\"formatted\":\"" << json_escape(efmt.str()) << "\""
                << ",\"files\":[";
            for (size_t i = 0; i < eres.output_files.size(); ++i) {
                if (i) ejs << ",";
                ejs << "\"" << json_escape(eres.output_files[i]) << "\"";
            }
            ejs << "]}";
            return dup_str(ejs.str());
        }

        chdlite::ArchiveOptions opts;
        std::string codec_arg = codec ? normalize_codec_arg(codec) : "";
        std::array<chdlite::Codec, 4> custom_codecs{};
        clear_codecs(custom_codecs);

        if (!codec_arg.empty() && codec_arg != "auto") {
            if (codec_arg == "best") {
                opts.best = true;
            } else if (codec_arg == "chdman") {
                opts.chdman_compat = true;
            } else if (codec_arg.find(',') != std::string::npos) {
                if (!parse_codec_list(codec_arg, custom_codecs))
                    return json_error("Invalid compression list. Use up to 4 codecs separated by commas.");
                for (int i = 0; i < 4; i++)
                    opts.compression[i] = custom_codecs[i];
            } else {
                chdlite::Codec parsed = parse_codec(codec_arg);
                if (parsed == chdlite::Codec::None)
                    return json_error("Unknown compression mode or codec");
                opts.codec = parsed;
            }
        }
        if (hunk_size > 0) opts.hunk_bytes = static_cast<uint32_t>(hunk_size);
        if (unit_size > 0) opts.unit_bytes = static_cast<uint32_t>(unit_size);
        if (threads > 0)   opts.num_processors = threads;
        opts.progress_callback = make_progress();
        opts.log_callback = make_log();

        CompressionSummary summary = build_compression_summary(in, opts);

        std::string out;
        if (output_path && *output_path) {
            out = output_path;
        } else {
            // Default: same name with .chd extension
            out = in;
            auto dot = out.rfind('.');
            if (dot != std::string::npos) out = out.substr(0, dot);
            out += ".chd";
        }

        chdlite::ChdArchiver archiver;
        auto result = archiver.archive(in, out, opts);

        if (!result.success)
            return json_error(result.error_message);

        std::ostringstream formatted;
        formatted << "Output:       " << result.output_path << "\n";
        if (summary.mode == "--best")
            formatted << "Mode:         Create (--best)\n";
        else if (summary.mode == "--chdman")
            formatted << "Mode:         Create (--chdman)\n";
        else if (summary.mode == "custom")
            formatted << "Mode:         Create (custom codecs)\n";
        else
            formatted << "Mode:         Create (auto)\n";
        if (summary.show_detection) {
            formatted << "Detected Media:    " << summary.media << "\n";
            formatted << "Detected Platform: " << summary.platform << "\n";
        }
        formatted << "Selected Codecs:   " << codec_list_string(summary.codecs) << "\n";
        formatted << "Codec Used:        " << codec_name(result.codec_used) << "\n";
        formatted << "Ratio:             " << result.compression_ratio;

        std::ostringstream js;
        js << "{\"success\":true"
           << ",\"output_path\":\"" << json_escape(result.output_path) << "\""
           << ",\"input_bytes\":" << result.input_bytes
           << ",\"output_bytes\":" << result.output_bytes
           << ",\"compression_ratio\":" << result.compression_ratio
           << ",\"codec_used\":\"" << codec_name(result.codec_used) << "\""
           << ",\"compression_mode\":\"" << json_escape(summary.mode) << "\""
           << ",\"selected_codecs\":\"" << json_escape(codec_list_string(summary.codecs)) << "\""
           << ",\"detected_media\":\"" << json_escape(summary.media) << "\""
           << ",\"detected_platform\":\"" << json_escape(summary.platform) << "\""
           << ",\"formatted\":\"" << json_escape(formatted.str()) << "\""
           << "}";

        return dup_str(js.str());
    }
    catch (const chdlite::ChdCancelledException&) {
        return json_error("Cancelled");
    }
    catch (const std::exception& e) {
        return json_error(e.what());
    }
}

// -----------------------------------------------------------------------
// Housekeeping
// -----------------------------------------------------------------------
CHDLITE_API void chdlite_free(char* ptr)
{
    std::free(ptr);
}

CHDLITE_API const char* chdlite_version(void)
{
    static const char ver[] = "0.2.1";
    return ver;
}

} // extern "C"
