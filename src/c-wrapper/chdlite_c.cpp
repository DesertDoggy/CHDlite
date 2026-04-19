// license:GPLv3
// CHDlite - Cross-platform CHD library
// chdlite_c.cpp - C wrapper implementation

#define CHDLITE_FFI_BUILD

#include "chdlite_c.h"
#include "chd_api.hpp"

#include <atomic>
#include <filesystem>
#include <cstdlib>
#include <cstring>
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

            std::ostringstream js;
            js << "{\"success\":true"
               << ",\"version\":0"
               << ",\"logical_bytes\":" << file_size
               << ",\"hunk_bytes\":0"
               << ",\"hunk_count\":0"
               << ",\"unit_bytes\":0"
               << ",\"unit_count\":0"
               << ",\"compressed\":false"
               << ",\"has_parent\":false"
               << ",\"content_type\":\"" << detected_type << "\""
               << ",\"codecs\":[]"
               << ",\"sha1\":\"\""
               << ",\"raw_sha1\":\"\""
               << ",\"parent_sha1\":\"\""
               << ",\"num_tracks\":0"
               << ",\"is_gdrom\":false"
               << ",\"platform\":\"" << json_escape(chdlite::game_platform_name(det.game_platform)) << "\""
               << ",\"title\":\"" << json_escape(det.title) << "\""
               << ",\"manufacturer_id\":\"" << json_escape(det.manufacturer_id) << "\""
               << "}";

            return dup_str(js.str());
        }

        chdlite::ChdReader reader;
        reader.open(chd_path);
        auto hdr = reader.read_header();

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
        chdlite::ArchiveOptions opts;
        if (codec && *codec) {
            opts.codec = parse_codec(codec);
        }
        if (hunk_size > 0) opts.hunk_bytes = static_cast<uint32_t>(hunk_size);
        if (unit_size > 0) opts.unit_bytes = static_cast<uint32_t>(unit_size);
        if (threads > 0)   opts.num_processors = threads;
        opts.progress_callback = make_progress();
        opts.log_callback = make_log();

        std::string out;
        if (output_path && *output_path) {
            out = output_path;
        } else {
            // Default: same name with .chd extension
            out = input_path;
            auto dot = out.rfind('.');
            if (dot != std::string::npos) out = out.substr(0, dot);
            out += ".chd";
        }

        chdlite::ChdArchiver archiver;
        auto result = archiver.archive(input_path, out, opts);

        if (!result.success)
            return json_error(result.error_message);

        std::ostringstream js;
        js << "{\"success\":true"
           << ",\"output_path\":\"" << json_escape(result.output_path) << "\""
           << ",\"input_bytes\":" << result.input_bytes
           << ",\"output_bytes\":" << result.output_bytes
           << ",\"compression_ratio\":" << result.compression_ratio
           << ",\"codec_used\":\"" << codec_name(result.codec_used) << "\""
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
