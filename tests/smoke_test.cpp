// CHDlite - Smoke test
// Tests read, header, and extraction against real CHD files.

#include "chd_api.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static FILE* g_log = nullptr;

static void log_print(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_print(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);

    if (g_log) {
        va_start(args, fmt);
        std::vfprintf(g_log, fmt, args);
        va_end(args);
    }
}

static const char* content_type_str(chdlite::ContentType ct)
{
    switch (ct) {
    case chdlite::ContentType::HardDisk:  return "HardDisk";
    case chdlite::ContentType::CDROM:     return "CD-ROM";
    case chdlite::ContentType::GDROM:     return "GD-ROM";
    case chdlite::ContentType::DVD:       return "DVD";
    case chdlite::ContentType::LaserDisc: return "LaserDisc";
    case chdlite::ContentType::Raw:       return "Raw";
    default:                              return "Unknown";
    }
}

static const char* track_type_str(chdlite::TrackType tt)
{
    switch (tt) {
    case chdlite::TrackType::Mode1:        return "MODE1";
    case chdlite::TrackType::Mode1Raw:     return "MODE1_RAW";
    case chdlite::TrackType::Mode2:        return "MODE2";
    case chdlite::TrackType::Mode2Form1:   return "MODE2_FORM1";
    case chdlite::TrackType::Mode2Form2:   return "MODE2_FORM2";
    case chdlite::TrackType::Mode2FormMix: return "MODE2_FORM_MIX";
    case chdlite::TrackType::Mode2Raw:     return "MODE2_RAW";
    case chdlite::TrackType::Audio:        return "AUDIO";
    default:                               return "???";
    }
}

struct TestResult
{
    std::string chd_path;
    bool read_ok = false;
    bool extract_ok = false;
    std::string error;
};

static TestResult test_one_chd(const std::string& chd_path, const std::string& output_dir)
{
    TestResult tr;
    tr.chd_path = chd_path;

    std::string name = fs::path(chd_path).stem().string();
    log_print("\n========================================\n");
    log_print("FILE: %s\n", chd_path.c_str());
    log_print("========================================\n");

    // --- is_chd_file ---
    bool is_chd = chdlite::is_chd_file(chd_path);
    log_print("  is_chd_file: %s\n", is_chd ? "true" : "false");
    if (!is_chd) {
        tr.error = "Not a CHD file";
        return tr;
    }

    // --- Reader: open + header ---
    chdlite::ChdReader reader;
    try {
        reader.open(chd_path);
    } catch (const chdlite::ChdException& e) {
        tr.error = std::string("open failed: ") + e.what();
        log_print("  OPEN FAILED: %s\n", e.what());
        return tr;
    }

    chdlite::ChdHeader hdr;
    try {
        hdr = reader.read_header();
    } catch (const chdlite::ChdException& e) {
        tr.error = std::string("read_header failed: ") + e.what();
        log_print("  READ_HEADER FAILED: %s\n", e.what());
        return tr;
    }

    log_print("  Version:      %u\n", hdr.version);
    log_print("  Content Type: %s\n", content_type_str(hdr.content_type));
    log_print("  Logical:      %llu bytes\n", (unsigned long long)hdr.logical_bytes);
    log_print("  Hunk:         %u bytes x %u hunks\n", hdr.hunk_bytes, hdr.hunk_count);
    log_print("  Unit:         %u bytes\n", hdr.unit_bytes);
    log_print("  Compressed:   %s\n", hdr.compressed ? "yes" : "no");
    log_print("  Has Parent:   %s\n", hdr.has_parent ? "yes" : "no");
    log_print("  SHA-1:        %s\n", hdr.sha1.c_str());
    log_print("  Raw SHA-1:    %s\n", hdr.raw_sha1.c_str());

    if (hdr.content_type == chdlite::ContentType::CDROM || hdr.content_type == chdlite::ContentType::GDROM) {
        log_print("  Tracks:       %u  (GD-ROM: %s)\n", hdr.num_tracks, hdr.is_gdrom ? "yes" : "no");
        for (const auto& t : hdr.tracks) {
            log_print("    Track %02u: type=%-14s frames=%-6u data=%u sub=%u %s\n",
                t.track_number, track_type_str(t.type),
                t.frames, t.data_size, t.sub_size,
                t.is_audio ? "[AUDIO]" : "");
        }
    }

    tr.read_ok = true;
    reader.close();

    // --- Extractor ---
    log_print("  Extracting...\n");

    // Create per-file output subfolder
    std::string file_out_dir = (fs::path(output_dir) / name).string();
    fs::create_directories(file_out_dir);

    chdlite::ExtractOptions ext_opts;
    ext_opts.output_dir = file_out_dir;
    ext_opts.progress_callback = [](uint64_t done, uint64_t total) -> bool {
        // silent — just don't cancel
        return true;
    };

    chdlite::ChdExtractor extractor;

    auto t0 = std::chrono::steady_clock::now();
    auto ext_result = extractor.extract(chd_path, ext_opts);
    auto t1 = std::chrono::steady_clock::now();
    double elapsed_sec = std::chrono::duration<double>(t1 - t0).count();

    if (ext_result.success) {
        tr.extract_ok = true;
        log_print("  Extract OK (%.1fs, %llu bytes)\n", elapsed_sec,
            (unsigned long long)ext_result.bytes_written);
        log_print("  Output: %s\n", ext_result.output_path.c_str());
        for (const auto& f : ext_result.output_files)
            log_print("    -> %s\n", f.c_str());
    } else {
        tr.error = "extract failed: " + ext_result.error_message;
        log_print("  EXTRACT FAILED: %s\n", ext_result.error_message.c_str());
    }

    return tr;
}

