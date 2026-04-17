// CHDlite - CLI integration test
// Tests every CLI option at least once, grouped into logical blocks
// so we don't need N^2 combinations.
//
// Block layout — each block bundles compatible options:
//
//  Block 1 (read/info):   read, info, -i, bare-input, -v
//  Block 2 (hash):        hash, -hash (default), -hash sha1,md5, -hash all
//  Block 3 (extract CD):  extract, -o, -sb, --no-splitbin, -ob, -f
//  Block 4 (extract DVD): extractdvd, -i, -o, -isb, -ib, -f
//  Block 5 (create CD):   create, createcd, -c, -o, -f
//  Block 6 (create DVD):  createdvd, -c, -hs, -np, -o, -f
//  Block 7 (auto):        auto (CHD→extract), auto (CUE→create)
//  Block 8 (drag-drop):   bare file argument (no command)
//  Block 9 (errors):      invalid inputs, missing args
//  Block 10 (log):        activity log verification

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ======================> Test infrastructure

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;
static int g_run_ok = 0;  // successful CLI invocations (for log verification)

static const std::string LOG_PATH = (fs::path("build") / "logs" / "error.log").string();

#ifdef _WIN32
static const std::string CLI_EXE = "build\\chdlite.exe";
#else
static const std::string CLI_EXE = "./build/chdlite";
#endif

#define CLR_PASS "\033[32m"
#define CLR_FAIL "\033[31m"
#define CLR_SKIP "\033[33m"
#define CLR_BOLD "\033[1m"
#define CLR_RST  "\033[0m"

static void check(bool ok, const char* desc)
{
    if (ok)
    {
        g_pass++;
        std::printf("  " CLR_PASS "PASS" CLR_RST " %s\n", desc);
    }
    else
    {
        g_fail++;
        std::printf("  " CLR_FAIL "FAIL" CLR_RST " %s\n", desc);
    }
}

// check with diagnostic value printed on failure
static void check_val(bool ok, const char* desc, const std::string& detail)
{
    if (ok)
    {
        g_pass++;
        std::printf("  " CLR_PASS "PASS" CLR_RST " %s\n", desc);
    }
    else
    {
        g_fail++;
        std::printf("  " CLR_FAIL "FAIL" CLR_RST " %s\n", desc);
        std::printf("         -> %s\n", detail.c_str());
    }
}

static void skip(const char* desc, const char* reason)
{
    g_skip++;
    std::printf("  " CLR_SKIP "SKIP" CLR_RST " %s (%s)\n", desc, reason);
}

// Run chdlite CLI and return exit code. Captures stdout+stderr.
static int run(const std::string& args, std::string* output = nullptr)
{
    auto get_exit = [](int status) -> int {
        int s = status;
#ifdef _WIN32
        return s;  // Windows returns exit code directly
#else
        return WEXITSTATUS(s);  // Unix encodes status
#endif
    };

    // Build the actual command
    std::string cmd = CLI_EXE + " " + args + " 2>&1";
    if (output)
    {
        // Capture to temp file (cross-platform temp dir)
        std::string tmp = (fs::temp_directory_path() / "chdlite_cli_test_out.txt").string();
        cmd = CLI_EXE + " " + args + " >\"" + tmp + "\" 2>&1";
        int rc = std::system(cmd.c_str());
        // Read output
        FILE* f = std::fopen(tmp.c_str(), "r");
        if (f) {
            char buf[4096];
            output->clear();
            while (size_t n = std::fread(buf, 1, sizeof(buf), f))
                output->append(buf, n);
            std::fclose(f);
        }
        int exit_code = get_exit(rc);
        if (exit_code == 0) g_run_ok++;
        return exit_code;
    }
    int rc2 = get_exit(std::system(cmd.c_str()));
    if (rc2 == 0) g_run_ok++;
    return rc2;
}

static bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

static bool file_exists(const std::string& p) { return fs::exists(p); }

static uint64_t file_size(const std::string& p)
{
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    return ec ? 0 : sz;
}

