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

// ======================> Beta error log (always on until release)
// Appends to chdlite_errors.log next to the binary or cwd.
// Records: timestamp, version, full command line, error message.
// Users share this file for bug reports.

static std::string g_cmdline;       // full command line for logging
static std::string g_input_file;    // resolved after parse
static std::string g_log_path;      // path to error log file

static void init_error_log(int argc, char** argv)
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

    // Log file: next to the executable, fallback to cwd
    std::error_code ec;
    fs::path exe_dir = fs::canonical(fs::path(argv[0]).parent_path(), ec);
    if (ec || exe_dir.empty())
        g_log_path = "chdlite_errors.log";
    else
        g_log_path = (exe_dir / "chdlite_errors.log").string();
}

static void log_error(const std::string& error_msg)
{
    // Timestamp
    std::time_t now = std::time(nullptr);
    char timebuf[64];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    std::ofstream log(g_log_path, std::ios::app);
    if (!log.is_open()) return;   // can't write log, silently skip

    log << "---\n";
    log << "Time:    " << timebuf << "\n";
    log << "Version: " << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << "\n";
    log << "Command: " << g_cmdline << "\n";
    if (!g_input_file.empty())
        log << "Input:   " << g_input_file << "\n";
    log << "Error:   " << error_msg << "\n";
    log << std::endl;

    // Tell the user
    std::fprintf(stderr, "(error logged to %s)\n", g_log_path.c_str());
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
    // "read" / "info" — display CHD header, tracks, metadata, system detection
    if (args.input.empty())
    {
        std::fprintf(stderr, "Error: no input file specified\n");
        return 1;
    }

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

    // System detection
    auto det = reader.detect_system(DetectFlags::All, true);
    if (det.system != CdSystem::Unknown)
    {
        std::printf("System:       %s\n", system_name(det.system));
        if (!det.title.empty())
            std::printf("Title:        %s\n", det.title.c_str());
        if (!det.game_id.empty())
            std::printf("Game ID:      %s\n", det.game_id.c_str());
    }

    return 0;
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
    if (result.detected_system != CdSystem::Unknown)
        std::printf("System:       %s\n", system_name(result.detected_system));
    if (!result.detected_title.empty())
        std::printf("Title:        %s\n", result.detected_title.c_str());
    if (!result.detected_gameid.empty())
        std::printf("Game ID:      %s\n", result.detected_gameid.c_str());
    std::printf("Compression complete\n");

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

    return 0;
}

// ======================> auto command: .chd → extract, else → create

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

// ======================> main

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        print_usage();
        return 0;
    }

    init_error_log(argc, argv);

    auto args = parse_args(argc, argv);
    g_input_file = args.input;
    std::string cmd = args.command;

    // Lowercase the command
    for (auto& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::printf("chdlite - CHD disc image tool v%d.%d.%d\n",
                VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    try
    {
        // Generic commands
        if (cmd == "read" || cmd == "info")       return cmd_read(args);
        if (cmd == "hash")                        return cmd_hash(args);
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

        // Check if first arg is a file (drag-and-drop / bare input)
        if (fs::exists(args.command))
        {
            args.input = args.command;
            g_input_file = args.input;
            args.command = "auto";
            return cmd_auto(args);
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
