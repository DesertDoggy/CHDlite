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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace chdlite;

// ======================> Activity log
// Appends to chdlite.log next to the binary or cwd.
// Records: timestamp, version, full command line, status (OK or error).
// Default level per release phase:  alpha=info  beta=error  release=none
// Override with -log info|error|none

enum class LogLevel { Info = 0, Error = 1, None = 2 };

static constexpr LogLevel LOG_DEFAULT = LogLevel::Info;  // ← change per phase

static LogLevel     g_log_level = LOG_DEFAULT;
static std::string  g_cmdline;
static std::string  g_input_file;
static std::string  g_log_path;
static fs::path     g_logs_dir;   // <exe_root>/logs/  (populated by init_log)

static void init_log(int argc, char** argv)
{
    // Build full command line string
    for (int i = 0; i < argc; i++)
    {
        if (i > 0) g_cmdline += ' ';
        // Quote args with spaces
        std::string a = argv[i];
        if (a.find(' ') != std::string::npos)
            g_cmdline += '"' + a + '"';
        else
            g_cmdline += a;
    }

    // Resolve <exe_root>/logs/ — create it if absent; fall back to ./logs/
    std::error_code ec;
    fs::path exe_dir = fs::canonical(fs::path(argv[0]).parent_path(), ec);
    g_logs_dir = (ec || exe_dir.empty()) ? fs::path("logs") : exe_dir / "logs";
    fs::create_directories(g_logs_dir, ec);  // silently ignore "already exists" etc.

    g_log_path = (g_logs_dir / "chdlite.log").string();
}

static void log_entry(LogLevel level, const std::string& status)
{
    if (g_log_level == LogLevel::None) return;
    if (level < g_log_level) return;

    std::time_t now = std::time(nullptr);
    char timebuf[64];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    std::ofstream log(g_log_path, std::ios::app);
    if (!log.is_open()) return;

    log << "---\n";
    log << "Time:    " << timebuf << "\n";
    log << "Version: " << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << "\n";
    log << "Command: " << g_cmdline << "\n";
    if (!g_input_file.empty())
        log << "Input:   " << g_input_file << "\n";
    log << "Status:  " << status << "\n";
    log << std::endl;
}

