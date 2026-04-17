// CHDlite - CUE style conversion and extraction test
// Tests convert_cue_style() all 6 non-identity conversions × 2 track types
// Tests extraction with each CueStyle and verifies against known reference hashes

#include "chd_extractor.hpp"
#include "chd_reader.hpp"
#include "../src/mame/util/hashing.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

static void check(bool cond, const char* name)
{
    if (cond) { ++g_pass; std::printf("  PASS %s\n", name); }
    else      { ++g_fail; std::printf("  FAIL %s\n", name); }
}

static std::string read_file_bin(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), {} };
}

static std::string sha1_of(const std::string& data)
{
    // Use the library's built-in SHA1
    util::sha1_creator sha1;
    sha1.append(data.data(), data.size());
    return sha1.finish().as_string();
}

// ======================> Block 1: convert_cue_style round-trips

static void test_conversions()
{
    using namespace chdlite;
    std::printf("\n=== Block 1: convert_cue_style (6 conversions × 2 track types = 12) ===\n");

    // Single-track CUEs
    std::string single_chdman =
        "FILE \"Game (Track 1).bin\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n";
    std::string single_redump =
        "FILE \"Game.bin\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n";
    std::string single_redump_cat =
        "CATALOG 0000000000000\r\n"
        "FILE \"Game.bin\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n";

    // Multi-track CUEs
    std::string multi_chdman =
        "FILE \"Game (Track 01).bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "FILE \"Game (Track 02).bin\" BINARY\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:02:00\r\n";
    std::string multi_redump = multi_chdman; // multi-track names are same
    std::string multi_redump_cat =
        "CATALOG 0000000000000\r\n" + multi_chdman;

    // --- Single-track conversions ---
    check(convert_cue_style(single_chdman, CueStyle::Redump) == single_redump,
          "single: Chdman → Redump");
    check(convert_cue_style(single_chdman, CueStyle::RedumpCatalog) == single_redump_cat,
          "single: Chdman → RedumpCatalog");
    check(convert_cue_style(single_redump, CueStyle::Chdman) == single_chdman,
          "single: Redump → Chdman");
    check(convert_cue_style(single_redump, CueStyle::RedumpCatalog) == single_redump_cat,
          "single: Redump → RedumpCatalog");
    check(convert_cue_style(single_redump_cat, CueStyle::Chdman) == single_chdman,
          "single: RedumpCatalog → Chdman");
    check(convert_cue_style(single_redump_cat, CueStyle::Redump) == single_redump,
          "single: RedumpCatalog → Redump");

    // --- Multi-track conversions ---
    check(convert_cue_style(multi_chdman, CueStyle::Redump) == multi_redump,
          "multi: Chdman → Redump");
    check(convert_cue_style(multi_chdman, CueStyle::RedumpCatalog) == multi_redump_cat,
          "multi: Chdman → RedumpCatalog");
    check(convert_cue_style(multi_redump, CueStyle::Chdman) == multi_chdman,
          "multi: Redump → Chdman");
    check(convert_cue_style(multi_redump, CueStyle::RedumpCatalog) == multi_redump_cat,
          "multi: Redump → RedumpCatalog");
    check(convert_cue_style(multi_redump_cat, CueStyle::Chdman) == multi_chdman,
          "multi: RedumpCatalog → Chdman");
    check(convert_cue_style(multi_redump_cat, CueStyle::Redump) == multi_redump,
          "multi: RedumpCatalog → Redump");
}

// ======================> Block 2: identity / idempotent conversions

