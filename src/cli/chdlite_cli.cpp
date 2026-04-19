// license:GPLv3
// CHDlite - Cross-platform CHD command-line tool
// chdlite_cli.cpp - chdman-compatible CLI with extended commands
//
// Generic commands (auto-detect from input):
//   chdlite extract <input.chd> [options]     - extract CHD to original format
//   chdlite create  <input> [options]          - create CHD from disc image
//   chdlite read    <input.chd> [options]      - read CHD header/metadata (info)
//   chdlite hash    <input.chd> [options]      - hash CHD content tracks
//   chdlite auto    <input> [options]           - auto: .chd→extract, else→create
//
// chdman-compatible commands:
//   chdlite info        -i <input.chd>
//   chdlite verify      -i <input.chd>
//   chdlite createcd    -i <input> -o <output.chd> [options]
//   chdlite createdvd   -i <input> -o <output.chd> [options]
//   chdlite createraw   -i <input> -o <output.chd> [options]
//   chdlite createhd    -i <input> -o <output.chd> [options]
//   chdlite extractcd   -i <input.chd> -o <output.cue> [options]
//   chdlite extractdvd  -i <input.chd> -o <output.iso> [options]
//   chdlite extractraw  -i <input.chd> -o <output.bin> [options]
//   chdlite extracthd   -i <input.chd> -o <output.bin> [options]

#include "chd_api.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

// OSD thread control — set before any compression to override auto-detect
extern int osd_num_processors;

namespace fs = std::filesystem;
using namespace chdlite;

// ======================> Activity log + Pretty log
// Appends to error.log (machine-parseable single-line, pipe-delimited) - for errors mainly.
// Optionally appends to chdread.log / chdhash.log / chdcomp.log etc (human-readable multi-line indented).
// Records: timestamp, version, full command line, status (OK or error).
// Log level control via --log <level>: info, error, none, debug, warning, critical
// Override with --log <level> for structured log detail
// LogLevel is defined in chdlite::chd_types.hpp and shared with the library.

static constexpr LogLevel LOG_LEVEL_DEFAULT = LogLevel::Info;

static LogLevel      g_log_level = LOG_LEVEL_DEFAULT;
static bool          g_pretty_log_enabled = false;
static std::string   g_current_command;  // set by main() for command-specific pretty log names
static std::string   g_cmdline;
static std::string   g_input_file;
static std::shared_ptr<spdlog::logger> g_logger;          // structured log (error.log)
static std::shared_ptr<spdlog::logger> g_pretty_logger;   // pretty log (chdread.log/chdhash.log/etc)
static fs::path g_logs_dir;  // set by init_log / --log-dir, used for hash output default

static void init_log(int argc, char** argv)
{
    // Build full command line string
    for (int i = 0; i < argc; i++)
    {
        if (i > 0) g_cmdline += ' ';
        std::string a = argv[i];
        if (a.find(' ') != std::string::npos)
            g_cmdline += '"' + a + '"';
        else
            g_cmdline += a;
    }

    // Resolve <exe_root>/logs/
    std::error_code ec;
    fs::path exe_dir = fs::canonical(fs::path(argv[0]).parent_path(), ec);
    g_logs_dir = (ec || exe_dir.empty()) ? fs::path("logs") : exe_dir / "logs";
    fs::create_directories(g_logs_dir, ec);
    
    // Initialize structured log (error.log - kept for errors/important info)
    std::string structured_log_path = (g_logs_dir / "error.log").string();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(structured_log_path, false /*truncate=false, i.e. append*/);
    g_logger = std::make_shared<spdlog::logger>("chdlite", file_sink);
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
    g_logger->set_level(spdlog::level::trace);  // gate via g_log_level below
    g_logger->flush_on(spdlog::level::debug);
    
    // Initialize pretty log (chdlite-pretty.log) — will be lazily created on first use
}

// Parse log level string → LogLevel enum
static LogLevel parse_log_level(const std::string& s)
{
    std::string lower = s;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info") return LogLevel::Info;
    if (lower == "warning" || lower == "warn") return LogLevel::Warning;
    if (lower == "error") return LogLevel::Error;
    if (lower == "critical") return LogLevel::Critical;
    if (lower == "none" || lower == "off") return LogLevel::None;
    return LogLevel::Info;  // default to info
}

// Ensure pretty logger is initialized with command-specific filename
static void ensure_pretty_logger()
{
    if (g_pretty_logger) return;
    
    // Build command-specific pretty log name: chd<cmd>.log
    std::string pretty_log_name = "chd" + g_current_command + ".log";
    std::string pretty_log_path = (g_logs_dir / pretty_log_name).string();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(pretty_log_path, false);
    g_pretty_logger = std::make_shared<spdlog::logger>("chdlite-pretty", file_sink);
    g_pretty_logger->set_pattern("%v");  // no timestamp prefix for pretty log
    g_pretty_logger->set_level(spdlog::level::trace);
    g_pretty_logger->flush_on(spdlog::level::debug);
}

// Write multi-line pretty log entry with indentation
static void log_pretty(const std::string& prefix, const std::map<std::string, std::string>& fields)
{
    if (!g_pretty_log_enabled) return;
    
    ensure_pretty_logger();
    
    // Timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    char ts[64];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t_now));
    
    g_pretty_logger->info(std::string(ts) + " " + prefix);
    for (const auto& [key, val] : fields)
    {
        g_pretty_logger->info("  " + key + ": " + val);
    }
    g_pretty_logger->info("");  // blank line separator
}

// Map chdlite::LogLevel → spdlog level and forward if at/above threshold.
static void log_entry(LogLevel level, const std::string& msg)
{
    if (g_log_level == LogLevel::None || level < g_log_level) return;
    if (!g_logger) return;

    // Prefix with context fields on first call per entry
    std::string entry = msg;
    if (!g_cmdline.empty())
        entry = "cmd=" + g_cmdline + " | " + entry;
    if (!g_input_file.empty())
        entry = "input=" + g_input_file + " | " + entry;
    entry = "v" + std::to_string(VERSION_MAJOR) + "." + std::to_string(VERSION_MINOR)
          + "." + std::to_string(VERSION_PATCH) + " | " + entry;

    switch (level) {
    case LogLevel::Debug:    g_logger->debug   (entry); break;
    case LogLevel::Info:     g_logger->info    (entry); break;
    case LogLevel::Warning:  g_logger->warn    (entry); break;
    case LogLevel::Error:    g_logger->error   (entry); break;
    case LogLevel::Critical: g_logger->critical(entry); break;
    default: break;
    }
}

static void log_debug(const std::string& msg)    { log_entry(LogLevel::Debug,    msg); }
static void log_info(const std::string& msg)     { log_entry(LogLevel::Info,     msg); }
static void log_warning(const std::string& msg)  { log_entry(LogLevel::Warning,  msg); }
static void log_error(const std::string& msg)    { log_entry(LogLevel::Error,    msg); }
static void log_critical(const std::string& msg) { log_entry(LogLevel::Critical, msg); }

static const char* format_source_name(FormatSource s)
{
    switch (s) {
    case FormatSource::Extension:    return "extension";
    case FormatSource::SyncBytes:    return "sync-bytes (raw CD sectors)";
    case FormatSource::DvdMatch:     return "dvd-match (platform identified)";
    case FormatSource::DvdFallback:  return "dvd-fallback (no specific match)";
    case FormatSource::CdOverride:   return "cd-override (CD platform beat DVD fallback)";
    default:                         return "unknown";
    }
}

static const char* platform_source_name(PlatformSource s)
{
    switch (s) {
    case PlatformSource::Sector0:   return "sector-0 magic";
    case PlatformSource::Iso9660:   return "ISO 9660 filesystem";
    case PlatformSource::Heuristic: return "heuristic";
    case PlatformSource::Default:   return "default (no match)";
    default:                        return "unknown";
    }
}

// ======================> Standalone-launch detection + pause
// On Windows, a process created via drag-and-drop (or double-click) owns its
// own console window.  A process launched from an existing terminal inherits
// one owned by the shell.  Comparing the console-window owner PID to the
// current PID distinguishes the two cases.

#ifdef _WIN32
#include <windows.h>
static bool is_standalone_launch()
{
    HWND hwnd = GetConsoleWindow();
    if (!hwnd) return false;
    DWORD console_pid = 0;
    GetWindowThreadProcessId(hwnd, &console_pid);
    return console_pid == GetCurrentProcessId();
}
#else
static bool is_standalone_launch() { return false; }
#endif

static void pause_for_user()
{
#ifdef _WIN32
    std::printf("\nPress Enter to close...");
    std::fflush(stdout);
    std::fflush(stdin);
    (void)std::getchar();
#endif
}

// ======================> Terminal output helpers (chdman style)

static std::string big_int_string(uint64_t val)
{
    std::string s = std::to_string(val);
    int n = static_cast<int>(s.size());
    std::string result;
    for (int i = 0; i < n; i++)
    {
        if (i > 0 && (n - i) % 3 == 0) result += ',';
        result += s[i];
    }
    return result;
}

static const char* content_type_name(ContentType ct)
{
    switch (ct) {
    case ContentType::HardDisk:  return "Hard Disk";
    case ContentType::CDROM:     return "CD-ROM";
    case ContentType::GDROM:     return "GD-ROM";
    case ContentType::DVD:       return "DVD";
    case ContentType::LaserDisc: return "LaserDisc";
    case ContentType::Raw:       return "Raw";
    default:                     return "Unknown";
    }
}

static const char* codec_name(Codec c)
{
    switch (c) {
    case Codec::None:      return "none";
    case Codec::Zlib:      return "zlib";
    case Codec::ZlibPlus:  return "zlib+";
    case Codec::Zstd:      return "zstd";
    case Codec::LZMA:      return "lzma";
    case Codec::FLAC:      return "flac";
    case Codec::Huffman:   return "huff";
    case Codec::AVHUFF:    return "avhuff";
    case Codec::CD_Zlib:   return "cdzl";
    case Codec::CD_Zstd:   return "cdzs";
    case Codec::CD_LZMA:   return "cdlz";
    case Codec::CD_FLAC:   return "cdfl";
    default:               return "?";
    }
}