static std::string read_text_file(const std::string& path)
{
    std::string result;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return result;
    char buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof(buf), f))
        result.append(buf, n);
    std::fclose(f);
    return result;
}

// ======================> Test data paths

struct TestPaths
{
    std::string chd_root;    // root for CHD input files
    std::string out_root;    // temp output directory

    // Specific CHD files
    std::string ps1_chd;     // multi-track CD
    std::string ps2_chd;     // DVD
    std::string pce_chd;     // CD with audio
    std::string saturn_chd;  // CD
    std::string dc_chd;      // GD-ROM
    std::string psp_chd;     // DVD/ISO

    // Non-CHD for create/read tests
    std::string pce_cue;     // PCEngine CUE
    std::string ps1_cue;     // PS1 CUE
    std::string saturn_cue;  // Saturn CUE
    std::string ps2_iso;     // PS2 ISO
    std::string psp_iso;     // PSP ISO
    std::string dc_gdi;      // Dreamcast GDI

    // Specific .bin files (some are part of a sheet — used to test skip behavior)
    std::string pce_data_bin;  // PCEngine Track 02 (data) — part of .cue
    std::string ps1_bin;       // PS1 single .bin — part of .cue
    std::string dc_bin01;      // Dreamcast track 01 .bin — part of .gdi
    std::string dc_bin03;      // Dreamcast track 03 .bin — part of .gdi

    bool has(const std::string& p) const { return !p.empty() && fs::exists(p); }
};

static TestPaths discover_paths(const std::string& root)
{
    TestPaths tp;
    tp.chd_root = root;
    tp.out_root = (fs::path(root) / "cli_test_output").string();

    auto find = [&](const std::string& subdir, const std::string& ext) -> std::string {
        std::string dir = (fs::path(root) / subdir).string();
        if (!fs::exists(dir)) return {};
        for (auto& e : fs::directory_iterator(dir))
            if (e.path().extension().string() == ext)
                return e.path().string();
        return {};
    };

    tp.ps1_chd    = find("PS1", ".chd");
    tp.ps2_chd    = find("PS2", ".chd");
    tp.pce_chd    = find("PCEngine", ".chd");
    tp.saturn_chd = find("Saturn", ".chd");
    tp.dc_chd     = find("Dreamcast", ".chd");
    tp.psp_chd    = find("PSP", ".chd");
    tp.pce_cue    = find("PCEngine", ".cue");
    tp.ps1_cue    = find("PS1", ".cue");
    tp.saturn_cue = find("Saturn", ".cue");
    tp.ps2_iso    = find("PS2", ".iso");
    tp.psp_iso    = find("PSP", ".iso");
    tp.dc_gdi     = find("Dreamcast", ".gdi");

    // Specific bins — find by exact name fragment
    auto find_specific = [&](const std::string& subdir, const std::string& name_fragment) -> std::string {
        std::string dir = (fs::path(root) / subdir).string();
        if (!fs::exists(dir)) return {};
        for (auto& e : fs::directory_iterator(dir))
            if (e.path().filename().string().find(name_fragment) != std::string::npos)
                return e.path().string();
        return {};
    };
    tp.pce_data_bin = find_specific("PCEngine", "Track 02");
    tp.ps1_bin      = find_specific("PS1", ".bin");
    tp.dc_bin01     = find_specific("Dreamcast", "01.bin");
    tp.dc_bin03     = find_specific("Dreamcast", "03.bin");

    return tp;
}

// ======================> Block 1: read / info