static void test_identity()
{
    using namespace chdlite;
    std::printf("\n=== Block 2: identity / idempotent (6 tests) ===\n");

    std::string single_chdman =
        "FILE \"Game (Track 1).bin\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n";
    std::string single_redump =
        "FILE \"Game.bin\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n";
    std::string single_redump_cat =
        "CATALOG 0000000000000\r\n"
        "FILE \"Game.bin\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n";

    check(convert_cue_style(single_chdman, CueStyle::Chdman) == single_chdman,
          "single: Chdman → Chdman idempotent");
    check(convert_cue_style(single_redump, CueStyle::Redump) == single_redump,
          "single: Redump → Redump idempotent");
    check(convert_cue_style(single_redump_cat, CueStyle::RedumpCatalog) == single_redump_cat,
          "single: RedumpCatalog → RedumpCatalog idempotent");

    std::string multi =
        "FILE \"X (Track 01).bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n"
        "FILE \"X (Track 02).bin\" BINARY\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 00 00:00:00\r\n"
        "    INDEX 01 00:02:00\r\n";
    check(convert_cue_style(multi, CueStyle::Chdman) == multi,
          "multi: Chdman → Chdman idempotent");
    check(convert_cue_style(multi, CueStyle::Redump) == multi,
          "multi: Redump → Redump idempotent");
    auto multi_cat = "CATALOG 0000000000000\r\n" + multi;
    check(convert_cue_style(multi_cat, CueStyle::RedumpCatalog) == multi_cat,
          "multi: RedumpCatalog → RedumpCatalog idempotent");
}

// ======================> Block 3: LF/mixed input normalization

static void test_normalization()
{
    using namespace chdlite;
    std::printf("\n=== Block 3: line ending normalization (3 tests) ===\n");

    std::string lf_input =
        "FILE \"Test (Track 1).bin\" BINARY\n"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\n";
    std::string expected_crlf =
        "FILE \"Test (Track 1).bin\" BINARY\r\n"
        "  TRACK 01 MODE1/2352\r\n"
        "    INDEX 01 00:00:00\r\n";

    check(convert_cue_style(lf_input, CueStyle::Chdman) == expected_crlf,
          "LF input → CRLF output");

    // Mixed CR/LF
    std::string mixed =
        "FILE \"A.bin\" BINARY\r"
        "  TRACK 01 MODE1/2352\n"
        "    INDEX 01 00:00:00\r\n";
    auto result = convert_cue_style(mixed, CueStyle::Chdman);
    check(result.find('\r') != std::string::npos
       && result.find("\r\n") != std::string::npos
       && result.find("\r\r") == std::string::npos,
          "mixed CR/LF → clean CRLF");

    // Single-track with CATALOG and LF
    std::string cat_lf =
        "CATALOG 0000000000000\n"
        "FILE \"X (Track 1).bin\" BINARY\n"
        "  TRACK 01 AUDIO\n"
        "    INDEX 01 00:00:00\n";
    auto stripped = convert_cue_style(cat_lf, CueStyle::Redump);
    check(stripped.find("CATALOG") == std::string::npos
       && stripped.find("\r\n") != std::string::npos,
          "LF+CATALOG input → stripped CRLF Redump");
}

// ======================> Block 4: extraction with CueStyle + hash verification

