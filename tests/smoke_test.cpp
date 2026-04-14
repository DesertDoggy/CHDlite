// CHDlite - Smoke test
// Tests read, header, and extraction against real CHD files.

#include "chd_api.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
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
    bool hash_ok = false;
    int hash_tracks_matched = 0;
    int hash_tracks_total = 0;
    std::string error;
};

// Reference hashes loaded from chd-content-hashes file
// Map: filename -> { sha1, md5, crc32 }
struct RefHash { std::string sha1; std::string md5; std::string crc32; };
static std::map<std::string, RefHash> g_ref_hashes;

static void load_reference_hashes(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    // skip header
    if (!std::getline(f, line)) return;
    while (std::getline(f, line)) {
        // tab-separated: filename sha1 md5 crc32 ...
        std::vector<std::string> cols;
        size_t pos = 0;
        while (pos < line.size()) {
            auto tab = line.find('\t', pos);
            if (tab == std::string::npos) {
                cols.push_back(line.substr(pos));
                break;
            }
            cols.push_back(line.substr(pos, tab - pos));
            pos = tab + 1;
        }
        if (cols.size() >= 4 && !cols[0].empty() && cols[1].size() == 40) {
            RefHash rh;
            rh.sha1 = cols[1];
            rh.md5 = cols[2];
            rh.crc32 = cols[3];
            g_ref_hashes[cols[0]] = rh;
        }
    }
}