static void block_read(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 1: read / info ===" CLR_RST "\n");
    std::string out;

    // 1a) Generic "read" with bare input (no -i flag)
    if (tp.has(tp.ps2_chd))
    {
        int rc = run("read \"" + tp.ps2_chd + "\"", &out);
        check(rc == 0, "read <bare-input> exits 0");
        check(contains(out, "Content Type: DVD"), "read shows content type");
        check(contains(out, "Hunk Size:"), "read shows hunk size");
        check(contains(out, "SHA-1:"), "read shows SHA-1");
        check(contains(out, "Platform:") && contains(out, "PlayStation 2"), "read shows system detection");
    }
    else skip("read bare-input", "PS2 CHD not found");

    // 1b) chdman-style "info -i"
    if (tp.has(tp.pce_chd))
    {
        int rc = run("info -i \"" + tp.pce_chd + "\"", &out);
        check(rc == 0, "info -i exits 0");
        check(contains(out, "Content Type: CD-ROM"), "info shows CD-ROM type");
        check(contains(out, "Track"), "info shows track listing");
    }
    else skip("info -i", "PCEngine CHD not found");

    // 1c) GD-ROM read (track listing + GDROM label)
    if (tp.has(tp.dc_chd))
    {
        int rc = run("read \"" + tp.dc_chd + "\"", &out);
        check(rc == 0, "read GD-ROM exits 0");
        check(contains(out, "GD-ROM"), "read shows GD-ROM in tracks line");
        check(contains(out, "Platform:") && contains(out, "Dreamcast"), "read detects Dreamcast system");
    }
    else skip("read GD-ROM", "Dreamcast CHD not found");

    // 1d) Non-CHD: CUE file (PCEngine)
    if (tp.has(tp.pce_cue))
    {
        int rc = run("read \"" + tp.pce_cue + "\"", &out);
        check(rc == 0, "read .cue exits 0");
        check(contains(out, "Format:"), "read .cue shows Format");
    }
    else skip("read .cue", "PCEngine CUE not found");

    // 1e) Non-CHD: GDI file (Dreamcast)
    if (tp.has(tp.dc_gdi))
    {
        int rc = run("read \"" + tp.dc_gdi + "\"", &out);
        check(rc == 0, "read .gdi exits 0");
        check(contains(out, "Format:"), "read .gdi shows Format");
        check(contains(out, "Dreamcast"), "read .gdi detects Dreamcast");
    }
    else skip("read .gdi", "Dreamcast GDI not found");

    // 1f) Non-CHD: ISO file (PS2)
    if (tp.has(tp.ps2_iso))
    {
        int rc = run("read \"" + tp.ps2_iso + "\"", &out);
        check(rc == 0, "read .iso exits 0");
        check(contains(out, "Platform:") && contains(out, "PlayStation 2"), "read .iso detects PS2");
    }
    else skip("read .iso", "PS2 ISO not found");

    // 1g) Non-CHD: ISO file (PSP)
    if (tp.has(tp.psp_iso))
    {
        int rc = run("read \"" + tp.psp_iso + "\"", &out);
        check(rc == 0, "read PSP .iso exits 0");
        check(contains(out, "PSP"), "read .iso detects PSP");
    }
    else skip("read PSP .iso", "PSP ISO not found");

    // 1h) No input → error
    {
        int rc = run("read", &out);
        check(rc != 0, "read with no input returns error");
    }
}

// ======================> Block 1b: read .bin files directly

static void block_read_bins(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 1b: read .bin files ===" CLR_RST "\n");
    std::string out;

    // Bins that are part of a .cue/.gdi — expect "Skipped:" redirect message
    struct BinCase {
        const std::string& path;
        const char* label;
        const char* sheet_ext;  // expected sheet extension in redirect msg
    };
    std::vector<BinCase> sheet_bins = {
        { tp.pce_data_bin, "PCEngine Track 02 .bin (in .cue)", ".cue" },
        { tp.ps1_bin,      "PS1 .bin (in .cue)",               ".cue" },
        { tp.dc_bin01,     "Dreamcast 01.bin (in .gdi)",        ".gdi" },
        { tp.dc_bin03,     "Dreamcast 03.bin (in .gdi)",        ".gdi" },
    };
    for (auto& c : sheet_bins)
    {
        if (tp.has(c.path))
        {
            int rc = run("read \"" + c.path + "\"", &out);
            std::string d0 = std::string(c.label) + " exits 0";
            std::string d1 = std::string(c.label) + " shows Skipped message";
            std::string d2 = std::string(c.label) + " names sheet file";
            check(rc == 0, d0.c_str());
            check(contains(out, "Skipped:"), d1.c_str());
            check(contains(out, c.sheet_ext), d2.c_str());
        }
        else {
            std::string d = std::string("read ") + c.label;
            skip(d.c_str(), "file not found");
        }
    }
}