static void test_extraction_styles(const std::string& rom_dir, const std::string& tmp_dir)
{
    using namespace chdlite;
    std::printf("\n=== Block 4: extraction with CueStyle + hash verification ===\n");

    struct TestCase {
        const char* chd_subpath;
        CueStyle    style;
        const char* style_name;
        const char* expected_sha1;   // CUE file SHA1
        int         expected_size;   // CUE file size
    };

    // Reference hashes verified against chdman output and Redump DATs
    TestCase cases[] = {
        // PS1 single-track — chdman style: (Track 1)
        { "PS1/PoPoRoGue (Japan).chd", CueStyle::Chdman,
          "PS1 Chdman", "0b11eeb5bcbffd13fc321281879c2be558fcdd5a", 93 },
        // PS1 single-track — Redump style: no (Track 1)
        { "PS1/PoPoRoGue (Japan).chd", CueStyle::Redump,
          "PS1 Redump", "8a60dd8bde95f608345fdecf2ff68da47ec8fc69", 83 },
        // PS1 single-track — RedumpCatalog: Redump + CATALOG header
        { "PS1/PoPoRoGue (Japan).chd", CueStyle::RedumpCatalog,
          "PS1 RedumpCatalog", nullptr, 83 + 23 },

        // Saturn multi-track — chdman style
        { "Saturn/Sakura Taisen (Japan) (Disc 1) (6M).chd", CueStyle::Chdman,
          "Saturn Chdman", "8638937b15dac1927797f4b4c17d2882721107af", 240 },
        // Saturn multi-track — Redump style (same as chdman for multi)
        { "Saturn/Sakura Taisen (Japan) (Disc 1) (6M).chd", CueStyle::Redump,
          "Saturn Redump", "8638937b15dac1927797f4b4c17d2882721107af", 240 },
        // Saturn multi-track — RedumpCatalog
        { "Saturn/Sakura Taisen (Japan) (Disc 1) (6M).chd", CueStyle::RedumpCatalog,
          "Saturn RedumpCatalog", nullptr, 240 + 23 },

        // PCEngine multi-track — chdman style
        { "PCEngine/Dragon Half (Japan).chd", CueStyle::Chdman,
          "PCE Chdman", "caac370d238ca52f5ad0c4d4aac77170d4e08ab6", 2081 },
        // PCEngine multi-track — RedumpCatalog (matches Redump DAT)
        { "PCEngine/Dragon Half (Japan).chd", CueStyle::RedumpCatalog,
          "PCE RedumpCatalog", "691892ab2509b26533ee31c7f515f4de667b63cc", 2104 },
    };

    for (auto& tc : cases)
    {
        std::string chd_path = rom_dir + "/" + tc.chd_subpath;
        if (!fs::exists(chd_path))
        {
            std::printf("  SKIP %s (CHD not found)\n", tc.style_name);
            continue;
        }

        // Create unique output dir per test case
        std::string out_dir = tmp_dir + "/" + tc.style_name;
        fs::create_directories(out_dir);

        ChdExtractor extractor;
        ExtractOptions opts;
        opts.output_dir = out_dir;
        opts.cue_style = tc.style;
        opts.force_overwrite = true;

        ExtractionResult result;
        try {
            result = extractor.extract(chd_path, opts);
        } catch (const std::exception& e) {
            std::printf("  FAIL %s — exception: %s\n", tc.style_name, e.what());
            ++g_fail;
            continue;
        }

        if (!result.success) {
            std::printf("  FAIL %s — extraction failed: %s\n", tc.style_name, result.error_message.c_str());
            ++g_fail;
            continue;
        }

        // Find the .cue file in output
        std::string cue_path;
        for (auto& f : result.output_files)
            if (f.size() >= 4 && f.substr(f.size() - 4) == ".cue")
                cue_path = f;

        if (cue_path.empty()) {
            std::printf("  FAIL %s — no .cue in output\n", tc.style_name);
            ++g_fail;
            continue;
        }

        std::string cue_data = read_file_bin(cue_path);
        int cue_size = static_cast<int>(cue_data.size());
        std::string cue_sha1 = sha1_of(cue_data);

        // Check size
        char desc_size[128];
        std::snprintf(desc_size, sizeof(desc_size), "%s size %d == %d",
                      tc.style_name, cue_size, tc.expected_size);
        check(cue_size == tc.expected_size, desc_size);

        // Check SHA1 (if we have a reference)
        if (tc.expected_sha1) {
            char desc_sha1[128];
            std::snprintf(desc_sha1, sizeof(desc_sha1), "%s SHA1 matches", tc.style_name);
            check(cue_sha1 == tc.expected_sha1, desc_sha1);
        }
    }
}

// ======================> Block 5: convert extracted CUE and verify hash