// Lookup reference hash by filename, trying both "Track N" and "Track 0N" formats
static const RefHash* find_ref_hash(const std::string& name)
{
    auto it = g_ref_hashes.find(name);
    if (it != g_ref_hashes.end()) return &it->second;

    // Try zero-padded track number: "Track 1" -> "Track 01"
    std::string padded = name;
    std::string pattern = "(Track ";
    auto tpos = padded.find(pattern);
    if (tpos != std::string::npos) {
        auto numstart = tpos + pattern.size();
        auto paren = padded.find(')', numstart);
        if (paren != std::string::npos) {
            std::string numstr = padded.substr(numstart, paren - numstart);
            if (numstr.size() == 1) {
                padded = padded.substr(0, numstart) + "0" + numstr + padded.substr(paren);
                it = g_ref_hashes.find(padded);
                if (it != g_ref_hashes.end()) return &it->second;
            }
        }
    }
    return nullptr;
}

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

    // --- Content hashing ---
    log_print("  Hashing content...\n");
    {
        auto t0h = std::chrono::steady_clock::now();
        auto hash_result = reader.hash_content();
        auto t1h = std::chrono::steady_clock::now();
        double hash_sec = std::chrono::duration<double>(t1h - t0h).count();

        if (hash_result.success) {
            log_print("  Hash OK (%.1fs, %zu tracks)\n", hash_sec, hash_result.tracks.size());
            tr.hash_tracks_total = (int)hash_result.tracks.size();

            // Determine the filename pattern for looking up reference hashes
            std::string stem = fs::path(chd_path).stem().string();
            bool is_cd = (hash_result.content_type == chdlite::ContentType::CDROM ||
                          hash_result.content_type == chdlite::ContentType::GDROM);

            for (const auto& thr : hash_result.tracks) {
                std::string ref_name;
                if (!is_cd) {
                    // DVD/raw: reference uses .iso extension
                    ref_name = stem + ".iso";
                    // Also try .bin if .iso not found
                } else if (hash_result.tracks.size() == 1) {
                    // Single-track CD: "Name (Track 1).bin" in reference
                    ref_name = stem + " (Track 1).bin";
                } else {
                    // Multi-track: check GD-ROM audio = .raw
                    bool gdi = (hash_result.content_type == chdlite::ContentType::GDROM);
                    std::string ext = (gdi && thr.is_audio) ? ".raw" : ".bin";
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%02u", thr.track_number);
                    // Try "(Track NN)" format first, then "stemNN" format
                    ref_name = stem + " (Track " + buf + ")" + ext;
                }

                const RefHash* rh = find_ref_hash(ref_name);
                // Try alternate naming formats used by different tools
                if (!rh && is_cd && hash_result.tracks.size() > 1) {
                    bool gdi = (hash_result.content_type == chdlite::ContentType::GDROM);
                    std::string ext = (gdi && thr.is_audio) ? ".raw" : ".bin";
                    char buf_pad[16], buf_nopad[16];
                    std::snprintf(buf_pad, sizeof(buf_pad), "%02u", thr.track_number);
                    std::snprintf(buf_nopad, sizeof(buf_nopad), "%u", thr.track_number);
                    // "stem (Track N).ext" unpadded
                    if (!rh) rh = find_ref_hash(stem + " (Track " + buf_nopad + ")" + ext);
                    // "stemNN.ext" direct append padded
                    if (!rh) rh = find_ref_hash(stem + buf_pad + ext);
                    // "stemN.ext" direct append unpadded
                    if (!rh) rh = find_ref_hash(stem + buf_nopad + ext);
                }
                // Also try without zero-padded track for DVD
                if (!rh && !is_cd) {
                    ref_name = stem + ".bin";
                    rh = find_ref_hash(ref_name);
                }

                if (rh) {
                    bool sha_match = (thr.sha1.hex_string == rh->sha1);
                    bool md5_match = (thr.md5.hex_string == rh->md5);
                    bool crc_match = (thr.crc32.hex_string == rh->crc32);

                    if (sha_match && md5_match && crc_match) {
                        log_print("    Track %02u: SHA1=%s  ALL MATCH\n",
                            thr.track_number, thr.sha1.hex_string.c_str());
                        tr.hash_tracks_matched++;
                    } else {
                        log_print("    Track %02u: MISMATCH\n", thr.track_number);
                        log_print("      SHA1: %s %s (ref: %s)\n",
                            thr.sha1.hex_string.c_str(), sha_match ? "OK" : "FAIL", rh->sha1.c_str());
                        log_print("      MD5:  %s %s (ref: %s)\n",
                            thr.md5.hex_string.c_str(), md5_match ? "OK" : "FAIL", rh->md5.c_str());
                        log_print("      CRC:  %s %s (ref: %s)\n",
                            thr.crc32.hex_string.c_str(), crc_match ? "OK" : "FAIL", rh->crc32.c_str());
                    }
                } else {
                    log_print("    Track %02u: SHA1=%s  MD5=%s  CRC=%s  (no ref: %s)\n",
                        thr.track_number, thr.sha1.hex_string.c_str(),
                        thr.md5.hex_string.c_str(), thr.crc32.hex_string.c_str(),
                        ref_name.c_str());
                }
            }

            tr.hash_ok = (tr.hash_tracks_matched == tr.hash_tracks_total) && (tr.hash_tracks_total > 0);
            if (!tr.hash_ok && tr.hash_tracks_matched > 0)
                log_print("  Hash partial: %d/%d tracks matched\n", tr.hash_tracks_matched, tr.hash_tracks_total);

            // Display sheet hash (CUE/GDI) if present
            if (!hash_result.sheet_content.empty()) {
                std::string sheet_type = (hash_result.content_type == chdlite::ContentType::GDROM) ? "GDI" : "CUE";
                log_print("  %s sheet (%zu bytes):\n", sheet_type.c_str(), hash_result.sheet_content.size());
                log_print("    SHA1: %s\n", hash_result.sheet_hash.sha1.hex_string.c_str());
                log_print("    MD5:  %s\n", hash_result.sheet_hash.md5.hex_string.c_str());
                log_print("    CRC:  %s\n", hash_result.sheet_hash.crc32.hex_string.c_str());
            }
        } else {
            log_print("  HASH FAILED: %s\n", hash_result.error_message.c_str());
        }
    }

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

    std::string filter;
    if (argc >= 2) rom_dir = argv[1];
    if (argc >= 3) output_dir = argv[2];
    if (argc >= 4) filter = argv[3];

    // Setup output dir and log file
    fs::create_directories(output_dir);
    std::string log_path = (fs::path(output_dir) / "smoke_test.log").string();
    g_log = std::fopen(log_path.c_str(), "w");

    log_print("CHDlite Smoke Test v%d.%d.%d\n",
        chdlite::VERSION_MAJOR, chdlite::VERSION_MINOR, chdlite::VERSION_PATCH);
    log_print("ROM dir:    %s\n", rom_dir.c_str());
    log_print("Output dir: %s\n", output_dir.c_str());

    // Load reference hashes
    std::string ref_hash_path = (fs::path(rom_dir) / ".." / "chd-content-hashes").string();
    load_reference_hashes(ref_hash_path);
    if (!g_ref_hashes.empty())
        log_print("Loaded %zu reference hashes from %s\n", g_ref_hashes.size(), ref_hash_path.c_str());

    // Find all .chd files recursively
    std::vector<std::string> chd_files;
    for (const auto& entry : fs::recursive_directory_iterator(rom_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".chd") {
            if (!filter.empty()) {
                std::string p = entry.path().string();
                if (p.find(filter) == std::string::npos)
                    continue;
            }
            chd_files.push_back(entry.path().string());
        }
    }

    std::sort(chd_files.begin(), chd_files.end());
    log_print("Found %zu CHD files.\n", chd_files.size());

    // Run tests
    std::vector<TestResult> results;
    for (const auto& chd : chd_files)
        results.push_back(test_one_chd(chd, output_dir));

    // Summary
    int pass_read = 0, pass_extract = 0, pass_hash = 0, fail = 0;
    int total_tracks_matched = 0, total_tracks = 0;
    for (const auto& r : results) {
        if (r.read_ok) pass_read++;
        if (r.extract_ok) pass_extract++;
        if (r.hash_ok) pass_hash++;
        if (!r.read_ok || !r.extract_ok) fail++;
        total_tracks_matched += r.hash_tracks_matched;
        total_tracks += r.hash_tracks_total;
    }

    log_print("\n========================================\n");
    log_print("SUMMARY: %zu files tested\n", results.size());
    log_print("  Read OK:     %d / %zu\n", pass_read, results.size());
    log_print("  Hash OK:     %d / %zu  (%d/%d tracks matched)\n", pass_hash, results.size(), total_tracks_matched, total_tracks);
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

    // --- Hash flags & output format tests ---
    // Use the first CHD found for targeted hash testing
    if (!chd_files.empty()) {
        const std::string& test_chd = chd_files.front();
        std::string stem = fs::path(test_chd).stem().string();
        log_print("\n========================================\n");
        log_print("HASH FLAG & FORMAT TESTS: %s\n", test_chd.c_str());
        log_print("========================================\n");

        chdlite::ChdReader reader;
        reader.open(test_chd);

        // Reference: hash with All flags
        auto ref = reader.hash_content(chdlite::HashFlags::All);
        if (!ref.success) {
            log_print("  HASH ALL FAILED: %s\n", ref.error_message.c_str());
        } else {
            log_print("  All:    SHA1=%s  MD5=%s  CRC=%s  SHA256=%s  XXH3=%s\n",
                ref.tracks[0].sha1.hex_string.c_str(),
                ref.tracks[0].md5.hex_string.c_str(),
                ref.tracks[0].crc32.hex_string.c_str(),
                ref.tracks[0].sha256.hex_string.c_str(),
                ref.tracks[0].xxh3_128.hex_string.c_str());
        }

        // Test each individual flag
        struct FlagTest {
            const char* name;
            chdlite::HashFlags flag;
        };
        FlagTest flag_tests[] = {
            { "SHA1",     chdlite::HashFlags::SHA1     },
            { "MD5",      chdlite::HashFlags::MD5      },
            { "CRC32",    chdlite::HashFlags::CRC32    },
            { "SHA256",   chdlite::HashFlags::SHA256   },
            { "XXH3_128", chdlite::HashFlags::XXH3_128 },
        };

        bool all_flags_ok = true;
        for (const auto& ft : flag_tests) {
            auto t0 = std::chrono::steady_clock::now();
            auto hr = reader.hash_content(ft.flag);
            auto t1 = std::chrono::steady_clock::now();
            double sec = std::chrono::duration<double>(t1 - t0).count();

            if (!hr.success) {
                log_print("  %s:  FAILED: %s\n", ft.name, hr.error_message.c_str());
                all_flags_ok = false;
                continue;
            }

            const auto& t = hr.tracks[0];
            const auto& r = ref.tracks[0];
            bool ok = true;
            std::string computed;

            // Get the computed value for this flag
            if (ft.flag == chdlite::HashFlags::SHA1)          computed = t.sha1.hex_string;
            else if (ft.flag == chdlite::HashFlags::MD5)      computed = t.md5.hex_string;
            else if (ft.flag == chdlite::HashFlags::CRC32)    computed = t.crc32.hex_string;
            else if (ft.flag == chdlite::HashFlags::SHA256)   computed = t.sha256.hex_string;
            else if (ft.flag == chdlite::HashFlags::XXH3_128) computed = t.xxh3_128.hex_string;

            // Match against reference
            if (ft.flag == chdlite::HashFlags::SHA1)          ok = (computed == r.sha1.hex_string);
            else if (ft.flag == chdlite::HashFlags::MD5)      ok = (computed == r.md5.hex_string);
            else if (ft.flag == chdlite::HashFlags::CRC32)    ok = (computed == r.crc32.hex_string);
            else if (ft.flag == chdlite::HashFlags::SHA256)   ok = (computed == r.sha256.hex_string);
            else if (ft.flag == chdlite::HashFlags::XXH3_128) ok = (computed == r.xxh3_128.hex_string);

            // Verify other fields are empty (only requested flag should be set)
            if (ft.flag != chdlite::HashFlags::SHA1)     ok = ok && t.sha1.hex_string.empty();
            if (ft.flag != chdlite::HashFlags::MD5)      ok = ok && t.md5.hex_string.empty();
            if (ft.flag != chdlite::HashFlags::CRC32)    ok = ok && t.crc32.hex_string.empty();
            if (ft.flag != chdlite::HashFlags::SHA256)   ok = ok && t.sha256.hex_string.empty();
            if (ft.flag != chdlite::HashFlags::XXH3_128) ok = ok && t.xxh3_128.hex_string.empty();

            log_print("  %-9s: %s  (%.1fs)  %s\n", ft.name, computed.c_str(), sec, ok ? "OK" : "FAIL");
            if (!ok) all_flags_ok = false;
        }
        log_print("  Hash flags: %s\n", all_flags_ok ? "ALL OK" : "SOME FAILED");

        // --- Output format tests ---
        log_print("\n  --- Output Format Tests ---\n");
        struct FormatTest {
            const char* name;
            const char* ext;
            chdlite::HashOutputFormat fmt;
        };
        FormatTest format_tests[] = {
            { "Log",  ".log",  chdlite::HashOutputFormat::Log  },
            { "SFV",  ".sfv",  chdlite::HashOutputFormat::SFV  },
            { "JSON", ".json", chdlite::HashOutputFormat::JSON },
        };

        std::string fmt_dir = (fs::path(output_dir) / "hash_formats").string();
        fs::create_directories(fmt_dir);

        // Write each format individually
        for (const auto& ft : format_tests) {
            std::string content = chdlite::ChdReader::format_hash(ref, ft.fmt, stem);
            std::string path = (fs::path(fmt_dir) / (stem + ft.ext)).string();
            std::ofstream out(path);
            out << content;
            out.close();
            log_print("  %s: %zu bytes -> %s\n", ft.name, content.size(), path.c_str());
        }

        // Write per-algorithm verification files
        struct VerifFile {
            const char* ext;
            const char* label;
            // lambda-like: which field to use from TrackHashResult
        };
        auto write_verif = [&](const char* ext, const char* label,
                               auto get_hash) {
            std::string path = (fs::path(fmt_dir) / (stem + ext)).string();
            std::ofstream out(path);
            out << "; Generated by CHDlite\n";
            for (const auto& t : ref.tracks) {
                std::string fn = chdlite::ChdReader::format_hash(ref, chdlite::HashOutputFormat::Log, stem); // unused
                // Build filename matching track_filename logic
                std::string hash = get_hash(t);
                if (!hash.empty()) {
                    bool is_cd = (ref.content_type == chdlite::ContentType::CDROM ||
                                  ref.content_type == chdlite::ContentType::GDROM);
                    std::string fn;
                    if (!is_cd)
                        fn = stem + ((ref.content_type == chdlite::ContentType::DVD) ? ".iso" : ".bin");
                    else if (ref.content_type == chdlite::ContentType::GDROM) {
                        char buf[8]; std::snprintf(buf, sizeof(buf), "%02u", t.track_number);
                        fn = stem + buf + (t.is_audio ? ".raw" : ".bin");
                    } else if (ref.tracks.size() == 1)
                        fn = stem + ".bin";
                    else {
                        char buf[8];
                        if (ref.tracks.size() >= 10)
                            std::snprintf(buf, sizeof(buf), "%02u", t.track_number);
                        else
                            std::snprintf(buf, sizeof(buf), "%u", t.track_number);
                        fn = stem + " (Track " + buf + ").bin";
                    }
                    out << hash << " *" << fn << "\n";
                }
            }
            if (!ref.sheet_content.empty()) {
                std::string sh = get_hash(ref.sheet_hash);
                if (!sh.empty()) {
                    std::string fn = stem + ((ref.content_type == chdlite::ContentType::GDROM) ? ".gdi" : ".cue");
                    out << sh << " *" << fn << "\n";
                }
            }
            out.close();
            auto sz = fs::file_size(path);
            log_print("  %s: %llu bytes -> %s\n", label, (unsigned long long)sz, path.c_str());
        };

        write_verif(".md5", "MD5", [](const chdlite::TrackHashResult& t) { return t.md5.hex_string; });
        write_verif(".sha1", "SHA1", [](const chdlite::TrackHashResult& t) { return t.sha1.hex_string; });
        write_verif(".sha256", "SHA256", [](const chdlite::TrackHashResult& t) { return t.sha256.hex_string; });
        write_verif(".xxhash3_128", "XXH3_128", [](const chdlite::TrackHashResult& t) { return t.xxh3_128.hex_string; });

        // Print each format to log for verification
        for (const auto& ft : format_tests) {
            std::string content = chdlite::ChdReader::format_hash(ref, ft.fmt, stem);
            log_print("\n  --- %s Output ---\n%s", ft.name, content.c_str());
        }

        reader.close();
    }

    if (g_log) std::fclose(g_log);
    return fail > 0 ? 1 : 0;
}