int main(int argc, char* argv[])
{
    std::string rom_dir = "test_root/Roms/DiscRomsChd";
    std::string output_dir = "test_root/output";

    if (argc >= 2) rom_dir = argv[1];
    if (argc >= 3) output_dir = argv[2];

    // Setup output dir and log file
    fs::create_directories(output_dir);
    std::string log_path = (fs::path(output_dir) / "smoke_test.log").string();
    g_log = std::fopen(log_path.c_str(), "w");

    log_print("CHDlite Smoke Test v%d.%d.%d\n",
        chdlite::VERSION_MAJOR, chdlite::VERSION_MINOR, chdlite::VERSION_PATCH);
    log_print("ROM dir:    %s\n", rom_dir.c_str());
    log_print("Output dir: %s\n", output_dir.c_str());

    // Find all .chd files recursively
    std::vector<std::string> chd_files;
    for (const auto& entry : fs::recursive_directory_iterator(rom_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".chd")
            chd_files.push_back(entry.path().string());
    }

    std::sort(chd_files.begin(), chd_files.end());
    log_print("Found %zu CHD files.\n", chd_files.size());

    // Run tests
    std::vector<TestResult> results;
    for (const auto& chd : chd_files)
        results.push_back(test_one_chd(chd, output_dir));

    // Summary
    int pass_read = 0, pass_extract = 0, fail = 0;
    for (const auto& r : results) {
        if (r.read_ok) pass_read++;
        if (r.extract_ok) pass_extract++;
        if (!r.read_ok || !r.extract_ok) fail++;
    }

    log_print("\n========================================\n");
    log_print("SUMMARY: %zu files tested\n", results.size());
    log_print("  Read OK:     %d / %zu\n", pass_read, results.size());
    log_print("  Extract OK:  %d / %zu\n", pass_extract, results.size());
    log_print("  Failures:    %d\n", fail);

    if (fail > 0) {
        log_print("\nFailed files:\n");
        for (const auto& r : results) {
            if (!r.read_ok || !r.extract_ok)
                log_print("  %s\n    %s\n", r.chd_path.c_str(), r.error.c_str());
        }
    }

    log_print("========================================\n");

    if (g_log) std::fclose(g_log);
    return fail > 0 ? 1 : 0;
}