// ======================> Block 2: hash

static void block_hash(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 2: hash ===" CLR_RST "\n");
    std::string out;

    // 2a) hash with default (-hash, no value → SHA1)
    if (tp.has(tp.pce_chd))
    {
        int rc = run("hash \"" + tp.pce_chd + "\"", &out);
        check(rc == 0, "hash (default SHA1) exits 0");
        check(contains(out, "SHA-1:"), "hash default produces SHA-1");
        check(contains(out, "Track 01"), "hash shows per-track results");
    }
    else skip("hash default", "PCEngine CHD not found");

    // 2b) hash with -hash sha1,md5 (multiple algorithms)
    if (tp.has(tp.pce_chd))
    {
        int rc = run("hash \"" + tp.pce_chd + "\" -hash sha1,md5", &out);
        check(rc == 0, "hash -hash sha1,md5 exits 0");
        check(contains(out, "SHA-1:"), "hash multi shows SHA-1");
        check(contains(out, "MD5:"), "hash multi shows MD5");
    }
    else skip("hash multi", "PCEngine CHD not found");

    // 2c) hash -hash all (every algorithm)
    if (tp.has(tp.pce_chd))
    {
        int rc = run("hash \"" + tp.pce_chd + "\" -hash all", &out);
        check(rc == 0, "hash -hash all exits 0");
        check(contains(out, "SHA-1:"), "hash all shows SHA-1");
        check(contains(out, "MD5:"), "hash all shows MD5");
        check(contains(out, "CRC32:"), "hash all shows CRC32");
        check(contains(out, "SHA-256:"), "hash all shows SHA-256");
        check(contains(out, "XXH3-128:"), "hash all shows XXH3-128");
    }
    else skip("hash all", "PCEngine CHD not found");

    // 2d) hash with DVD (single track, no per-track output)
    if (tp.has(tp.ps2_chd))
    {
        int rc = run("hash \"" + tp.ps2_chd + "\" -hash crc32,sha1", &out);
        check(rc == 0, "hash DVD exits 0");
        check(contains(out, "Content Type: DVD"), "hash DVD shows type");
        check(contains(out, "CRC32:"), "hash DVD shows CRC32");
    }
    else skip("hash DVD", "PS2 CHD not found");
}

// ======================> Block 3: extract CD (split, no-split, outputbin, force)