static std::string codec_list_string(const Codec codecs[4])
{
    std::string s;
    for (int i = 0; i < 4; i++)
    {
        if (codecs[i] == Codec::None) break;
        if (!s.empty()) s += ", ";
        s += codec_name(codecs[i]);
    }
    return s.empty() ? "none" : s;
}

static const char* track_type_name(TrackType tt)
{
    switch (tt) {
    case TrackType::Mode1:        return "MODE1/2048";
    case TrackType::Mode1Raw:     return "MODE1/2352";
    case TrackType::Mode2:        return "MODE2/2336";
    case TrackType::Mode2Form1:   return "MODE2_FORM1";
    case TrackType::Mode2Form2:   return "MODE2_FORM2";
    case TrackType::Mode2FormMix: return "MODE2_MIX";
    case TrackType::Mode2Raw:     return "MODE2/2352";
    case TrackType::Audio:        return "AUDIO";
    default:                      return "???";
    }
}

static const char* hash_algorithm_name(HashFlags f)
{
    if (f == HashFlags::SHA1)     return "SHA-1";
    if (f == HashFlags::MD5)      return "MD5";
    if (f == HashFlags::CRC32)    return "CRC32";
    if (f == HashFlags::SHA256)   return "SHA-256";
    if (f == HashFlags::XXH3_128) return "XXH3-128";
    return "?";
}

// Progress callback: prints to stderr with carriage return (chdman style)
// label is optional — when non-empty, prefixes each line (used in parallel batch)
static auto make_progress(const char* verb, const std::string& label = {})
{
    auto last_time = std::chrono::steady_clock::now();
    return [verb, label, last_time](uint64_t done, uint64_t total) mutable -> bool
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (elapsed >= 500 || done >= total)
        {
            double pct = total ? 100.0 * done / total : 100.0;
            if (label.empty())
                std::fprintf(stderr, "\r%s, %.1f%% complete...  ", verb, pct);
            else
                std::fprintf(stderr, "[%s] %s, %.1f%%\n", label.c_str(), verb, pct);
            std::fflush(stderr);
            last_time = now;
        }
        if (done >= total && label.empty())
            std::fprintf(stderr, "\n");
        return true; // never cancel
    };
}

// ======================> Argument parsing

struct Args
{
    std::string command;
    std::string input;
    std::string output;
    std::string output_bin;
    std::string input_parent;
    std::string output_parent;
    std::string compression;     // e.g. "cdzs,cdlz,cdzl,cdfl"
    uint32_t    hunk_size = 0;
    uint32_t    unit_size = 0;
    int         num_processors = 0;
    int         max_concurrent_files = 0; // 0 = auto; -jf override
    bool        force = false;
    bool        splitbin = false;
    bool        no_splitbin = false;
    bool        verbose = false;
    uint64_t    input_start_byte = 0;
    uint64_t    input_start_hunk = 0;
    uint64_t    input_bytes = 0;
    uint64_t    input_hunks = 0;
    uint64_t    input_start_frame = 0;
    uint64_t    input_frames = 0;
    HashFlags   hash = HashFlags(0);    // 0 = none
    bool        hash_default = false;   // -hash with no args → SHA1
    std::string log_level;              // -log debug|info|warning|error|critical|none
    std::string log_dir;                // --log-dir <path>        override activity log directory
    std::string hash_dir;               // --hash-dir <path|"disc"> override .hashes output dir
                                        //   "disc" = next to input file
    int         cue_style = -1;          // -style 0/1/2 → CueStyle::Chdman/Redump/RedumpCatalog (-1 = default)
    bool        best = false;             // --best → maximum compression ratio
    bool        fix = false;              // --fix → verify: fix SHA1 mismatch by updating header
    std::string meta_tag;                 // -t → dumpmeta: 4-char metadata tag (e.g. "CHTR")
    int         meta_index = 0;           // -ix → dumpmeta: metadata index (default: 0)
    std::string result_format;            // --result text|json|lot|svg etc; controls pretty log (on by default for read/hash)
};

static void print_usage()
{
    std::printf(
        "chdlite - CHD disc image tool v%d.%d.%d\n"
        "\n"
        "  --version                              Show version and exit\n"
        "\n"
        "Generic commands (auto-detect format):\n"
        "  extract  <input.chd> [-o output] [options]    Extract CHD to disc image\n"
        "  create   <input>     [-o output.chd] [options] Create CHD from disc image\n"
        "  read     <input.chd> [options]                 Read CHD info (header + tracks)\n"
        "  hash     <input.chd> [-hash sha1,md5,...] [options] Hash CHD content\n"
        "  auto     <input>     [options]                 Auto: .chd→extract, else→create\n"
        "\n"
        "chdman-compatible commands:\n"
        "  info       -i <input.chd>                      Display CHD info\n"
        "  verify     -i <input.chd> [--fix]              Verify CHD integrity\n"
        "  copy       -i <input.chd> [-o <output.chd>]    Re-compress CHD with different codecs\n"
        "  dumpmeta   -i <input.chd> -t <tag> [-ix <n>]   Dump raw metadata by tag\n"
        "  createcd   -i <input> -o <output.chd> [opts]   Create CD CHD\n"
        "  createdvd  -i <input> -o <output.chd> [opts]   Create DVD CHD\n"
        "  createraw  -i <input> -o <output.chd> [opts]   Create raw CHD\n"
        "  createhd   -i <input> -o <output.chd> [opts]   Create hard disk CHD\n"
        "  extractcd  -i <input.chd> -o <output> [opts]   Extract CD from CHD\n"
        "  extractdvd -i <input.chd> -o <output> [opts]   Extract DVD from CHD\n"
        "  extractraw -i <input.chd> -o <output> [opts]   Extract raw from CHD\n"
        "  extracthd  -i <input.chd> -o <output> [opts]   Extract hard disk from CHD\n"
        "  convertcue -i <input.cue> [-o <output.cue>] -style <n>  Convert CUE style\n"
        "\n"
        "Options:\n"
        "  -i, --input <file>          Input file\n"
        "  -o, --output <file>         Output file\n"
        "  -ip, --inputparent <file>   Parent CHD for input\n"
        "  -f, --force                 Overwrite existing output\n"
        "  -c, --compression <codecs>  Compression: none, zlib, zstd, lzma, flac,\n"
        "                              cdzl, cdzs, cdlz, cdfl (comma-separated, up to 4)\n"
        "  -hs, --hunksize <bytes>     Hunk size in bytes\n"
        "  -us, --unitsize <bytes>     Unit size in bytes\n"
        "  -np, --numprocessors <n>    Number of compression threads\n"
        "  -ob, --outputbin <file>     Output bin filename template (%%t = track#)\n"
        "  -sb, --splitbin             Split output into per-track bin files\n"
        "  --no-splitbin               Single bin file for all tracks\n"
        "  -isb, --inputstartbyte <n>  Start byte offset for extraction\n"
        "  -ish, --inputstarthunk <n>  Start hunk offset for extraction\n"
        "  -ib, --inputbytes <n>       Number of bytes to extract\n"
        "  -ih, --inputhunks <n>       Number of hunks to extract\n"
        "  -hash <algorithms>          Compute hashes (sha1,md5,crc32,sha256,xxh3)\n"
        "                              Default: sha1. Comma-separated for multiple.\n"
        "  --hash-dir <path>           Directory for .hashes output file\n"
        "                              Default: <exe>/logs/  Special: \"disc\" = next to input\n"
        "  -log <level>                Structured log level: debug, info, warning, error, critical, none\n"
        "                              (default: info)\n"
        "  --log-dir <path>            Directory for chdlite.log  (default: <exe>/logs/)\n"
        "  --result <format>           Pretty log output: on|off (read/hash default: on; extract/create default: off)\n"
        "                              Formats: text, json, lot, svg, etc. (for hash output)\n"
        "  --best                      Maximum compression (cdlz+cdzs+cdzl+cdfl for CD, zstd+lzma+zlib+huff for DVD)\n"
        "  --fix                       Verify: fix SHA1 mismatch by updating header\n"
        "  -t, --tag <tag>             Dumpmeta: 4-char metadata tag (e.g. CHTR, DVD )\n"
        "  -ix, --index <n>            Dumpmeta: metadata index (default: 0)\n"
        "  -style <n>                  CUE style: 0=chdman 1=redump 2=redump+catalog\n"
        "  -v, --verbose               Verbose output\n"
        "\n",
        VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH
    );
}