static void test_convert_extracted(const std::string& rom_dir, const std::string& tmp_dir)
{
    using namespace chdlite;
    std::printf("\n=== Block 5: extract Chdman → convert to all styles → verify hash ===\n");

    struct ConvertCase {
        const char* chd_subpath;
        const char* label;
        CueStyle target_style;
        const char* target_name;
        const char* expected_sha1;
        int expected_size;
    };

    ConvertCase cases[] = {
        // PS1 single-track: extract as Chdman, convert to Redump
        { "PS1/PoPoRoGue (Japan).chd", "PS1", CueStyle::Redump,
          "Redump", "8a60dd8bde95f608345fdecf2ff68da47ec8fc69", 83 },
        // PS1 single-track: extract as Chdman, convert to RedumpCatalog
        { "PS1/PoPoRoGue (Japan).chd", "PS1", CueStyle::RedumpCatalog,
          "RedumpCatalog", nullptr, 106 },

        // Saturn multi: extract Chdman, convert to RedumpCatalog
        { "Saturn/Sakura Taisen (Japan) (Disc 1) (6M).chd", "Saturn", CueStyle::RedumpCatalog,
          "RedumpCatalog", nullptr, 263 },
        // Saturn multi: extract as Chdman, convert to Redump (should be same)
        { "Saturn/Sakura Taisen (Japan) (Disc 1) (6M).chd", "Saturn", CueStyle::Redump,
          "Redump", "8638937b15dac1927797f4b4c17d2882721107af", 240 },

        // PCEngine: extract Chdman, convert to RedumpCatalog (matches Redump DAT)
        { "PCEngine/Dragon Half (Japan).chd", "PCE", CueStyle::RedumpCatalog,
          "RedumpCatalog", "691892ab2509b26533ee31c7f515f4de667b63cc", 2104 },
        // PCEngine: convert RedumpCatalog back to Chdman
        { "PCEngine/Dragon Half (Japan).chd", "PCE", CueStyle::Chdman,
          "Chdman(roundtrip)", "caac370d238ca52f5ad0c4d4aac77170d4e08ab6", 2081 },
    };

    // First extract all needed CHDs in Chdman style
    std::map<std::string, std::string> chdman_cues; // chd_subpath → cue content

    for (auto& tc : cases)
    {
        if (chdman_cues.count(tc.chd_subpath)) continue;

        std::string chd_path = rom_dir + "/" + tc.chd_subpath;
        if (!fs::exists(chd_path)) continue;

        std::string out_dir = tmp_dir + "/convert_src_" + tc.label;
        fs::create_directories(out_dir);

        ChdExtractor extractor;
        ExtractOptions opts;
        opts.output_dir = out_dir;
        opts.cue_style = CueStyle::Chdman;
        opts.force_overwrite = true;

        auto result = extractor.extract(chd_path, opts);
        if (!result.success) continue;

        for (auto& f : result.output_files) {
            if (f.size() >= 4 && f.substr(f.size() - 4) == ".cue") {
                chdman_cues[tc.chd_subpath] = read_file_bin(f);
                break;
            }
        }
    }

    // Now test conversions
    for (auto& tc : cases)
    {
        auto it = chdman_cues.find(tc.chd_subpath);
        if (it == chdman_cues.end()) {
            std::printf("  SKIP %s→%s (source CUE not available)\n", tc.label, tc.target_name);
            continue;
        }

        std::string source = it->second;
        std::string converted;

        if (std::strcmp(tc.target_name, "Chdman(roundtrip)") == 0) {
            // First convert to RedumpCatalog, then back to Chdman
            auto intermediate = convert_cue_style(source, CueStyle::RedumpCatalog);
            converted = convert_cue_style(intermediate, CueStyle::Chdman);
        } else {
            converted = convert_cue_style(source, tc.target_style);
        }

        int size = static_cast<int>(converted.size());
        std::string sha1 = sha1_of(converted);

        char desc[128];
        std::snprintf(desc, sizeof(desc), "%s convert→%s size %d == %d",
                      tc.label, tc.target_name, size, tc.expected_size);
        check(size == tc.expected_size, desc);

        if (tc.expected_sha1) {
            std::snprintf(desc, sizeof(desc), "%s convert→%s SHA1 matches", tc.label, tc.target_name);
            check(sha1 == tc.expected_sha1, desc);
        }
    }
}

// ======================> Block 6: SheetSink (hash_content) produces chdman-style