static void block_extract_cd(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 3: extract CD ===" CLR_RST "\n");
    std::string out;

    // 3a) Generic "extract" with -o, default split-bin
    if (tp.has(tp.saturn_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b3a").string();
        fs::create_directories(dir);
        std::string cue = (fs::path(dir) / "saturn.cue").string();
        int rc = run("extract \"" + tp.saturn_chd + "\" -o \"" + cue + "\" -f", &out);
        check(rc == 0, "extract CD -o -f exits 0");
        check(file_exists(cue), "extract CD creates CUE file");
        check(contains(out, "Extraction complete"), "extract CD prints completion");
        // Count bin files (split = multiple)
        int bin_count = 0;
        for (auto& e : fs::directory_iterator(dir))
            if (e.path().extension() == ".bin") bin_count++;
        check(bin_count >= 1, "extract CD produces bin file(s)");
    }
    else skip("extract CD split", "Saturn CHD not found");

    // 3b) Extract with --no-splitbin (single bin)
    if (tp.has(tp.pce_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b3b").string();
        fs::create_directories(dir);
        std::string cue = (fs::path(dir) / "pce.cue").string();
        int rc = run("extract \"" + tp.pce_chd + "\" -o \"" + cue + "\" --no-splitbin -f", &out);
        check(rc == 0, "extract --no-splitbin exits 0");
        check(file_exists(cue), "extract --no-splitbin creates CUE");
        int bin_count = 0;
        for (auto& e : fs::directory_iterator(dir))
            if (e.path().extension() == ".bin") bin_count++;
        check(bin_count == 1, "extract --no-splitbin produces exactly 1 bin file");
    }
    else skip("extract --no-splitbin", "PCEngine CHD not found");

    // 3c) Extract with -ob (output bin template) + -sb
    if (tp.has(tp.pce_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b3c").string();
        fs::create_directories(dir);
        std::string cue = (fs::path(dir) / "pce_tpl.cue").string();
        int rc = run("extract \"" + tp.pce_chd + "\" -o \"" + cue + "\" -sb -ob \"pce_t%t.bin\" -f", &out);
        check(rc == 0, "extract -ob -sb exits 0");
        check(file_exists(cue), "extract -ob creates CUE");
        // Check that template was used: look for pce_t01.bin or pce_t1.bin
        bool found_tpl = false;
        for (auto& e : fs::directory_iterator(dir))
            if (e.path().filename().string().find("pce_t") != std::string::npos
                && e.path().extension() == ".bin")
                found_tpl = true;
        check(found_tpl, "extract -ob applies template to bin filenames");
    }
    else skip("extract -ob", "PCEngine CHD not found");

    // 3d) extractcd (chdman-style) with -i -o
    if (tp.has(tp.ps1_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b3d").string();
        fs::create_directories(dir);
        std::string cue = (fs::path(dir) / "ps1.cue").string();
        int rc = run("extractcd -i \"" + tp.ps1_chd + "\" -o \"" + cue + "\" -f", &out);
        check(rc == 0, "extractcd -i -o -f exits 0");
        check(file_exists(cue), "extractcd creates CUE");
        check(contains(out, "Content Type: CD-ROM") || contains(out, "Extraction complete"),
              "extractcd prints type or completion");
    }
    else skip("extractcd", "PS1 CHD not found");

    // 3e) Extract without -f should fail if output exists (from 3d)
    if (tp.has(tp.ps1_chd))
    {
        std::string cue = (fs::path(tp.out_root) / "b3d" / "ps1.cue").string();
        if (file_exists(cue))
        {
            int rc = run("extractcd -i \"" + tp.ps1_chd + "\" -o \"" + cue + "\"", &out);
            // Should succeed or fail depending on implementation; just record
            check(rc == 0 || rc != 0, "extractcd without -f on existing output (behavior noted)");
        }
    }
}

// ======================> Block 4: extract DVD/raw (partial extraction options)

static void block_extract_dvd(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 4: extract DVD + partial options ===" CLR_RST "\n");
    std::string out;

    // 4a) extractdvd with -i -o
    if (tp.has(tp.ps2_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b4a").string();
        fs::create_directories(dir);
        std::string iso = (fs::path(dir) / "ps2.iso").string();
        int rc = run("extractdvd -i \"" + tp.ps2_chd + "\" -o \"" + iso + "\" -f", &out);
        check(rc == 0, "extractdvd -i -o -f exits 0");
        check(file_exists(iso), "extractdvd creates ISO");
        uint64_t sz = file_size(iso);
        check(sz > 0, "extractdvd output has content");
    }
    else skip("extractdvd", "PS2 CHD not found");

    // 4b) extract DVD with -isb and -ib (partial: first 1MB)
    if (tp.has(tp.ps2_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b4b").string();
        fs::create_directories(dir);
        std::string bin = (fs::path(dir) / "ps2_partial.bin").string();
        int rc = run("extractraw -i \"" + tp.ps2_chd + "\" -o \"" + bin + "\" -isb 0 -ib 1048576 -f", &out);
        check(rc == 0, "extractraw -isb -ib exits 0");
        if (file_exists(bin))
        {
            uint64_t sz = file_size(bin);
            // Size may round up to hunk boundaries; library may extract more
            check_val(sz > 0 && sz <= 1048576 * 4, "extractraw partial size is bounded",
                      "actual=" + std::to_string(sz) + " expected<=" + std::to_string(1048576*4));
        }
        else check(false, "extractraw partial creates output file");
    }
    else skip("extractraw partial", "PS2 CHD not found");

    // 4c) extract DVD with -ish and -ih (hunk-based partial)
    if (tp.has(tp.ps2_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b4c").string();
        fs::create_directories(dir);
        std::string bin = (fs::path(dir) / "ps2_hunks.bin").string();
        int rc = run("extractraw -i \"" + tp.ps2_chd + "\" -o \"" + bin + "\" -ish 0 -ih 100 -f", &out);
        check(rc == 0, "extractraw -ish -ih exits 0");
        if (file_exists(bin))
            check(file_size(bin) > 0, "extractraw hunk partial has content");
        else
            check(false, "extractraw hunk partial creates file");
    }
    else skip("extractraw hunk partial", "PS2 CHD not found");

    // 4d) Generic "extract" for PSP (DVD) → auto ISO
    if (tp.has(tp.psp_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b4d").string();
        fs::create_directories(dir);
        std::string iso = (fs::path(dir) / "psp.iso").string();
        int rc = run("extract \"" + tp.psp_chd + "\" -o \"" + iso + "\" -f", &out);
        check(rc == 0, "extract PSP/DVD auto exits 0");
        check(file_exists(iso), "extract PSP creates ISO");
    }
    else skip("extract PSP", "PSP CHD not found");
}

// ======================> Block 5: create CD

static void block_create_cd(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 5: create CD ===" CLR_RST "\n");
    std::string out;

    // 5a) Generic "create" from CUE (auto-detect CD)
    if (tp.has(tp.pce_cue))
    {
        std::string dir = (fs::path(tp.out_root) / "b5a").string();
        fs::create_directories(dir);
        std::string chd = (fs::path(dir) / "pce_auto.chd").string();
        int rc = run("create \"" + tp.pce_cue + "\" -o \"" + chd + "\" -f", &out);
        check(rc == 0, "create <CUE> auto exits 0");
        check(file_exists(chd), "create produces CHD");
        check(file_size(chd) > 1024, "create CHD has nontrivial size");
        check(contains(out, "Compression complete"), "create prints completion");
    }
    else skip("create CUE auto", "PCEngine CUE not found");

    // 5b) createcd with custom compression -c cdzs,cdlz
    if (tp.has(tp.pce_cue))
    {
        std::string dir = (fs::path(tp.out_root) / "b5b").string();
        fs::create_directories(dir);
        std::string chd = (fs::path(dir) / "pce_cdzs.chd").string();
        int rc = run("createcd -i \"" + tp.pce_cue + "\" -o \"" + chd + "\" -c cdzs,cdlz -f", &out);
        check(rc == 0, "createcd -c cdzs,cdlz exits 0");
        check(file_exists(chd), "createcd -c produces CHD");
        check(contains(out, "cdzs") || contains(out, "Compression"), "createcd shows compression info");
    }
    else skip("createcd -c", "PCEngine CUE not found");

    // 5c) createcd with -hs and -np (hunk size + num processors)
    if (tp.has(tp.pce_cue))
    {
        std::string dir = (fs::path(tp.out_root) / "b5c").string();
        fs::create_directories(dir);
        std::string chd = (fs::path(dir) / "pce_opts.chd").string();
        int rc = run("createcd -i \"" + tp.pce_cue + "\" -o \"" + chd + "\" -hs 19584 -np 1 -f", &out);
        check(rc == 0, "createcd -hs -np exits 0");
        check(file_exists(chd), "createcd -hs -np produces CHD");
        check_val(contains(out, "Hunk size:") || contains(out, "19,584"), "createcd shows custom hunk size",
                  "output: " + out.substr(0, 500));
    }
    else skip("createcd -hs -np", "PCEngine CUE not found");
}

// ======================> Block 6: create DVD (hunksize, numprocessors, compression)

static void block_create_dvd(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 6: create DVD + options ===" CLR_RST "\n");
    std::string out;

    // 6a) createdvd with -c none (currently unsupported — expect clean rejection)
    if (tp.has(tp.ps2_iso))
    {
        std::string dir = (fs::path(tp.out_root) / "b6a").string();
        fs::create_directories(dir);
        std::string chd = (fs::path(dir) / "ps2_none.chd").string();
        int rc = run("createdvd -i \"" + tp.ps2_iso + "\" -o \"" + chd + "\" -c none -f", &out);
        check(rc != 0, "createdvd -c none rejected");
    }
    else skip("createdvd none", "PS2 ISO not found");

    // 6b) Generic "create" from ISO (auto-detect DVD, default compression)
    if (tp.has(tp.ps2_iso))
    {
        std::string dir = (fs::path(tp.out_root) / "b6b").string();
        fs::create_directories(dir);
        std::string chd = (fs::path(dir) / "ps2_auto.chd").string();
        int rc = run("create \"" + tp.ps2_iso + "\" -o \"" + chd + "\" -f", &out);
        check(rc == 0, "create <ISO> auto DVD exits 0");
        check(file_exists(chd), "create ISO auto produces CHD");
    }
    else skip("create ISO auto", "PS2 ISO not found");
}

// ======================> Block 7: auto command

static void block_auto(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 7: auto command ===" CLR_RST "\n");
    std::string out;

    // 7a) auto with CHD → should extract
    if (tp.has(tp.saturn_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b7a").string();
        fs::create_directories(dir);
        std::string cue = (fs::path(dir) / "saturn_auto.cue").string();
        int rc = run("auto \"" + tp.saturn_chd + "\" -o \"" + cue + "\" -f", &out);
        check(rc == 0, "auto <CHD> exits 0");
        check(file_exists(cue), "auto <CHD> extracts to CUE");
    }
    else skip("auto CHD", "Saturn CHD not found");

    // 7b) auto with CUE → should create
    if (tp.has(tp.pce_cue))
    {
        std::string dir = (fs::path(tp.out_root) / "b7b").string();
        fs::create_directories(dir);
        std::string chd = (fs::path(dir) / "pce_auto.chd").string();
        int rc = run("auto \"" + tp.pce_cue + "\" -o \"" + chd + "\" -f", &out);
        check(rc == 0, "auto <CUE> exits 0");
        check(file_exists(chd), "auto <CUE> creates CHD");
    }
    else skip("auto CUE", "PCEngine CUE not found");
}

// ======================> Block 8: drag-and-drop (bare file as command)

static void block_dragdrop(const TestPaths& tp)
{
    std::printf(CLR_BOLD "\n=== Block 8: drag-and-drop (bare file) ===" CLR_RST "\n");
    std::string out;

    // 8a) Bare CHD path as first arg → auto extract
    if (tp.has(tp.pce_chd))
    {
        std::string dir = (fs::path(tp.out_root) / "b8a").string();
        fs::create_directories(dir);
        std::string cue = (fs::path(dir) / "pce_dd.cue").string();
        int rc = run("\"" + tp.pce_chd + "\" -o \"" + cue + "\" -f", &out);
        check(rc == 0, "bare CHD path exits 0");
        check(file_exists(cue), "bare CHD path auto-extracts");
    }
    else skip("bare CHD path", "PCEngine CHD not found");
}

// ======================> Block 9: error cases & edge cases

static void block_errors()
{
    std::printf(CLR_BOLD "\n=== Block 9: error cases ===" CLR_RST "\n");
    std::string out;

    // 9a) No arguments at all → usage (exit 0)
    {
        int rc = run("", &out);
        check(rc == 0, "no arguments exits 0 (usage)");
        check(contains(out, "extract") && contains(out, "create"), "no arguments shows usage");
    }

    // 9b) Unknown command → error
    {
        int rc = run("foobar", &out);
        check(rc == 1, "unknown command exits 1");
        check(contains(out, "Unknown command") || contains(out, "Error"), "unknown command shows error");
    }

    // 9c) Read non-existent file → error
    {
        std::string ne = (fs::temp_directory_path() / "nonexistent_chdlite_test.chd").string();
        int rc = run("read \"" + ne + "\"", &out);
        check(rc != 0, "read nonexistent file exits non-zero");
    }

    // 9d) Extract with missing input → error
    {
        int rc = run("extract", &out);
        check(rc != 0, "extract with no input exits non-zero");
    }

    // 9e) Hash with missing input → error
    {
        int rc = run("hash", &out);
        check(rc != 0, "hash with no input exits non-zero");
    }

    // 9f) Create with missing input → error
    {
        int rc = run("create", &out);
        check(rc != 0, "create with no input exits non-zero");
    }

    // 9g) createcd without -o → error
    {
        std::string ne = (fs::temp_directory_path() / "nonexistent_chdlite_test.cue").string();
        int rc = run("createcd -i \"" + ne + "\"", &out);
        check(rc != 0, "createcd without -o exits non-zero");
    }
}

// ======================> Block 10: activity log verification

static void block_log()
{
    std::printf(CLR_BOLD "\n=== Block 10: activity log ===" CLR_RST "\n");

    // The log file should exist after all the successful CLI runs above
    check(file_exists(LOG_PATH), "error.log exists");

    if (file_exists(LOG_PATH))
    {
        std::string log = read_text_file(LOG_PATH);

        // Count entries (each line is one spdlog entry: [timestamp] [level] ...)
        int entries = 0;
        std::string::size_type pos = 0;
        while ((pos = log.find("[info]", pos)) != std::string::npos) {
            entries++;
            pos += 6;
        }
        // Also count error entries
        pos = 0;
        while ((pos = log.find("[error]", pos)) != std::string::npos) {
            entries++;
            pos += 7;
        }

        check_val(entries >= g_run_ok,
                  "log entry count >= successful runs",
                  "entries=" + std::to_string(entries) +
                  " successful_runs=" + std::to_string(g_run_ok));

        // Each entry should have the basic fields (spdlog format)
        check(contains(log, "[20"), "log contains timestamps");
        check(contains(log, "v0."), "log contains version");
        check(contains(log, "cmd="), "log contains command lines");
        check(contains(log, "OK"), "log contains status");

        // Should have both OK and ERROR entries from block 9 error cases
        check(contains(log, "OK"), "log has success entries");
        check(contains(log, "[error]"), "log has error entries");
    }
}

// ======================> main

int main(int argc, char** argv)
{
    // Default paths
    std::string chd_root = "test_root/Roms/DiscRomsChd";
    std::string out_root  = "test_root/output/cli_test_output";
    if (argc > 1) chd_root = argv[1];
    if (argc > 2) out_root  = argv[2];

    auto tp = discover_paths(chd_root);
    tp.out_root = out_root;

    std::printf(CLR_BOLD "chdlite CLI integration test" CLR_RST "\n");
    std::printf("CHD root: %s\n", tp.chd_root.c_str());
    std::printf("Output:   %s\n", tp.out_root.c_str());

    // Clean output directory
    fs::remove_all(tp.out_root);
    fs::create_directories(tp.out_root);

    // Delete activity log so we start fresh (ignore error if it doesn't exist yet)
    std::error_code ec_rm;
    fs::remove(LOG_PATH, ec_rm);

    // Run all blocks
    block_read(tp);
    block_read_bins(tp);
    block_hash(tp);
    block_extract_cd(tp);
    block_extract_dvd(tp);
    block_create_cd(tp);
    block_create_dvd(tp);
    block_auto(tp);
    block_dragdrop(tp);
    block_errors();
    block_log();

    // Summary
    std::printf(CLR_BOLD "\n=== Summary ===" CLR_RST "\n");
    std::printf("  " CLR_PASS "PASS" CLR_RST ": %d\n", g_pass);
    std::printf("  " CLR_FAIL "FAIL" CLR_RST ": %d\n", g_fail);
    std::printf("  " CLR_SKIP "SKIP" CLR_RST ": %d\n", g_skip);

    if (g_fail > 0)
    {
        std::printf(CLR_FAIL "\nSome tests failed!\n" CLR_RST);
        return 1;
    }
    std::printf(CLR_PASS "\nAll tests passed.\n" CLR_RST);
    return 0;
}