static void print_version()
{
    std::printf("chdlite - CHD disc image tool v%d.%d.%d\n",
                VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
}

static HashFlags parse_hash_flags(const std::string& str)
{
    HashFlags flags = HashFlags(0);
    std::string s = str;
    // lowercase
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    auto has = [&](const char* tok) {
        return s.find(tok) != std::string::npos;
    };

    if (s == "all") return HashFlags::All;
    if (has("sha1"))   flags = flags | HashFlags::SHA1;
    if (has("md5"))    flags = flags | HashFlags::MD5;
    if (has("crc32") || has("crc")) flags = flags | HashFlags::CRC32;
    if (has("sha256")) flags = flags | HashFlags::SHA256;
    if (has("xxh3") || has("xxhash")) flags = flags | HashFlags::XXH3_128;

    return flags;
}

static Codec parse_codec(const std::string& s)
{
    if (s == "none")   return Codec::None;
    if (s == "zlib")   return Codec::Zlib;
    if (s == "zstd")   return Codec::Zstd;
    if (s == "lzma")   return Codec::LZMA;
    if (s == "flac")   return Codec::FLAC;
    if (s == "huff")   return Codec::Huffman;
    if (s == "avhuff") return Codec::AVHUFF;
    if (s == "cdzl")   return Codec::CD_Zlib;
    if (s == "cdzs")   return Codec::CD_Zstd;
    if (s == "cdlz")   return Codec::CD_LZMA;
    if (s == "cdfl")   return Codec::CD_FLAC;
    return Codec::None;
}

static Args parse_args(int argc, char** argv)
{
    Args a;
    if (argc < 2) return a;

    a.command = argv[1];

    // Check for bare input file as second arg (generic commands)
    int start = 2;
    if (argc > 2 && argv[2][0] != '-')
    {
        a.input = argv[2];
        start = 3;
    }

    for (int i = start; i < argc; i++)
    {
        std::string arg = argv[i];
        auto next = [&]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : "";
        };

        if (arg == "-i" || arg == "--input")           a.input = next();
        else if (arg == "-o" || arg == "--output")     a.output = next();
        else if (arg == "-ip" || arg == "--inputparent")  a.input_parent = next();
        else if (arg == "-op" || arg == "--outputparent") a.output_parent = next();
        else if (arg == "-ob" || arg == "--outputbin")    a.output_bin = next();
        else if (arg == "-sb" || arg == "--splitbin")     a.splitbin = true;
        else if (arg == "--no-splitbin")               a.no_splitbin = true;
        else if (arg == "-f" || arg == "--force")      a.force = true;
        else if (arg == "-v" || arg == "--verbose")    a.verbose = true;
        else if (arg == "-c" || arg == "--compression") a.compression = next();
        else if (arg == "-hs" || arg == "--hunksize")  a.hunk_size = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "-us" || arg == "--unitsize")   a.unit_size = static_cast<uint32_t>(std::stoul(next()));
        else if (arg == "-np" || arg == "-j" || arg == "--numprocessors") a.num_processors = std::stoi(next());
        else if (arg == "-jf" || arg == "--concurrent-files") a.max_concurrent_files = std::stoi(next());
        else if (arg == "-isb" || arg == "--inputstartbyte")  a.input_start_byte = std::stoull(next());
        else if (arg == "-ish" || arg == "--inputstarthunk")  a.input_start_hunk = std::stoull(next());
        else if (arg == "-ib" || arg == "--inputbytes")       a.input_bytes = std::stoull(next());
        else if (arg == "-ih" || arg == "--inputhunks")       a.input_hunks = std::stoull(next());
        else if (arg == "-isf" || arg == "--inputstartframe") a.input_start_frame = std::stoull(next());
        else if (arg == "-if" || arg == "--inputframes")      a.input_frames = std::stoull(next());
        else if (arg == "-log" || arg == "--log")       a.log_level = next();
        else if (arg == "--log-dir")                    a.log_dir = next();
        else if (arg == "--hash-dir")                   a.hash_dir = next();
        else if (arg == "--result")                     a.result_format = next();
        else if (arg == "-style" || arg == "--style" || arg == "--cue-style") a.cue_style = std::stoi(next());
        else if (arg == "--best")                       a.best = true;
        else if (arg == "--fix")                        a.fix = true;
        else if (arg == "-t" || arg == "--tag")         a.meta_tag = next();
        else if (arg == "-ix" || arg == "--index")      a.meta_index = std::stoi(next());
        else if (arg == "-hash" || arg == "--hash")
        {
            // -hash with optional value: if next arg looks like a flag, use default SHA1
            if (i + 1 < argc && argv[i + 1][0] != '-')
                a.hash = parse_hash_flags(next());
            else
                a.hash = HashFlags::SHA1;
            a.hash_default = (a.hash == HashFlags::SHA1);
        }
        else
        {
            // Bare argument: treat as input if input is empty, else output
            if (a.input.empty()) a.input = arg;
            else if (a.output.empty()) a.output = arg;
        }
    }
    return a;
}

// ======================> Command implementations

// Map CLI -style integer (0/1/2) to CueStyle enum.
// Returns CueStyle::Unmatched if invalid.
static CueStyle parse_cue_style(int n)
{
    switch (n) {
    case 0: return CueStyle::Chdman;
    case 1: return CueStyle::Redump;
    case 2: return CueStyle::RedumpCatalog;
    default: return CueStyle::Unmatched;
    }
}

static const char* cue_style_name(CueStyle s)
{
    switch (s) {
    case CueStyle::Chdman:        return "chdman";
    case CueStyle::Redump:        return "redump";
    case CueStyle::RedumpCatalog: return "redump+catalog";
    default:                      return "unknown";
    }
}