static void test_sheet_sink_hashes(const std::string& rom_dir)
{
    using namespace chdlite;
    std::printf("\n=== Block 6: hash_content() sheet matches chdman reference ===\n");

    struct SheetCase {
        const char* chd_subpath;
        const char* label;
        const char* expected_sha1;
        int expected_size;
    };

    SheetCase cases[] = {
        { "PS1/PoPoRoGue (Japan).chd", "PS1",
          "0b11eeb5bcbffd13fc321281879c2be558fcdd5a", 93 },
        { "Saturn/Sakura Taisen (Japan) (Disc 1) (6M).chd", "Saturn",
          "8638937b15dac1927797f4b4c17d2882721107af", 240 },
        { "PCEngine/Dragon Half (Japan).chd", "PCE",
          "caac370d238ca52f5ad0c4d4aac77170d4e08ab6", 2081 },
        { "Dreamcast/D2 - D no Shokutaku 2 (Japan) (Disc 1).chd", "DC",
          "3039daf7e6c256da2dd0565d6ab20bfbee998191", 192 },
    };

    for (auto& sc : cases)
    {
        std::string chd_path = rom_dir + "/" + sc.chd_subpath;
        if (!fs::exists(chd_path)) {
            std::printf("  SKIP %s (CHD not found)\n", sc.label);
            continue;
        }

        ChdReader reader;
        reader.open(chd_path);
        auto result = reader.hash_content(HashFlags::SHA1);

        if (!result.success) {
            std::printf("  FAIL %s hash_content failed: %s\n", sc.label, result.error_message.c_str());
            ++g_fail;
            continue;
        }

        int sheet_size = static_cast<int>(result.sheet_content.size());
        char desc[128];
        std::snprintf(desc, sizeof(desc), "%s sheet size %d == %d", sc.label, sheet_size, sc.expected_size);
        check(sheet_size == sc.expected_size, desc);

        std::snprintf(desc, sizeof(desc), "%s sheet SHA1 matches chdman ref", sc.label);
        check(result.sheet_hash.sha1.hex_string == sc.expected_sha1, desc);

        // Also test: converting the sheet to RedumpCatalog and back round-trips
        auto cat = convert_cue_style(result.sheet_content, CueStyle::RedumpCatalog);
        auto back = convert_cue_style(cat, CueStyle::Chdman);
        std::snprintf(desc, sizeof(desc), "%s sheet Chdman→Cat→Chdman round-trip", sc.label);
        check(back == result.sheet_content, desc);
    }
}

// ======================> Block 7: match_cue() tests

