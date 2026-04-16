// CHDlite - Archive roundtrip test
// For each input (CUE/GDI/ISO), archives to CHD with smart defaults,
// then extracts the new CHD and compares extracted files with originals.

#include "chd_api.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct TestCase
{
    std::string name;
    std::string input_path;       // CUE/GDI/ISO to archive
    std::string expected_system;
    // Original bin/iso files to compare against after extraction
    std::vector<std::string> compare_files;
};

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

static std::string codec_name(chdlite::Codec c)
{
    switch (c) {
    case chdlite::Codec::Zlib:     return "zlib";
    case chdlite::Codec::Zstd:     return "zstd";
    case chdlite::Codec::LZMA:     return "lzma";
    case chdlite::Codec::FLAC:     return "flac";
    case chdlite::Codec::Huffman:  return "huffman";
    case chdlite::Codec::CD_Zlib:  return "cdzl";
    case chdlite::Codec::CD_Zstd:  return "cdzs";
    case chdlite::Codec::CD_LZMA:  return "cdlz";
    case chdlite::Codec::CD_FLAC:  return "cdfl";
    case chdlite::Codec::None:     return "none";
    default:                       return "?";
    }
}

// Compare two files byte-for-byte
static bool compare_files_binary(const std::string& path_a, const std::string& path_b)
{
    std::ifstream fa(path_a, std::ios::binary);
    std::ifstream fb(path_b, std::ios::binary);
    if (!fa) { std::printf("    cannot open: %s\n", path_a.c_str()); return false; }
    if (!fb) { std::printf("    cannot open: %s\n", path_b.c_str()); return false; }

    fa.seekg(0, std::ios::end);
    fb.seekg(0, std::ios::end);
    auto size_a = fa.tellg();
    auto size_b = fb.tellg();
    fa.seekg(0);
    fb.seekg(0);

    if (size_a != size_b) {
        std::printf("    SIZE MISMATCH: %lld vs %lld bytes\n",
                    (long long)size_a, (long long)size_b);
        return false;
    }

    constexpr size_t BUF = 1024 * 1024;
    std::vector<char> buf_a(BUF), buf_b(BUF);
    uint64_t offset = 0;

    while (fa && fb) {
        fa.read(buf_a.data(), BUF);
        fb.read(buf_b.data(), BUF);
        auto count = std::min(fa.gcount(), fb.gcount());

        if (std::memcmp(buf_a.data(), buf_b.data(), static_cast<size_t>(count)) != 0) {
            for (std::streamsize i = 0; i < count; i++) {
                if (buf_a[i] != buf_b[i]) {
                    std::printf("    BYTE MISMATCH at offset %llu (0x%02x vs 0x%02x)\n",
                                (unsigned long long)(offset + i),
                                (unsigned char)buf_a[i], (unsigned char)buf_b[i]);
                    return false;
                }
            }
        }
        offset += count;
        if (count == 0) break;
    }

    std::printf("    OK (%lld bytes match)\n", (long long)size_a);
    return true;
}