static int cmd_read(const Args& args)
{
    // "read" / "info" — display file info, tracks, and platform detection
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    // Non-CHD path: use detect_input for platform info, skip CHD-specific sections
    if (!ChdReader::is_chd_file(args.input))
    {
        // If this is a .bin, check if a .cue/.gdi in the same directory references it.
        // If so, redirect the user to use the sheet file instead.
        fs::path inp(args.input);
        std::string ext_lower = inp.extension().string();
        std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), ::tolower);
        if (ext_lower == ".bin")
        {
            std::string bin_name_lower = inp.filename().string();
            std::transform(bin_name_lower.begin(), bin_name_lower.end(), bin_name_lower.begin(), ::tolower);
            fs::path dir = inp.parent_path();
            std::error_code ec2;
            for (auto& e : fs::directory_iterator(dir, ec2))
            {
                std::string sheet_ext = e.path().extension().string();
                std::transform(sheet_ext.begin(), sheet_ext.end(), sheet_ext.begin(), ::tolower);
                if (sheet_ext != ".cue" && sheet_ext != ".gdi") continue;
                std::ifstream sf(e.path().string());
                std::string line;
                while (std::getline(sf, line))
                {
                    std::string line_lower = line;
                    std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);
                    if (line_lower.find(bin_name_lower) != std::string::npos)
                    {
                        std::printf("Skipped: %s is part of %s\n",
                            inp.filename().string().c_str(),
                            e.path().filename().string().c_str());
                        std::printf("Use:     read \"%s\"\n", e.path().string().c_str());
                        log_info("skipped .bin redirect to " + e.path().filename().string());
                        return 0;
                    }
                }
            }
        }

        if (!fs::exists(args.input))
        {
            std::fprintf(stderr, "Error: file not found: %s\n", args.input.c_str());
            log_error("file not found: " + args.input);
            return 1;
        }

        std::printf("Input file:   %s\n", args.input.c_str());
        std::error_code ec;
        auto fsize = fs::file_size(fs::path(args.input), ec);
        if (!ec)
            std::printf("File size:    %s bytes\n", big_int_string(static_cast<uint64_t>(fsize)).c_str());

        auto det = detect_input(args.input, true);
        if (!det.format.empty())
        {
            const char* fmt_name = det.format == "cd"  ? "CD-ROM"
                                 : det.format == "dvd" ? "DVD"
                                 :                       "Raw";
            std::printf("Format:       %s\n", fmt_name);
        }
        if (det.game_platform != GamePlatform::Unknown)
        {
            std::printf("Platform:     %s\n", game_platform_name(det.game_platform));
            std::printf("Title:        %s\n", det.title.empty() ? "N/A" : det.title.c_str());
            std::printf("Manufacturer ID: %s\n", det.manufacturer_id.empty() ? "N/A" : det.manufacturer_id.c_str());
        }
        if (!det.detection_source.empty())
            std::printf("Detection:    %s\n", det.detection_source.c_str());
        log_debug("detect: format-source=" + std::string(format_source_name(det.format_source))
            + " platform=" + game_platform_name(det.game_platform)
            + " platform-source=" + platform_source_name(det.platform_source));
        log_info("read OK: i=" + args.input);
        return 0;
    }

    // CHD path: open with ChdReader and display full CHD info
    ChdReader reader;
    try {
        reader.open(args.input);
    } catch (const std::exception& e) {
        std::string err = std::string("opening CHD: ") + e.what();
        std::fprintf(stderr, "Error %s\n", err.c_str());
        log_error("read: i=" + args.input + " " + err);
        return 1;
    }

    auto hdr = reader.read_header();

    // Print header (chdman info style)
    std::printf("Input file:   %s\n", args.input.c_str());
    std::printf("File Version: %u\n", hdr.version);
    std::printf("Logical size: %s bytes\n", big_int_string(hdr.logical_bytes).c_str());
    std::printf("Hunk Size:    %s bytes\n", big_int_string(hdr.hunk_bytes).c_str());
    std::printf("Total Hunks:  %s\n", big_int_string(hdr.hunk_count).c_str());
    std::printf("Unit Size:    %s bytes\n", big_int_string(hdr.unit_bytes).c_str());
    std::printf("Compression:  %s\n", codec_list_string(hdr.codecs).c_str());
    std::printf("Compressed:   %s\n", hdr.compressed ? "yes" : "no");
    std::printf("Has Parent:   %s\n", hdr.has_parent ? "yes" : "no");
    std::printf("Content Type: %s\n", content_type_name(hdr.content_type));
    if (!hdr.sha1.empty())
        std::printf("SHA-1:        %s\n", hdr.sha1.c_str());
    if (!hdr.raw_sha1.empty())
        std::printf("Data SHA-1:   %s\n", hdr.raw_sha1.c_str());
    if (!hdr.parent_sha1.empty())
        std::printf("Parent SHA-1: %s\n", hdr.parent_sha1.c_str());

    // Track info for CD/GD
    if (hdr.content_type == ContentType::CDROM || hdr.content_type == ContentType::GDROM)
    {
        auto tracks = reader.get_tracks();
        std::printf("Tracks:       %u%s\n", static_cast<unsigned>(tracks.size()),
                    hdr.content_type == ContentType::GDROM ? "  (GD-ROM)" : "");
        for (auto& t : tracks)
        {
            std::printf("  Track %02u: %-14s  frames=%u  data=%u sub=%u%s\n",
                        t.track_number, track_type_name(t.type),
                        t.frames, t.data_size, t.sub_size,
                        t.is_audio ? " [AUDIO]" : "");
        }
    }

    // Platform detection
    auto det = reader.detect_game_platform(DetectFlags::All, true);
    if (det.game_platform != GamePlatform::Unknown)
    {
        std::printf("Platform:     %s\n", game_platform_name(det.game_platform));
        std::printf("Title:        %s\n", det.title.empty() ? "N/A" : det.title.c_str());
        std::printf("Manufacturer ID: %s\n", det.manufacturer_id.empty() ? "N/A" : det.manufacturer_id.c_str());
    }
    if (!det.detection_source.empty())
        std::printf("Detection:    %s\n", det.detection_source.c_str());

    // Raw metadata dump (verbose only) — enumerate all known tags
    if (args.verbose)
    {
        struct TagDef { const char* name; uint32_t tag; };
        auto mk = [](char a, char b, char c, char d) -> uint32_t {
            return (uint32_t(uint8_t(a)) << 24) | (uint32_t(uint8_t(b)) << 16)
                 | (uint32_t(uint8_t(c)) << 8)  | uint32_t(uint8_t(d));
        };
        TagDef tags[] = {
            { "CHT2", mk('C','H','T','2') },   // CD track (V5)
            { "CHTR", mk('C','H','T','R') },   // CD track (V3/V4)
            { "CHGT", mk('C','H','G','T') },   // GD-ROM track (V5)
            { "CHGD", mk('C','H','G','D') },   // GD-ROM track (V3/V4)
            { "DVD ", mk('D','V','D',' ') },    // DVD
            { "GDDD", mk('G','D','D','D') },    // Hard disk
            { "IDNT", mk('I','D','N','T') },    // Hard disk identity
            { "CIS ", mk('C','I','S',' ') },    // PCMCIA CIS
            { "AVAV", mk('A','V','A','V') },    // A/V LaserDisc
            { "AVLD", mk('A','V','L','D') },    // A/V LaserDisc
        };

        bool has_meta = false;
        std::string meta_log;
        for (auto& td : tags)
        {
            for (int ix = 0; ix < 100; ix++)
            {
                std::string data = reader.read_metadata(td.tag, ix);
                if (data.empty()) break;
                if (!has_meta) { std::printf("Metadata:\n"); has_meta = true; }
                std::printf("  %s[%d]: %s\n", td.name, ix, data.c_str());
                if (!meta_log.empty()) meta_log += " | ";
                meta_log += std::string(td.name) + "[" + std::to_string(ix) + "]=" + data;
            }
        }
        if (!meta_log.empty())
            log_debug("metadata: " + meta_log);
    }

    // Log all read data
    {
        std::string log;
        log += "type=" + std::string(content_type_name(hdr.content_type));
        log += " version=" + std::to_string(hdr.version);
        log += " size=" + std::to_string(hdr.logical_bytes);
        log += " hunk=" + std::to_string(hdr.hunk_bytes);
        log += " unit=" + std::to_string(hdr.unit_bytes);
        log += " compression=" + codec_list_string(hdr.codecs);
        if (!hdr.sha1.empty())
            log += " sha1=" + hdr.sha1;
        if (!hdr.raw_sha1.empty())
            log += " raw_sha1=" + hdr.raw_sha1;
        if (hdr.content_type == ContentType::CDROM || hdr.content_type == ContentType::GDROM)
            log += " tracks=" + std::to_string(hdr.tracks.size());
        if (det.game_platform != GamePlatform::Unknown)
        {
            log += " platform=" + std::string(game_platform_name(det.game_platform));
            if (!det.title.empty())
                log += " title=" + det.title;
            if (!det.manufacturer_id.empty())
                log += " id=" + det.manufacturer_id;
        }
        log += " i=" + args.input;
        log_info("read OK: " + log);
        
        // Write pretty log if enabled
        if (g_pretty_log_enabled)
        {
            std::map<std::string, std::string> fields;
            fields["type"] = content_type_name(hdr.content_type);
            fields["version"] = std::to_string(hdr.version);
            fields["size"] = big_int_string(hdr.logical_bytes) + " bytes";
            fields["hunk_size"] = std::to_string(hdr.hunk_bytes) + " bytes";
            fields["unit_size"] = std::to_string(hdr.unit_bytes) + " bytes";
            fields["compression"] = codec_list_string(hdr.codecs);
            if (!hdr.sha1.empty())
                fields["sha1"] = hdr.sha1;
            if (!hdr.raw_sha1.empty())
                fields["raw_sha1"] = hdr.raw_sha1;
            if (hdr.content_type == ContentType::CDROM || hdr.content_type == ContentType::GDROM)
                fields["tracks"] = std::to_string(hdr.tracks.size());
            // Always log platform, title, and manufacturer_id, using N/A if empty
            if (det.game_platform != GamePlatform::Unknown)
                fields["platform"] = game_platform_name(det.game_platform);
            else
                fields["platform"] = "N/A";
            fields["title"] = det.title.empty() ? "N/A" : det.title;
            fields["manufacturer_id"] = det.manufacturer_id.empty() ? "N/A" : det.manufacturer_id;
            fields["input"] = args.input;
            log_pretty("read", fields);
        }
    }
    return 0;
}

// ======================> Hash file writer (shared by all commands that produce hashes)
// Resolves the output path from args.hash_dir:
//   ""     → g_logs_dir (default)
//   "disc" → directory of input file
//   else   → use as-is (treated as a directory)
// Filename is always <input-filename>.hashes

static void write_hashes_file(const ContentHashResult& result,
                               const std::string& input_path,
                               const std::string& hash_dir_arg,
                               HashFlags flags)
{
    // Resolve output directory
    fs::path out_dir;
    if (hash_dir_arg.empty())
    {
        out_dir = g_logs_dir.empty() ? fs::path("logs") : g_logs_dir;
    }
    else if (hash_dir_arg == "disc")
    {
        out_dir = fs::path(input_path).parent_path();
        if (out_dir.empty()) out_dir = fs::path(".");
    }
    else
    {
        out_dir = fs::path(hash_dir_arg);
        std::error_code ec;
        fs::create_directories(out_dir, ec);
    }

    fs::path hashes_name = fs::path(input_path).filename();
    hashes_name += ".hashes";
    std::string hashes_path = (out_dir / hashes_name).string();

    std::ofstream hf(hashes_path);
    if (!hf.is_open()) return;

    std::time_t now = std::time(nullptr);
    char timebuf[64];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    hf << "# CHDlite hash results\n";
    hf << "# Source:  " << input_path << "\n";
    hf << "# Date:    " << timebuf << "\n";
    hf << "# Tracks:  " << result.tracks.size() << "\n";
    hf << "#\n";

    for (auto& t : result.tracks)
    {
        uint32_t tnum = t.track_number;
        char prefix[16];
        std::snprintf(prefix, sizeof(prefix), "Track %02u", tnum);
        if (has_flag(flags, HashFlags::SHA1)    && !t.sha1.hex_string.empty())
            hf << prefix << "  SHA-1:    " << t.sha1.hex_string    << "\n";
        if (has_flag(flags, HashFlags::MD5)     && !t.md5.hex_string.empty())
            hf << prefix << "  MD5:      " << t.md5.hex_string     << "\n";
        if (has_flag(flags, HashFlags::CRC32)   && !t.crc32.hex_string.empty())
            hf << prefix << "  CRC32:    " << t.crc32.hex_string   << "\n";
        if (has_flag(flags, HashFlags::SHA256)  && !t.sha256.hex_string.empty())
            hf << prefix << "  SHA-256:  " << t.sha256.hex_string  << "\n";
        if (has_flag(flags, HashFlags::XXH3_128) && !t.xxh3_128.hex_string.empty())
            hf << prefix << "  XXH3-128: " << t.xxh3_128.hex_string << "\n";
    }

    if (!result.sheet_content.empty())
    {
        if (has_flag(flags, HashFlags::SHA1)  && !result.sheet_hash.sha1.hex_string.empty())
            hf << "Sheet     SHA-1:    " << result.sheet_hash.sha1.hex_string  << "\n";
        if (has_flag(flags, HashFlags::MD5)   && !result.sheet_hash.md5.hex_string.empty())
            hf << "Sheet     MD5:      " << result.sheet_hash.md5.hex_string   << "\n";
        if (has_flag(flags, HashFlags::CRC32) && !result.sheet_hash.crc32.hex_string.empty())
            hf << "Sheet     CRC32:    " << result.sheet_hash.crc32.hex_string << "\n";
    }

    hf.flush();
    std::printf("Hash file:    %s\n", hashes_path.c_str());
}