static void test_match_cue()
{
    using namespace chdlite;
    std::printf("\n=== Block 7: match_cue() tests ===\n");

    // Single-track chdman CUE
    std::string chdman_cue =
        "FILE \"Game (Track 1).bin\" BINARY\r\n"
        "  TRACK 01 MODE2/2352\r\n"
        "    INDEX 01 00:00:00\r\n";

    // Pre-compute expected hashes for each style
    std::string redump_cue = convert_cue_style(chdman_cue, CueStyle::Redump);
    std::string catalog_cue = convert_cue_style(chdman_cue, CueStyle::RedumpCatalog);

    auto sha1_hex = [](const std::string& data) {
        return util::sha1_creator::simple(data.data(),
            static_cast<uint32_t>(data.size())).as_string();
    };
    auto md5_hex = [](const std::string& data) {
        return util::md5_creator::simple(data.data(),
            static_cast<uint32_t>(data.size())).as_string();
    };
    auto crc32_hex = [](const std::string& data) {
        return util::crc32_creator::simple(data.data(),
            static_cast<uint32_t>(data.size())).as_string();
    };

    // 1. Match Chdman style by SHA1
    {
        auto r = match_cue(chdman_cue, HashAlgorithm::SHA1, sha1_hex(chdman_cue));
        check(r.style == CueStyle::Chdman, "match Chdman SHA1");
        check(r.cue_data == chdman_cue, "match Chdman SHA1 data");
    }

    // 2. Match Redump style by SHA1 (input is chdman, db hash is redump)
    {
        auto r = match_cue(chdman_cue, HashAlgorithm::SHA1, sha1_hex(redump_cue));
        check(r.style == CueStyle::Redump, "match Redump SHA1");
        check(r.cue_data == redump_cue, "match Redump SHA1 data");
    }

    // 3. Match RedumpCatalog style by SHA1
    {
        auto r = match_cue(chdman_cue, HashAlgorithm::SHA1, sha1_hex(catalog_cue));
        check(r.style == CueStyle::RedumpCatalog, "match RedumpCatalog SHA1");
        check(r.cue_data == catalog_cue, "match RedumpCatalog SHA1 data");
    }

    // 4. Match with MD5
    {
        auto r = match_cue(chdman_cue, HashAlgorithm::MD5, md5_hex(redump_cue));
        check(r.style == CueStyle::Redump, "match Redump MD5");
    }

    // 5. Match with CRC32
    {
        auto r = match_cue(chdman_cue, HashAlgorithm::CRC32, crc32_hex(catalog_cue));
        check(r.style == CueStyle::RedumpCatalog, "match RedumpCatalog CRC32");
    }

    // 6. Case-insensitive hash comparison
    {
        std::string upper_hash = sha1_hex(redump_cue);
        for (auto& c : upper_hash) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        auto r = match_cue(chdman_cue, HashAlgorithm::SHA1, upper_hash);
        check(r.style == CueStyle::Redump, "match case-insensitive hash");
    }

    // 7. Unmatched: bogus hash
    {
        auto r = match_cue(chdman_cue, HashAlgorithm::SHA1, "0000000000000000000000000000000000000000");
        check(r.style == CueStyle::Unmatched, "unmatched bogus hash");
        check(r.cue_data.empty(), "unmatched empty data");
        check(r.cue_hash.empty(), "unmatched empty hash");
    }

    // 8. Multi-track: no single-track transforms, Chdman==Redump
    {
        std::string multi =
            "FILE \"X (Track 01).bin\" BINARY\r\n"
            "  TRACK 01 MODE1/2352\r\n"
            "    INDEX 01 00:00:00\r\n"
            "FILE \"X (Track 02).bin\" BINARY\r\n"
            "  TRACK 02 AUDIO\r\n"
            "    INDEX 00 00:00:00\r\n"
            "    INDEX 01 00:02:00\r\n";
        // For multi-track, Chdman==Redump (no Track 1 rename)
        auto r = match_cue(multi, HashAlgorithm::SHA1, sha1_hex(multi));
        check(r.style == CueStyle::Chdman, "match multi-track Chdman");
    }

    // 9. Input has LF endings → still matches (normalization)
    {
        std::string lf_cue =
            "FILE \"Game (Track 1).bin\" BINARY\n"
            "  TRACK 01 MODE2/2352\n"
            "    INDEX 01 00:00:00\n";
        // Should match chdman style (which normalizes to CRLF)
        auto r = match_cue(lf_cue, HashAlgorithm::SHA1, sha1_hex(chdman_cue));
        check(r.style == CueStyle::Chdman, "match LF input normalizes to CRLF");
    }
}

int main(int argc, char* argv[])
{
    std::string rom_dir = "test_root/Roms/DiscRomsChd";
    std::string tmp_dir = "/tmp/cue_style_test_run";
    if (argc >= 2) rom_dir = argv[1];

    std::printf("CUE Style Test\n");
    std::printf("ROM dir: %s\n", rom_dir.c_str());

    // Clean and create temp dir
    fs::remove_all(tmp_dir);
    fs::create_directories(tmp_dir);

    // Pure conversion tests (no CHD files needed)
    test_conversions();
    test_identity();
    test_normalization();

    // Extraction + conversion tests (need CHD files)
    test_extraction_styles(rom_dir, tmp_dir);
    test_convert_extracted(rom_dir, tmp_dir);
    test_sheet_sink_hashes(rom_dir);
    test_match_cue();

    std::printf("\n=== Summary ===\n");
    std::printf("  PASS: %d\n", g_pass);
    std::printf("  FAIL: %d\n", g_fail);

    // Clean up
    fs::remove_all(tmp_dir);

    return (g_fail > 0) ? 1 : 0;
}