int main(int argc, char* argv[])
{
    std::string base_dir = "test_root/Roms/DiscRomsChd";
    std::string out_dir  = "test_root/archive_output";

    if (argc >= 2) base_dir = argv[1];
    if (argc >= 3) out_dir  = argv[2];

    // Clean output
    std::error_code ec;
    fs::remove_all(out_dir, ec);
    fs::create_directories(out_dir, ec);

    std::vector<TestCase> tests;

    auto mp = [&](const char* rel) -> std::string {
        return (fs::path(base_dir) / rel).string();
    };

    // PS2 (DVD ISO)
    if (fs::exists(mp("PS2/Dragon Quest V - Tenkuu no Hanayome (Japan).iso"))) {
        TestCase tc;
        tc.name = "PS2_DVD";
        tc.input_path = mp("PS2/Dragon Quest V - Tenkuu no Hanayome (Japan).iso");
        tc.expected_system = "PlayStation 2";
        tc.compare_files.push_back(tc.input_path);
        tests.push_back(std::move(tc));
    }

    // PSP (DVD ISO)
    if (fs::exists(mp("PSP/Final Fantasy IV Complete Collection - Final Fantasy IV & The After Years (Japan) (En,Ja,Fr).iso"))) {
        TestCase tc;
        tc.name = "PSP_DVD";
        tc.input_path = mp("PSP/Final Fantasy IV Complete Collection - Final Fantasy IV & The After Years (Japan) (En,Ja,Fr).iso");
        tc.expected_system = "PSP";
        tc.compare_files.push_back(tc.input_path);
        tests.push_back(std::move(tc));
    }

    // PS1 (CUE with fix_cue — CUE may reference "(Track 1).bin" that doesn't exist)
    if (fs::exists(mp("PS1/PoPoRoGue (Japan).cue"))) {
        TestCase tc;
        tc.name = "PS1_CD";
        tc.input_path = mp("PS1/PoPoRoGue (Japan).cue");
        tc.expected_system = "PlayStation";
        tc.compare_files.push_back(mp("PS1/PoPoRoGue (Japan).bin"));
        tests.push_back(std::move(tc));
    }

    // Saturn (CUE + 2 BINs)
    if (fs::exists(mp("Saturn/Sakura Taisen (Japan) (Disc 1) (6M).cue"))) {
        TestCase tc;
        tc.name = "Saturn_CD";
        tc.input_path = mp("Saturn/Sakura Taisen (Japan) (Disc 1) (6M).cue");
        tc.expected_system = "Sega Saturn";
        tc.compare_files.push_back(mp("Saturn/Sakura Taisen (Japan) (Disc 1) (6M) (Track 1).bin"));
        tc.compare_files.push_back(mp("Saturn/Sakura Taisen (Japan) (Disc 1) (6M) (Track 2).bin"));
        tests.push_back(std::move(tc));
    }

    // PC Engine (CUE + 22 BINs)
    if (fs::exists(mp("PCEngine/Dragon Half (Japan).cue"))) {
        TestCase tc;
        tc.name = "PCEngine_CD";
        tc.input_path = mp("PCEngine/Dragon Half (Japan).cue");
        tc.expected_system = "Generic CD";
        for (int i = 1; i <= 22; i++) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "PCEngine/Dragon Half (Japan) (Track %02d).bin", i);
            tc.compare_files.push_back(mp(buf));
        }
        tests.push_back(std::move(tc));
    }

    // Dreamcast (GDI + 3 track files)
    if (fs::exists(mp("Dreamcast/D2 - D no Shokutaku 2 (Japan) (Disc 1).gdi"))) {
        TestCase tc;
        tc.name = "Dreamcast_GD";
        tc.input_path = mp("Dreamcast/D2 - D no Shokutaku 2 (Japan) (Disc 1).gdi");
        tc.expected_system = "Dreamcast";
        tc.compare_files.push_back(mp("Dreamcast/D2 - D no Shokutaku 2 (Japan) (Disc 1)01.bin"));
        tc.compare_files.push_back(mp("Dreamcast/D2 - D no Shokutaku 2 (Japan) (Disc 1)02.raw"));
        tc.compare_files.push_back(mp("Dreamcast/D2 - D no Shokutaku 2 (Japan) (Disc 1)03.bin"));
        tests.push_back(std::move(tc));
    }

    std::printf("Archive roundtrip test: %zu test cases\n\n", tests.size());

    int pass = 0, fail = 0;

    for (const auto& tc : tests)
    {
        std::printf("=== %s ===\n", tc.name.c_str());
        std::printf("  Input: %s\n", tc.input_path.c_str());

        // --- Step 1: Archive input → CHD ---
        fs::path chd_path = fs::path(out_dir) / (tc.name + ".chd");

        chdlite::ArchiveOptions arch_opts;
        arch_opts.detect_title = true;
        arch_opts.fix_cue = chdlite::FixCue::Single;

        auto t0 = std::chrono::steady_clock::now();
        chdlite::ChdArchiver archiver;
        auto ar = archiver.archive(tc.input_path, chd_path.string(), arch_opts);
        auto t1 = std::chrono::steady_clock::now();
        double arch_sec = std::chrono::duration<double>(t1 - t0).count();

        if (!ar.success) {
            std::printf("  ARCHIVE FAILED: %s\n", ar.error_message.c_str());
            fail++;
            continue;
        }

        double ratio = ar.input_bytes > 0
            ? 100.0 * double(ar.output_bytes) / double(ar.input_bytes) : 0.0;

        std::printf("  Archive OK (%.1fs)  %llu -> %llu bytes (%.1f%%)\n",
                    arch_sec, (unsigned long long)ar.input_bytes,
                    (unsigned long long)ar.output_bytes, ratio);
        std::printf("  System: %s", chdlite::game_platform_name(ar.detected_game_platform));
        if (!ar.detected_title.empty())  std::printf("  Title: %s", ar.detected_title.c_str());
        if (!ar.detected_manufacturer_id.empty()) std::printf("  ID: %s", ar.detected_manufacturer_id.c_str());
        std::printf("\n");

        // Print CHD header info
        try {
            chdlite::ChdReader reader;
            reader.open(ar.output_path);
            auto hdr = reader.read_header();
            std::printf("  CHD v%u %s  hunks=%u hunk=%u unit=%u  codecs=",
                        hdr.version, content_type_str(hdr.content_type),
                        hdr.hunk_count, hdr.hunk_bytes, hdr.unit_bytes);
            for (int i = 0; i < 4; i++)
                if (hdr.codecs[i] != chdlite::Codec::None)
                    std::printf("%s%s", i ? "," : "", codec_name(hdr.codecs[i]).c_str());
            std::printf("\n");
            reader.close();
        } catch (const std::exception& e) {
            std::printf("  WARNING: read CHD: %s\n", e.what());
        }

        // --- Step 2: Extract new CHD → raw files ---
        fs::path ext_dir = fs::path(out_dir) / (tc.name + "_ext");
        fs::create_directories(ext_dir, ec);

        chdlite::ExtractOptions ext_opts;
        ext_opts.output_dir = ext_dir.string();

        auto t2 = std::chrono::steady_clock::now();
        chdlite::ChdExtractor extractor;
        auto er = extractor.extract(ar.output_path, ext_opts);
        auto t3 = std::chrono::steady_clock::now();
        double ext_sec = std::chrono::duration<double>(t3 - t2).count();

        if (!er.success) {
            std::printf("  EXTRACT FAILED: %s\n", er.error_message.c_str());
            fail++;
            continue;
        }

        std::printf("  Extract OK (%.1fs)  %zu files  %llu bytes\n",
                    ext_sec, er.output_files.size(), (unsigned long long)er.bytes_written);
        for (const auto& f : er.output_files)
            std::printf("    -> %s (%llu)\n", fs::path(f).filename().c_str(),
                        (unsigned long long)fs::file_size(f, ec));

        // --- Step 3: Compare extracted with originals ---
        bool compare_ok = true;

        if (tc.compare_files.size() == 1 && !er.output_files.empty()) {
            // DVD/ISO or single BIN: find the main extracted data file
            std::string ext_main;
            for (const auto& f : er.output_files) {
                std::string ext = fs::path(f).extension().string();
                for (auto& c : ext) c = std::tolower(c);
                if (ext == ".iso" || ext == ".bin" || ext == ".img") {
                    ext_main = f;
                    break;
                }
            }
            if (ext_main.empty()) ext_main = er.output_files[0];

            std::printf("  Compare: %s vs %s\n",
                        fs::path(tc.compare_files[0]).filename().c_str(),
                        fs::path(ext_main).filename().c_str());
            if (!compare_files_binary(tc.compare_files[0], ext_main))
                compare_ok = false;
        }
        else if (tc.compare_files.size() > 1) {
            // Multi-track: collect extracted BIN/RAW files, sort, compare in order
            std::vector<std::string> ext_bins;
            for (const auto& f : er.output_files) {
                std::string ext = fs::path(f).extension().string();
                for (auto& c : ext) c = std::tolower(c);
                if (ext == ".bin" || ext == ".raw")
                    ext_bins.push_back(f);
            }
            std::sort(ext_bins.begin(), ext_bins.end());

            if (ext_bins.size() != tc.compare_files.size()) {
                std::printf("  TRACK COUNT MISMATCH: expected %zu, extracted %zu\n",
                            tc.compare_files.size(), ext_bins.size());
            }

            size_t n = std::min(ext_bins.size(), tc.compare_files.size());
            int matched = 0;
            for (size_t i = 0; i < n; i++) {
                std::printf("  Track %zu: %s vs %s\n", i + 1,
                            fs::path(tc.compare_files[i]).filename().c_str(),
                            fs::path(ext_bins[i]).filename().c_str());
                if (compare_files_binary(tc.compare_files[i], ext_bins[i]))
                    matched++;
                else
                    compare_ok = false;
            }
            std::printf("  Tracks: %d / %zu matched\n", matched, n);
        }

        if (compare_ok) {
            std::printf("  PASS\n\n");
            pass++;
        } else {
            std::printf("  FAIL\n\n");
            fail++;
        }
    }

    std::printf("\n=== Results: %d passed, %d failed out of %zu ===\n", pass, fail, tests.size());
    return fail > 0 ? 1 : 0;
}