static int cmd_hash(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    // Default hash flags: SHA1 if none specified
    HashFlags flags = (uint32_t(args.hash) != 0) ? args.hash : HashFlags::SHA1;

    ChdReader reader;
    try {
        reader.open(args.input);
    } catch (const std::exception& e) {
        std::string err = std::string("opening CHD: ") + e.what();
        std::fprintf(stderr, "Error %s\n", err.c_str());
        log_error(err);
        return 1;
    }

    std::printf("Input file:   %s\n", args.input.c_str());
    std::printf("Hashing content...\n");

    auto t0 = std::chrono::steady_clock::now();
    ContentHashResult result;
    try {
        result = reader.hash_content(flags);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        log_error("hash: i=" + args.input + " " + e.what());
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    if (!result.success)
    {
        std::fprintf(stderr, "Error: %s\n", result.error_message.c_str());
        log_error("hash: i=" + args.input + " " + result.error_message);
        return 1;
    }

    std::printf("Content Type: %s\n", content_type_name(result.content_type));
    if (!result.chd_sha1.empty())
        std::printf("CHD SHA-1:    %s\n", result.chd_sha1.c_str());
    if (!result.chd_raw_sha1.empty())
        std::printf("Raw SHA-1:    %s\n", result.chd_raw_sha1.c_str());

    std::printf("Tracks:       %u  (%.1fs)\n",
                static_cast<unsigned>(result.tracks.size()), secs);

    for (auto& t : result.tracks)
    {
        uint32_t tnum = t.track_number;
        std::printf("  Track %02u: %s  %s bytes\n",
                    tnum, t.is_audio ? "AUDIO " : track_type_name(t.type),
                    big_int_string(t.data_bytes).c_str());

        if (has_flag(flags, HashFlags::SHA1) && !t.sha1.hex_string.empty())
            std::printf("    SHA-1:    %s\n", t.sha1.hex_string.c_str());
        if (has_flag(flags, HashFlags::MD5) && !t.md5.hex_string.empty())
            std::printf("    MD5:      %s\n", t.md5.hex_string.c_str());
        if (has_flag(flags, HashFlags::CRC32) && !t.crc32.hex_string.empty())
            std::printf("    CRC32:    %s\n", t.crc32.hex_string.c_str());
        if (has_flag(flags, HashFlags::SHA256) && !t.sha256.hex_string.empty())
            std::printf("    SHA-256:  %s\n", t.sha256.hex_string.c_str());
        if (has_flag(flags, HashFlags::XXH3_128) && !t.xxh3_128.hex_string.empty())
            std::printf("    XXH3-128: %s\n", t.xxh3_128.hex_string.c_str());
    }

    // Sheet hash
    if (!result.sheet_content.empty())
    {
        std::printf("Sheet (%u bytes):\n", static_cast<unsigned>(result.sheet_content.size()));
        if (has_flag(flags, HashFlags::SHA1) && !result.sheet_hash.sha1.hex_string.empty())
            std::printf("    SHA-1:    %s\n", result.sheet_hash.sha1.hex_string.c_str());
        if (has_flag(flags, HashFlags::MD5) && !result.sheet_hash.md5.hex_string.empty())
            std::printf("    MD5:      %s\n", result.sheet_hash.md5.hex_string.c_str());
        if (has_flag(flags, HashFlags::CRC32) && !result.sheet_hash.crc32.hex_string.empty())
            std::printf("    CRC32:    %s\n", result.sheet_hash.crc32.hex_string.c_str());
    }

    // Write .hashes file (default: <exe>/logs/, override with --hash-dir)
    write_hashes_file(result, args.input, args.hash_dir, flags);

    log_info("hash OK: i=" + args.input + " tracks=" + std::to_string(result.tracks.size()));
    
    // Write pretty log if enabled
    if (g_pretty_log_enabled)
    {
        std::map<std::string, std::string> fields;
        fields["input"] = args.input;
        fields["tracks"] = std::to_string(result.tracks.size());
        
        // Count track hashes
        int hash_count = 0;
        for (const auto& t : result.tracks)
        {
            if (!t.sha1.hex_string.empty()) hash_count++;
        }
        fields["hashed_tracks"] = std::to_string(hash_count);
        
        // Sheet hash info
        if (!result.sheet_content.empty())
            fields["sheet"] = std::to_string(result.sheet_content.size()) + " bytes";
        
        log_pretty("hash", fields);
    }
    return 0;
}

static int cmd_extract(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    ExtractOptions opts;
    opts.parent_chd_path = args.input_parent;
    opts.force_overwrite = args.force;
    opts.output_bin = args.output_bin;
    opts.input_start_byte = args.input_start_byte;
    opts.input_start_hunk = args.input_start_hunk;
    opts.input_bytes = args.input_bytes;
    opts.input_hunks = args.input_hunks;
    opts.input_start_frame = args.input_start_frame;
    opts.input_frames = args.input_frames;
    opts.hash = args.hash;

    // CUE style override
    if (args.cue_style >= 0 && args.cue_style <= 2)
        opts.cue_style = parse_cue_style(args.cue_style);

    // Split-bin: default true, overridable with --no-splitbin
    opts.split_bin = args.no_splitbin ? false : true;

    // Determine output path
    if (!args.output.empty())
    {
        fs::path out(args.output);
        if (fs::is_directory(out) || args.output.back() == '/' || args.output.back() == '\\')
        {
            // Directory: use as output_dir, extractor auto-generates filename from input stem
            opts.output_dir = out.string();
        }
        else
        {
            opts.output_dir = out.parent_path().string();
            opts.output_filename = out.filename().string();

            // Detect force_raw / force_bin_cue from output extension
            std::string ext = out.extension().string();
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".bin") opts.force_raw = true;
        }
    }
    else
    {
        // Default: same directory as input, auto-detect format
        opts.output_dir = fs::path(args.input).parent_path().string();
    }

    // Progress callback
    opts.progress_callback = make_progress("Extracting");

    // Print header info
    std::printf("Input CHD:    %s\n", args.input.c_str());

    ChdExtractor extractor;
    ExtractionResult result;
    try {
        result = extractor.extract(args.input, opts);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\nError: %s\n", e.what());
        log_error("extract: i=" + args.input + " " + e.what());
        return 1;
    }

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error("extract: i=" + args.input + " " + result.error_message);
        return 1;
    }

    std::printf("Content Type: %s\n", content_type_name(result.detected_type));
    std::printf("Output:       %s\n", result.output_path.c_str());
    for (auto& f : result.output_files)
        std::printf("  -> %s\n", f.c_str());
    std::printf("Bytes:        %s\n", big_int_string(result.bytes_written).c_str());
    std::printf("Extraction complete\n");

    log_info("extract OK: i=" + args.input + " o=" + result.output_path + " bytes=" + std::to_string(result.bytes_written));
    
    // Write pretty log if enabled
    if (g_pretty_log_enabled)
    {
        std::map<std::string, std::string> fields;
        fields["input"] = args.input;
        fields["output"] = result.output_path;
        fields["bytes"] = big_int_string(result.bytes_written);
        fields["content_type"] = content_type_name(result.detected_type);
        for (size_t i = 0; i < result.output_files.size(); i++)
            fields["file_" + std::to_string(i+1)] = result.output_files[i];
        log_pretty("extract", fields);
    }
    return 0;
}

static int cmd_create(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    ArchiveOptions opts;
    opts.parent_chd_path = args.input_parent;
    opts.hunk_bytes = args.hunk_size;
    opts.unit_bytes = args.unit_size;
    opts.num_processors = args.num_processors;
    opts.detect_title = true;
    opts.best = args.best;

    // Parse compression
    if (!args.compression.empty())
    {
        if (args.compression == "none")
        {
            std::fprintf(stderr, "Error: uncompressed CHD (-c none) is not yet supported\n");
            return 1;
        }
        else if (args.compression == "chdman")
        {
            opts.chdman_compat = true;
        }
        else
        {
            // Parse comma-separated codec list
            std::string s = args.compression;
            int slot = 0;
            size_t pos = 0;
            while (pos < s.size() && slot < 4)
            {
                auto comma = s.find(',', pos);
                std::string tok = (comma == std::string::npos)
                    ? s.substr(pos) : s.substr(pos, comma - pos);
                // trim
                while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
                while (!tok.empty() && tok.back() == ' ') tok.pop_back();
                for (auto& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                opts.compression[slot++] = parse_codec(tok);
                pos = (comma == std::string::npos) ? s.size() : comma + 1;
            }
        }
    }

    // Determine output path
    std::string output = args.output;
    if (output.empty())
    {
        // Default: same name with .chd extension
        fs::path p(args.input);
        output = (p.parent_path() / p.stem()).string() + ".chd";
    }
    else if (fs::is_directory(output) || output.back() == '/' || output.back() == '\\')
    {
        // Directory: generate filename from input stem
        output = (fs::path(output) / (fs::path(args.input).stem().string() + ".chd")).string();
    }

    // Progress callback
    opts.progress_callback = make_progress("Compressing");

    // Library log callback — forwards Debug+ messages to the CLI log
    opts.log_callback = [](LogLevel lvl, const std::string& msg) { log_entry(lvl, msg); };
    std::printf("Output CHD:   %s\n", output.c_str());
    std::printf("Input file:   %s\n", args.input.c_str());
    if (!args.input_parent.empty())
        std::printf("Parent CHD:   %s\n", args.input_parent.c_str());
    if (opts.has_custom_compression())
        std::printf("Compression:  %s\n", codec_list_string(opts.compression).c_str());
    else if (opts.best)
        std::printf("Compression:  best (cdlz+cdzs+cdzl+cdfl for CD, zstd+lzma+zlib+huff for DVD)\n");
    else
        std::printf("Compression:  auto (smart defaults)\n");
    if (opts.hunk_bytes)
        std::printf("Hunk size:    %s bytes\n", big_int_string(opts.hunk_bytes).c_str());

    // Wire -np to OSD thread control
    if (args.num_processors > 0)
        osd_num_processors = args.num_processors;

    ChdArchiver archiver;
    ArchiveResult result;
    try {
        result = archiver.archive(args.input, output, opts);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\nError: %s\n", e.what());
        log_error("create: i=" + args.input + " o=" + output + " " + e.what());
        return 1;
    }

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error("create: i=" + args.input + " o=" + output + " " + result.error_message);
        return 1;
    }

    std::printf("Input size:   %s bytes\n", big_int_string(result.input_bytes).c_str());
    std::printf("Output size:  %s bytes\n", big_int_string(result.output_bytes).c_str());
    std::printf("Ratio:        %.1f%%\n", result.compression_ratio * 100.0);
    if (result.detected_game_platform != GamePlatform::Unknown)
        std::printf("Platform:     %s\n", game_platform_name(result.detected_game_platform));
    if (!result.detected_title.empty())
        std::printf("Title:        %s\n", result.detected_title.c_str());
    if (!result.detected_manufacturer_id.empty())
        std::printf("Manufacturer ID: %s\n", result.detected_manufacturer_id.c_str());
    std::printf("Compression complete\n");

    log_info("create OK: i=" + args.input + " o=" + output + " bytes=" + std::to_string(result.output_bytes));
    
    // Write pretty log if enabled
    if (g_pretty_log_enabled)
    {
        std::map<std::string, std::string> fields;
        fields["input"] = args.input;
        fields["output"] = output;
        fields["input_bytes"] = big_int_string(result.input_bytes);
        fields["output_bytes"] = big_int_string(result.output_bytes);
        fields["compression_ratio"] = std::to_string(static_cast<int>(result.compression_ratio * 100.0)) + "%";
        if (result.detected_game_platform != GamePlatform::Unknown)
            fields["platform"] = game_platform_name(result.detected_game_platform);
        if (!result.detected_title.empty())
            fields["title"] = result.detected_title;
        if (!result.detected_manufacturer_id.empty())
            fields["manufacturer_id"] = result.detected_manufacturer_id;
        log_pretty("create", fields);
    }
    return 0;
}