static void log_info(const std::string& msg)   { log_entry(LogLevel::Info, msg); }
static void log_error(const std::string& msg)  { log_entry(LogLevel::Error, "ERROR: " + msg); }

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
static auto make_progress(const char* verb)
{
    auto last_time = std::chrono::steady_clock::now();
    return [verb, last_time](uint64_t done, uint64_t total) mutable -> bool
    {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (elapsed >= 500 || done >= total)
        {
            double pct = total ? 100.0 * done / total : 100.0;
            std::fprintf(stderr, "\r%s, %.1f%% complete...  ", verb, pct);
            std::fflush(stderr);
            last_time = now;
        }
        if (done >= total)
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
    std::string log_level;              // -log info|error|none
    std::string log_dir;                // --log-dir <path>        override activity log directory
    std::string hash_dir;               // --hash-dir <path|"disc"> override .hashes output dir
                                        //   "disc" = next to input file
};

static void print_usage()
{
    std::printf(
        "chdlite - CHD disc image tool v%d.%d.%d\n"
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
        "  createcd   -i <input> -o <output.chd> [opts]   Create CD CHD\n"
        "  createdvd  -i <input> -o <output.chd> [opts]   Create DVD CHD\n"
        "  createraw  -i <input> -o <output.chd> [opts]   Create raw CHD\n"
        "  createhd   -i <input> -o <output.chd> [opts]   Create hard disk CHD\n"
        "  extractcd  -i <input.chd> -o <output> [opts]   Extract CD from CHD\n"
        "  extractdvd -i <input.chd> -o <output> [opts]   Extract DVD from CHD\n"
        "  extractraw -i <input.chd> -o <output> [opts]   Extract raw from CHD\n"
        "  extracthd  -i <input.chd> -o <output> [opts]   Extract hard disk from CHD\n"
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
        "  -log <level>                Log level: info, error, none (default: info)\n"
        "  --log-dir <path>            Directory for chdlite.log  (default: <exe>/logs/)\n"
        "  -v, --verbose               Verbose output\n"
        "\n",
        VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH
    );
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
        else if (arg == "-np" || arg == "--numprocessors") a.num_processors = std::stoi(next());
        else if (arg == "-isb" || arg == "--inputstartbyte")  a.input_start_byte = std::stoull(next());
        else if (arg == "-ish" || arg == "--inputstarthunk")  a.input_start_hunk = std::stoull(next());
        else if (arg == "-ib" || arg == "--inputbytes")       a.input_bytes = std::stoull(next());
        else if (arg == "-ih" || arg == "--inputhunks")       a.input_hunks = std::stoull(next());
        else if (arg == "-isf" || arg == "--inputstartframe") a.input_start_frame = std::stoull(next());
        else if (arg == "-if" || arg == "--inputframes")      a.input_frames = std::stoull(next());
        else if (arg == "-log" || arg == "--log")       a.log_level = next();
        else if (arg == "--log-dir")                    a.log_dir = next();
        else if (arg == "--hash-dir")                   a.hash_dir = next();
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
                        return 0;
                    }
                }
            }
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
        log_info("read OK");
        return 0;
    }

    // CHD path: open with ChdReader and display full CHD info
    ChdReader reader;
    try {
        reader.open(args.input);
    } catch (const std::exception& e) {
        std::string err = std::string("opening CHD: ") + e.what();
        std::fprintf(stderr, "Error %s\n", err.c_str());
        log_error(err);
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

    log_info("read OK");
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
    auto result = reader.hash_content(flags);
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    if (!result.success)
    {
        std::fprintf(stderr, "Error: %s\n", result.error_message.c_str());
        log_error("hash: " + result.error_message);
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

    log_info("hash OK: " + std::to_string(result.tracks.size()) + " tracks");
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

    // Split-bin: default true, overridable with --no-splitbin
    opts.split_bin = args.no_splitbin ? false : true;

    // Determine output path
    if (!args.output.empty())
    {
        fs::path out(args.output);
        opts.output_dir = out.parent_path().string();
        opts.output_filename = out.filename().string();

        // Detect force_raw / force_bin_cue from output extension
        std::string ext = out.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".bin") opts.force_raw = true;
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
    auto result = extractor.extract(args.input, opts);

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error("extract: " + result.error_message);
        return 1;
    }

    std::printf("Content Type: %s\n", content_type_name(result.detected_type));
    std::printf("Output:       %s\n", result.output_path.c_str());
    for (auto& f : result.output_files)
        std::printf("  -> %s\n", f.c_str());
    std::printf("Bytes:        %s\n", big_int_string(result.bytes_written).c_str());
    std::printf("Extraction complete\n");

    log_info("extract OK: " + std::to_string(result.bytes_written) + " bytes");
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

    // Parse compression
    if (!args.compression.empty())
    {
        if (args.compression == "none")
        {
            opts.codec = Codec::None;
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

    // Progress callback
    opts.progress_callback = make_progress("Compressing");

    // Print header info
    std::printf("Output CHD:   %s\n", output.c_str());
    std::printf("Input file:   %s\n", args.input.c_str());
    if (!args.input_parent.empty())
        std::printf("Parent CHD:   %s\n", args.input_parent.c_str());
    if (opts.has_custom_compression())
        std::printf("Compression:  %s\n", codec_list_string(opts.compression).c_str());
    else
        std::printf("Compression:  auto (smart defaults)\n");
    if (opts.hunk_bytes)
        std::printf("Hunk size:    %s bytes\n", big_int_string(opts.hunk_bytes).c_str());

    ChdArchiver archiver;
    auto result = archiver.archive(args.input, output, opts);

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error("create: " + result.error_message);
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

    log_info("create OK: " + std::to_string(result.output_bytes) + " bytes");
    return 0;
}

static int cmd_create_typed(const Args& args, const char* type_hint)
{
    // Force a specific archive path: createcd, createdvd, createraw, createhd
    if (args.input.empty() || args.output.empty())
    {
        std::fprintf(stderr, "Error: both -i <input> and -o <output> are required for %s\n", type_hint);
        return 1;
    }

    ArchiveOptions opts;
    opts.parent_chd_path = args.input_parent;
    opts.hunk_bytes = args.hunk_size;
    opts.unit_bytes = args.unit_size;
    opts.num_processors = args.num_processors;
    opts.detect_title = true;

    if (!args.compression.empty())
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

    opts.progress_callback = make_progress("Compressing");

    std::printf("Output CHD:   %s\n", args.output.c_str());
    std::printf("Input file:   %s\n", args.input.c_str());
    if (!args.input_parent.empty())
        std::printf("Parent CHD:   %s\n", args.input_parent.c_str());
    if (opts.has_custom_compression())
        std::printf("Compression:  %s\n", codec_list_string(opts.compression).c_str());
    if (opts.hunk_bytes)
        std::printf("Hunk size:    %s bytes\n", big_int_string(opts.hunk_bytes).c_str());

    ChdArchiver archiver;
    ArchiveResult result;

    std::string th = type_hint;
    if (th == "cd")        result = archiver.archive_cd(args.input, args.output, opts);
    else if (th == "dvd")  result = archiver.archive_dvd(args.input, args.output, opts);
    else                   result = archiver.archive_raw(args.input, args.output, opts);

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error(std::string(type_hint) + ": " + result.error_message);
        return 1;
    }

    std::printf("Input size:   %s bytes\n", big_int_string(result.input_bytes).c_str());
    std::printf("Output size:  %s bytes\n", big_int_string(result.output_bytes).c_str());
    std::printf("Ratio:        %.1f%%\n", result.compression_ratio * 100.0);
    std::printf("Compression complete\n");

    log_info(std::string(type_hint) + " OK: " + std::to_string(result.output_bytes) + " bytes");
    return 0;
}

static int cmd_extract_typed(const Args& args, const char* type_hint)
{
    // extractcd, extractdvd, extractraw, extracthd
    if (args.input.empty() || args.output.empty())
    {
        std::fprintf(stderr, "Error: both -i <input> and -o <output> are required for extract%s\n", type_hint);
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

    fs::path out(args.output);
    opts.output_dir = out.parent_path().string();
    opts.output_filename = out.filename().string();

    std::string th = type_hint;
    if (th == "raw" || th == "hd") opts.force_raw = true;

    opts.progress_callback = make_progress("Extracting");

    std::printf("Input CHD:    %s\n", args.input.c_str());
    if (!args.input_parent.empty())
        std::printf("Parent CHD:   %s\n", args.input_parent.c_str());
    std::printf("Output file:  %s\n", args.output.c_str());
    if (opts.input_start_byte || opts.input_start_hunk)
        std::printf("Input start:  %s\n",
            opts.input_start_byte ? big_int_string(opts.input_start_byte).c_str()
                                  : (big_int_string(opts.input_start_hunk) + " hunks").c_str());
    if (opts.input_bytes || opts.input_hunks)
        std::printf("Input length: %s\n",
            opts.input_bytes ? big_int_string(opts.input_bytes).c_str()
                             : (big_int_string(opts.input_hunks) + " hunks").c_str());

    ChdExtractor extractor;
    auto result = extractor.extract(args.input, opts);

    if (!result.success)
    {
        std::fprintf(stderr, "\nError: %s\n", result.error_message.c_str());
        log_error(std::string("extract") + type_hint + ": " + result.error_message);
        return 1;
    }

    std::printf("Content Type: %s\n", content_type_name(result.detected_type));
    for (auto& f : result.output_files)
        std::printf("  -> %s\n", f.c_str());
    std::printf("Bytes:        %s\n", big_int_string(result.bytes_written).c_str());
    std::printf("Extraction complete\n");

    log_info(std::string("extract") + type_hint + " OK: " + std::to_string(result.bytes_written) + " bytes");
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

    std::printf("Batch: %zu file(s) to process\n\n", files.size());

    int ok = 0, fail = 0;
    for (const auto& f : files)
    {
        std::printf("--- [%d/%d] %s ---\n", ok + fail + 1, (int)files.size(), f.filename().string().c_str());
        Args a = base_args;
        a.input = f.string();
        a.command = "auto";
        g_input_file = a.input;

        int rc = cmd_auto(a);
        if (rc == 0)
        {
            log_info("OK");
            ++ok;
        }
        else
        {
            std::fprintf(stderr, "  FAILED: %s\n", f.string().c_str());
            ++fail;
        }
        std::printf("\n");
    }

    std::printf("Batch complete: %d OK, %d failed, %d total\n", ok, fail, ok + fail);
    return fail > 0 ? 1 : 0;
}

// Determine default mode from argv[0]: "chdread" → read, "chdhash" → hash, else → auto
enum class BinaryMode { Auto, Read, Hash };

static BinaryMode detect_binary_mode(const char* argv0)
{
    std::string name = fs::path(argv0).stem().string();
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name == "chdread")  return BinaryMode::Read;
    if (name == "chdhash")  return BinaryMode::Hash;
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

    init_log(argc, argv);

    auto args = parse_args(argc, argv);
    g_input_file = args.input;

    // Apply --log-dir override (must happen before any log_entry calls)
    if (!args.log_dir.empty())
    {
        std::error_code ec;
        g_logs_dir = fs::path(args.log_dir);
        fs::create_directories(g_logs_dir, ec);
        g_log_path = (g_logs_dir / "chdlite.log").string();
    }

    // Set log level from -log option
    if (!args.log_level.empty())
    {
        std::string ll = args.log_level;
        for (auto& c : ll) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ll == "info")        g_log_level = LogLevel::Info;
        else if (ll == "error")  g_log_level = LogLevel::Error;
        else if (ll == "none" || ll == "off") g_log_level = LogLevel::None;
    }

    std::string cmd = args.command;

    // Lowercase the command
    for (auto& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

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
    catch (const ChdException& e)
    {
        std::string err = std::string("CHD exception: ") + e.what();
        std::fprintf(stderr, "%s\n", err.c_str());
        log_error(err);
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