static int cmd_create_typed(const Args& args, const char* type_hint)
{
    // Force a specific archive path: createcd, createdvd, createraw, createhd
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: -i <input> is required for %s\n", type_hint);
        return 1;
    }

    // Default output: same directory/stem as input + .chd
    std::string output = args.output;
    if (output.empty())
    {
        fs::path p(args.input);
        output = (p.parent_path() / p.stem()).string() + ".chd";
    }

    ArchiveOptions opts;
    opts.parent_chd_path = args.input_parent;
    opts.hunk_bytes = args.hunk_size;
    opts.unit_bytes = args.unit_size;
    opts.num_processors = args.num_processors;
    opts.detect_title = true;
    opts.best = args.best;

    // Warn when createraw/createhd is used without -us
    if (std::string(type_hint) == "raw" && args.unit_size == 0)
        std::printf("Note: -us (unit size) not specified, defaulting to 512 bytes\n");

    if (!args.compression.empty())
    {
        if (args.compression == "none")
        {
            std::fprintf(stderr, "Error: uncompressed CHD (-c none) is not yet supported\n");
            return 1;
        }
        else if (args.compression == "chdman")
        {
            opts.chdman_compat = true;
        }
        else
        {
            std::string s = args.compression;
            int slot = 0;
            size_t pos = 0;
            while (pos < s.size() && slot < 4)
            {
                auto comma = s.find(',', pos);
                std::string tok = (comma == std::string::npos)
                    ? s.substr(pos) : s.substr(pos, comma - pos);
                while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
                while (!tok.empty() && tok.back() == ' ') tok.pop_back();
                for (auto& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                opts.compression[slot++] = parse_codec(tok);
                pos = (comma == std::string::npos) ? s.size() : comma + 1;
            }
        }
    }

    opts.progress_callback = make_progress("Compressing");

    std::printf("Output CHD:   %s\n", output.c_str());
    std::printf("Input file:   %s\n", args.input.c_str());
    if (!args.input_parent.empty())
        std::printf("Parent CHD:   %s\n", args.input_parent.c_str());
    if (opts.has_custom_compression())
        std::printf("Compression:  %s\n", codec_list_string(opts.compression).c_str());
    if (opts.hunk_bytes)
        std::printf("Hunk size:    %s bytes\n", big_int_string(opts.hunk_bytes).c_str());

    // Wire -np to OSD thread control
    if (args.num_processors > 0)
        osd_num_processors = args.num_processors;

    ChdArchiver archiver;
    ArchiveResult result;

    std::string th = type_hint;
    try {
        if (th == "cd")        result = archiver.archive_cd(args.input, output, opts);
        else if (th == "dvd")  result = archiver.archive_dvd(args.input, output, opts);
        else                   result = archiver.archive_raw(args.input, output, opts);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\nError: %s\n", e.what());
        log_error(std::string(type_hint) + ": i=" + args.input + " o=" + output + " " + e.what());
        return 1;
    }

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error(std::string(type_hint) + ": i=" + args.input + " o=" + output + " " + result.error_message);
        return 1;
    }

    std::printf("Input size:   %s bytes\n", big_int_string(result.input_bytes).c_str());
    std::printf("Output size:  %s bytes\n", big_int_string(result.output_bytes).c_str());
    std::printf("Ratio:        %.1f%%\n", result.compression_ratio * 100.0);
    std::printf("Compression complete\n");

    log_info(std::string(type_hint) + " OK: i=" + args.input + " o=" + output + " bytes=" + std::to_string(result.output_bytes));
    
    // Write pretty log if enabled
    if (g_pretty_log_enabled)
    {
        std::map<std::string, std::string> fields;
        fields["input"] = args.input;
        fields["output"] = output;
        fields["input_bytes"] = big_int_string(result.input_bytes);
        fields["output_bytes"] = big_int_string(result.output_bytes);
        fields["compression_ratio"] = std::to_string(static_cast<int>(result.compression_ratio * 100.0)) + "%";
        log_pretty(type_hint, fields);
    }
    return 0;
}

static int cmd_extract_typed(const Args& args, const char* type_hint)
{
    // extractcd, extractdvd, extractraw, extracthd
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: -i <input> is required for extract%s\n", type_hint);
        return 1;
    }

    ExtractOptions opts;
    opts.parent_chd_path = args.input_parent;
    opts.force_overwrite = args.force;
    opts.output_bin = args.output_bin;
    opts.input_start_byte = args.input_start_byte;
    opts.input_start_hunk = args.input_start_hunk;
    opts.input_bytes = args.input_bytes;
    opts.input_hunks = args.input_hunks;
    opts.hash = args.hash;
    opts.split_bin = args.no_splitbin ? false : true;

    // CUE style override
    if (args.cue_style >= 0 && args.cue_style <= 2)
        opts.cue_style = parse_cue_style(args.cue_style);

    std::string th = type_hint;
    if (th == "raw" || th == "hd") opts.force_raw = true;

    // Determine output path — default: same dir as input, extension from type_hint
    std::string output_display;
    if (!args.output.empty())
    {
        fs::path out(args.output);
        if (fs::is_directory(out) || args.output.back() == '/' || args.output.back() == '\\')
        {
            // Directory: use as output_dir, extractor auto-generates filename from input stem
            opts.output_dir = out.string();
            output_display = out.string();
        }
        else
        {
            opts.output_dir = out.parent_path().string();
            opts.output_filename = out.filename().string();
            output_display = args.output;
        }
    }
    else
    {
        fs::path inp(args.input);
        opts.output_dir = inp.parent_path().string();
        // For CD: leave filename empty so extractor auto-picks .cue/.gdi
        // For DVD: stem + .iso, for raw/hd: stem + .bin
        if (th == "dvd")
            opts.output_filename = inp.stem().string() + ".iso";
        else if (th == "raw" || th == "hd")
            opts.output_filename = inp.stem().string() + ".bin";
        // th == "cd": leave output_filename empty → extractor auto-generates .cue or .gdi
        output_display = (fs::path(opts.output_dir) / (opts.output_filename.empty()
            ? inp.stem().string() + ".*" : opts.output_filename)).string();
    }

    opts.progress_callback = make_progress("Extracting");

    std::printf("Input CHD:    %s\n", args.input.c_str());
    if (!args.input_parent.empty())
        std::printf("Parent CHD:   %s\n", args.input_parent.c_str());
    std::printf("Output file:  %s\n", output_display.c_str());
    if (opts.input_start_byte || opts.input_start_hunk)
        std::printf("Input start:  %s\n",
            opts.input_start_byte ? big_int_string(opts.input_start_byte).c_str()
                                  : (big_int_string(opts.input_start_hunk) + " hunks").c_str());
    if (opts.input_bytes || opts.input_hunks)
        std::printf("Input length: %s\n",
            opts.input_bytes ? big_int_string(opts.input_bytes).c_str()
                             : (big_int_string(opts.input_hunks) + " hunks").c_str());

    ChdExtractor extractor;
    ExtractionResult result;
    try {
        result = extractor.extract(args.input, opts);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\nError: %s\n", e.what());
        log_error(std::string("extract") + type_hint + ": i=" + args.input + " " + e.what());
        return 1;
    }

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error(std::string("extract") + type_hint + ": i=" + args.input + " " + result.error_message);
        return 1;
    }

    std::printf("Content Type: %s\n", content_type_name(result.detected_type));
    for (auto& f : result.output_files)
        std::printf("  -> %s\n", f.c_str());
    std::printf("Bytes:        %s\n", big_int_string(result.bytes_written).c_str());
    std::printf("Extraction complete\n");

    log_info(std::string("extract") + type_hint + " OK: i=" + args.input + " o=" + result.output_path + " bytes=" + std::to_string(result.bytes_written));
    
    // Write pretty log if enabled
    if (g_pretty_log_enabled)
    {
        std::map<std::string, std::string> fields;
        fields["input"] = args.input;
        fields["output"] = result.output_path;
        fields["bytes"] = big_int_string(result.bytes_written);
        fields["content_type"] = content_type_name(result.detected_type);
        for (size_t i = 0; i < result.output_files.size(); i++)
            fields["file_" + std::to_string(i+1)] = result.output_files[i];
        log_pretty(std::string("extract") + type_hint, fields);
    }
    return 0;
}

// ======================> verify command

static int cmd_verify(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    ChdReader reader;
    try {
        reader.open(args.input);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error opening CHD: %s\n", e.what());
        log_error(std::string("verify: i=") + args.input + " " + e.what());
        return 1;
    }

    std::printf("Verifying:    %s\n", args.input.c_str());

    auto progress = make_progress("Verifying");

    VerifyResult vr;
    try {
        if (args.fix)
            vr = reader.verify_fix(progress);
        else
            vr = reader.verify(progress);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\nError: %s\n", e.what());
        log_error("verify: i=" + args.input + " " + e.what());
        return 1;
    }

    if (!vr.success)
    {
        std::fprintf(stderr, "\nError: %s\n", vr.error_message.c_str());
        log_error("verify: i=" + args.input + " " + vr.error_message);
        return 1;
    }

    std::printf("Raw SHA1:     %s %s\n", vr.computed_raw_sha1.c_str(),
                vr.raw_sha1_match ? "(verified)" : "(MISMATCH)");
    std::printf("Overall SHA1: %s %s\n", vr.computed_overall_sha1.c_str(),
                vr.overall_sha1_match ? "(verified)" : "(MISMATCH)");

    if (!vr.raw_sha1_match || !vr.overall_sha1_match)
    {
        std::printf("Header raw:   %s\n", vr.header_raw_sha1.c_str());
        std::printf("Header SHA1:  %s\n", vr.header_overall_sha1.c_str());

        if (args.fix)
            std::printf("SHA1 updated in header.\n");
        else
            std::printf("Use --fix to update the header SHA1.\n");

        log_info("verify: i=" + args.input + " SHA1 mismatch" + std::string(args.fix ? " (fixed)" : ""));
        return args.fix ? 0 : 1;
    }

    std::printf("Verification complete — integrity OK\n");
    log_info("verify OK: i=" + args.input);
    return 0;
}

// ======================> copy command (re-compress CHD)

static int cmd_copy(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: -i <input> is required for copy\n");
        return 1;
    }

    // Default output: stem_copy.chd
    std::string output = args.output;
    if (output.empty())
    {
        fs::path p(args.input);
        output = (p.parent_path() / (p.stem().string() + "_copy")).string() + ".chd";
    }

    if (fs::exists(output))
    {
        std::error_code ec;
        if (fs::equivalent(fs::path(args.input), fs::path(output), ec))
        {
            std::fprintf(stderr, "Error: input and output cannot be the same file\n");
            return 1;
        }
    }

    ArchiveOptions opts;
    opts.parent_chd_path = args.output_parent.empty() ? args.input_parent : args.output_parent;
    opts.hunk_bytes = args.hunk_size;
    opts.unit_bytes = args.unit_size;
    opts.num_processors = args.num_processors;
    opts.best = args.best;

    if (!args.compression.empty())
    {
        if (args.compression == "none")
        {
            std::fprintf(stderr, "Error: uncompressed CHD (-c none) is not yet supported\n");
            return 1;
        }
        else if (args.compression == "chdman")
        {
            opts.chdman_compat = true;
        }
        else {
        std::string s = args.compression;
        int slot = 0;
        size_t pos = 0;
        while (pos < s.size() && slot < 4)
        {
            auto comma = s.find(',', pos);
            std::string tok = (comma == std::string::npos)
                ? s.substr(pos) : s.substr(pos, comma - pos);
            while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
            while (!tok.empty() && tok.back() == ' ') tok.pop_back();
            for (auto& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            opts.compression[slot++] = parse_codec(tok);
            pos = (comma == std::string::npos) ? s.size() : comma + 1;
        }
        }
    }

    opts.progress_callback = make_progress("Compressing");

    std::printf("Input CHD:    %s\n", args.input.c_str());
    std::printf("Output CHD:   %s\n", output.c_str());
    if (opts.has_custom_compression())
        std::printf("Compression:  %s\n", codec_list_string(opts.compression).c_str());
    if (opts.hunk_bytes)
        std::printf("Hunk size:    %s bytes\n", big_int_string(opts.hunk_bytes).c_str());

    if (args.num_processors > 0)
        osd_num_processors = args.num_processors;

    ChdArchiver archiver;
    ArchiveResult result;
    try {
        result = archiver.copy(args.input, output, opts);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\nError: %s\n", e.what());
        log_error("copy: i=" + args.input + " o=" + output + " " + e.what());
        return 1;
    }

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error("copy: i=" + args.input + " o=" + output + " " + result.error_message);
        return 1;
    }

    std::printf("Input size:   %s bytes\n", big_int_string(result.input_bytes).c_str());
    std::printf("Output size:  %s bytes\n", big_int_string(result.output_bytes).c_str());
    std::printf("Ratio:        %.1f%%\n", result.compression_ratio * 100.0);
    std::printf("Copy complete\n");

    log_info("copy OK: i=" + args.input + " o=" + output + " bytes=" + std::to_string(result.output_bytes));
    return 0;
}

// ======================> dumpmeta command

static uint32_t parse_meta_tag(const std::string& s)
{
    if (s.size() != 4)
    {
        std::fprintf(stderr, "Error: metadata tag must be exactly 4 characters (e.g. CHTR, DVD )\n");
        return 0;
    }
    return (uint32_t(uint8_t(s[0])) << 24) | (uint32_t(uint8_t(s[1])) << 16)
         | (uint32_t(uint8_t(s[2])) << 8)  | uint32_t(uint8_t(s[3]));
}

static int cmd_dumpmeta(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: -i <input> is required for dumpmeta\n");
        return 1;
    }
    if (args.meta_tag.empty())
    {
        std::fprintf(stderr, "Error: -t <tag> is required for dumpmeta (e.g. -t CHTR)\n");
        return 1;
    }

    uint32_t tag = parse_meta_tag(args.meta_tag);
    if (tag == 0)
        return 1;

    ChdReader reader;
    try {
        reader.open(args.input);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error opening CHD: %s\n", e.what());
        log_error(std::string("dumpmeta: i=") + args.input + " " + e.what());
        return 1;
    }

    std::string data = reader.read_metadata(tag, args.meta_index);
    if (data.empty())
    {
        std::fprintf(stderr, "No metadata found for tag '%s' index %d\n",
                     args.meta_tag.c_str(), args.meta_index);
        return 1;
    }

    // If -o specified, write to file; otherwise print to stdout
    if (!args.output.empty())
    {
        std::ofstream out(args.output, std::ios::binary);
        if (!out)
        {
            std::fprintf(stderr, "Error: cannot write to '%s'\n", args.output.c_str());
            return 1;
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        std::printf("Wrote %zu bytes to %s\n", data.size(), args.output.c_str());
    }
    else
    {
        std::printf("%s\n", data.c_str());
    }

    log_info("dumpmeta OK: i=" + args.input + " tag=" + args.meta_tag + " index=" + std::to_string(args.meta_index));
    return 0;
}

// ======================> convertcue command

static int cmd_convertcue(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }
    if (args.cue_style < 0 || args.cue_style > 2)
    {
        std::fprintf(stderr, "Error: -style required (0=chdman, 1=redump, 2=redump+catalog)\n");
        return 1;
    }

    auto style = parse_cue_style(args.cue_style);

    std::string out_path = args.output.empty() ? args.input : args.output;

    try {
        convert_cue_file(args.input, out_path, style);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        log_error("convertcue: i=" + args.input + " " + e.what());
        return 1;
    }

    std::printf("Converted: %s -> %s (style: %s)\n",
                args.input.c_str(), out_path.c_str(), cue_style_name(style));
    log_info("convertcue OK: i=" + args.input + " o=" + out_path + " style=" + cue_style_name(style));
    return 0;
}

// ======================> auto command: .chd → extract, else → create

static bool is_auto_supported(const fs::path& p)
{
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".chd" || ext == ".cue" || ext == ".gdi" || ext == ".iso";
}

static int cmd_auto(const Args& args)
{
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

    std::string ext = fs::path(args.input).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".chd")
        return cmd_extract(args);
    else
        return cmd_create(args);
}

// Run cmd_auto on a list of paths (files and/or folders).
// Folders are scanned for supported files (.chd, .cue, .gdi, .iso).
// Processes files in parallel when multiple cores are available.
static int cmd_auto_batch(const Args& base_args, const std::vector<std::string>& paths)
{
    // Collect all files to process
    std::vector<fs::path> files;
    for (const auto& p : paths)
    {
        fs::path fp(p);
        if (fs::is_directory(fp))
        {
            for (const auto& entry : fs::directory_iterator(fp))
            {
                if (entry.is_regular_file() && is_auto_supported(entry.path()))
                    files.push_back(entry.path());
            }
        }
        else if (fs::is_regular_file(fp))
        {
            files.push_back(fp);
        }
        else
        {
            std::fprintf(stderr, "Skipping (not found): %s\n", p.c_str());
        }
    }

    std::sort(files.begin(), files.end());

    if (files.empty())
    {
        std::fprintf(stderr, "No supported files found.\n");
        return 1;
    }

    // Determine thread budget: divide cores among concurrent files
    int total_cores = static_cast<int>(std::thread::hardware_concurrency());
    if (total_cores <= 0) total_cores = 4;

    // Use user-specified -np as total thread budget if given
    if (base_args.num_processors > 0)
        total_cores = base_args.num_processors;

    int max_concurrent;
    if (base_args.max_concurrent_files > 0)
        max_concurrent = base_args.max_concurrent_files;
    else
        max_concurrent = std::max(1, std::min(4, total_cores / 4));
    int threads_per_file = std::max(2, total_cores / max_concurrent);

    // Single file → sequential path (full thread budget, with progress)
    if (files.size() == 1 || max_concurrent <= 1)
    {
        max_concurrent = 1;
        threads_per_file = total_cores;
    }

    // Set global thread budget before spawning any work
    osd_num_processors = threads_per_file;

    std::printf("Batch: %zu file(s), %d concurrent (%d threads each)\n\n",
                files.size(), max_concurrent, threads_per_file);

    if (max_concurrent <= 1)
    {
        // Sequential mode — full progress bars
        int ok = 0, fail = 0;
        for (const auto& f : files)
        {
            std::printf("--- [%d/%d] %s ---\n", ok + fail + 1, (int)files.size(), f.filename().string().c_str());
            Args a = base_args;
            a.input = f.string();
            a.command = "auto";
            a.num_processors = 0; // already set via osd_num_processors
            g_input_file = a.input;

            int rc = cmd_auto(a);
            if (rc == 0) { log_info("OK"); ++ok; }
            else { std::fprintf(stderr, "  FAILED: %s\n", f.string().c_str()); ++fail; }
            std::printf("\n");
        }
        std::printf("Batch complete: %d OK, %d failed, %d total\n", ok, fail, ok + fail);
        return fail > 0 ? 1 : 0;
    }

    // Parallel mode — worker threads pull from shared index
    std::atomic<int> next_index{0};
    std::atomic<int> ok{0}, fail{0};
    std::mutex io_mutex;

    auto worker = [&]() {
        for (;;)
        {
            int idx = next_index.fetch_add(1, std::memory_order_relaxed);
            if (idx >= static_cast<int>(files.size()))
                break;

            const auto& f = files[idx];
            {
                std::lock_guard<std::mutex> lk(io_mutex);
                std::printf("--- [%d/%d] %s ---\n", idx + 1, (int)files.size(), f.filename().string().c_str());
            }

            Args a = base_args;
            a.input = f.string();
            a.command = "auto";
            a.num_processors = 0; // already set via osd_num_processors

            int rc = cmd_auto(a);

            {
                std::lock_guard<std::mutex> lk(io_mutex);
                if (rc == 0)
                {
                    std::printf("  OK: %s\n", f.filename().string().c_str());
                    ++ok;
                }
                else
                {
                    std::fprintf(stderr, "  FAILED: %s\n", f.string().c_str());
                    ++fail;
                }
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(max_concurrent);
    for (int i = 0; i < max_concurrent; i++)
        threads.emplace_back(worker);
    for (auto& t : threads)
        t.join();

    std::printf("\nBatch complete: %d OK, %d failed, %d total\n",
                ok.load(), fail.load(), ok.load() + fail.load());
    return fail.load() > 0 ? 1 : 0;
}

// Determine default mode from argv[0]: "chdread" → read, "chdhash" → hash, "chdcomp" → comp, else → auto
enum class BinaryMode { Auto, Read, Hash, Comp };

static BinaryMode detect_binary_mode(const char* argv0)
{
    std::string name = fs::path(argv0).stem().string();
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name == "chdread")  return BinaryMode::Read;
    if (name == "chdhash")  return BinaryMode::Hash;
    if (name == "chdcomp")  return BinaryMode::Comp;
    return BinaryMode::Auto;
}

// ======================> main

int main(int argc, char** argv)
{
    const BinaryMode mode = detect_binary_mode(argv[0]);

    if (argc < 2)
    {
        print_usage();
        return 0;
    }

    if (std::strcmp(argv[1], "--version") == 0)
    {
        print_version();
        return 0;
    }

    init_log(argc, argv);

    auto args = parse_args(argc, argv);

    // Allow command-less flag form: `chdlite -i <path>` behaves like `chdlite <path>`.
    // If argv[1] is a flag and input is provided, infer command from binary mode.
    if (!args.input.empty() && !args.command.empty() && args.command.front() == '-')
    {
        if (mode == BinaryMode::Read)
            args.command = "read";
        else if (mode == BinaryMode::Hash)
            args.command = "hash";
        else
            args.command = "auto";
    }

    g_input_file = args.input;

    // Apply Comp mode: force --best if not conflict with create/extracting
    if (mode == BinaryMode::Comp && !args.best)
    {
        // Apply best compression for create/archive operations
        args.best = true;
    }

    // Apply --log-dir override (must happen before any log_entry calls)
    if (!args.log_dir.empty())
    {
        std::error_code ec;
        g_logs_dir = fs::path(args.log_dir);
        fs::create_directories(g_logs_dir, ec);
        // Reinitialise spdlog sink with new path
        std::string new_path = (g_logs_dir / "chdlite.log").string();
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(new_path, false);
        g_logger = std::make_shared<spdlog::logger>("chdlite", sink);
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");
        g_logger->set_level(spdlog::level::trace);
        g_logger->flush_on(spdlog::level::debug);
    }

    // Set log level from -log option
    if (!args.log_level.empty())
    {
        g_log_level = parse_log_level(args.log_level);
    }

    std::string cmd = args.command;

    // Lowercase the command
    for (auto& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    
    // Set for command-specific pretty log naming
    g_current_command = cmd;
    bool pretty_log_default = false;
    if (cmd == "read" || cmd == "info" || cmd == "hash")
    {
        pretty_log_default = true;
    }
    if (cmd == "extract" || cmd == "extractcd" || cmd == "extractdvd" || cmd == "extractraw" ||
        cmd == "create" || cmd == "createcd" || cmd == "createdvd" || cmd == "createraw" ||
        cmd == "createhd" || cmd == "extracthd" || cmd == "auto")
    {
        pretty_log_default = false;
    }

    // --result override: on/off or format-specific
    if (!args.result_format.empty())
    {
        std::string rf = args.result_format;
        for (auto& c : rf) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (rf == "on" || rf == "yes" || rf == "true" || rf == "1")
            g_pretty_log_enabled = true;
        else if (rf == "off" || rf == "no" || rf == "false" || rf == "0")
            g_pretty_log_enabled = false;
        else
            // For hash, treat as format specifier; for others, treat as on
            g_pretty_log_enabled = (cmd == "hash") || pretty_log_default;
    }
    else
    {
        g_pretty_log_enabled = pretty_log_default;
    }

    std::printf("chdlite - CHD disc image tool v%d.%d.%d\n",
                VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    try
    {
        // Generic commands
        if (cmd == "read" || cmd == "info")
        {
            int rc = cmd_read(args);
            if (is_standalone_launch()) pause_for_user();
            return rc;
        }
        if (cmd == "hash")
        {
            int rc = cmd_hash(args);
            if (is_standalone_launch()) pause_for_user();
            return rc;
        }
        if (cmd == "extract")                     return cmd_extract(args);
        if (cmd == "create")                      return cmd_create(args);
        if (cmd == "auto")                        return cmd_auto(args);

        // chdman-compatible commands
        if (cmd == "createcd")                    return cmd_create_typed(args, "cd");
        if (cmd == "createdvd")                   return cmd_create_typed(args, "dvd");
        if (cmd == "createraw" || cmd == "createhd") return cmd_create_typed(args, "raw");
        if (cmd == "extractcd")                   return cmd_extract_typed(args, "cd");
        if (cmd == "extractdvd")                  return cmd_extract_typed(args, "dvd");
        if (cmd == "extractraw")                  return cmd_extract_typed(args, "raw");
        if (cmd == "extracthd")                   return cmd_extract_typed(args, "hd");
        if (cmd == "convertcue")                   return cmd_convertcue(args);
        if (cmd == "verify")                       return cmd_verify(args);
        if (cmd == "copy")                         return cmd_copy(args);
        if (cmd == "dumpmeta")                     return cmd_dumpmeta(args);

        // Check if first arg is a file or directory (drag-and-drop / bare input)
        if (fs::exists(args.command))
        {
            // Collect all bare paths from argv (skip flags and their values)
            std::vector<std::string> paths;
            for (int i = 1; i < argc; i++)
            {
                std::string a = argv[i];
                if (a[0] == '-')
                {
                    // Skip flag and its value (if any)
                    if (a == "-log" || a == "--log") ++i;
                    continue;
                }
                paths.push_back(a);
            }

            // Binary mode overrides default drag-and-drop action
            if (mode == BinaryMode::Read || mode == BinaryMode::Hash)
            {
                const char* mode_cmd = (mode == BinaryMode::Read) ? "read" : "hash";
                int ok = 0, fail = 0;
                for (const auto& p : paths)
                {
                    if (!fs::is_regular_file(p)) {
                        std::fprintf(stderr, "Skipping (not a file): %s\n", p.c_str());
                        ++fail;
                        continue;
                    }
                    if (paths.size() > 1)
                        std::printf("--- %s ---\n", fs::path(p).filename().string().c_str());
                    Args a = args;
                    a.input = p;
                    a.command = mode_cmd;
                    g_input_file = a.input;
                    int rc = (mode == BinaryMode::Read) ? cmd_read(a) : cmd_hash(a);
                    if (rc == 0) { log_info("OK"); ++ok; } else ++fail;
                    if (paths.size() > 1) std::printf("\n");
                }
                if (paths.size() > 1)
                    std::printf("Done: %d OK, %d failed, %d total\n", ok, fail, ok + fail);
                if (is_standalone_launch()) pause_for_user();
                return fail > 0 ? 1 : 0;
            }

            // chdlite mode: auto (extract/create)
            if (paths.size() == 1 && fs::is_regular_file(paths[0]))
            {
                // Single file: direct auto
                args.input = paths[0];
                g_input_file = args.input;
                args.command = "auto";
                return cmd_auto(args);
            }
            else
            {
                // Multiple files and/or folders: batch auto
                return cmd_auto_batch(args, paths);
            }
        }

        std::fprintf(stderr, "Unknown command: %s\n\n", args.command.c_str());
        print_usage();
        return 1;
    }
    catch (const ChdCancelledException& e)
    {
        std::fprintf(stderr, "Cancelled: %s\n", e.what());
        log_entry(LogLevel::Info, std::string("cancelled: ") + e.what());
        return 2;
    }
    catch (const ChdException& e)
    {
        std::string err = std::string("CHD exception: ") + e.what();
        std::fprintf(stderr, "%s\n", err.c_str());
        log_entry(e.severity(), err);
        return 1;
    }
    catch (const std::exception& e)
    {
        std::string err = std::string("exception: ") + e.what();
        std::fprintf(stderr, "Error: %s\n", e.what());
        log_error(err);
        return 1;
    }
}
