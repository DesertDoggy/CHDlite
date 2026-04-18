// license:GPLv3
// CHDlite - Cross-platform CHD library
// tests/benchmark_chd.cpp - Compression benchmarking tool

#include <chd_api.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include <string>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <numeric>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <unistd.h>
#endif

// Access the OSD thread count (defined in osdsync.cpp, same as CLI does)
extern int osd_num_processors;

namespace fs = std::filesystem;
using namespace chdlite;
using namespace std::chrono;

// ============================================================================
// Data Structures
// ============================================================================

struct BenchmarkConfig {
    std::vector<std::string> input_paths;
    bool recursive = true;
    std::vector<std::string> platform_skip;
    std::vector<std::string> codec_presets;  // e.g. "best", "lzma", "zlib", "flac_huff", "zstd"
    int repetitions = 1;
    int num_processors = 0;
    bool verify_integrity = true;
    fs::path output_root;
    bool keep_last_output = false;
    std::vector<std::string> output_formats;
    bool output_log = true;
    bool auto_detect_format = true;
    fs::path chdman_path;
    fs::path chdlite_path;
    bool benchmark_chdman = false;
    bool benchmark_chdlite = true;
};

struct CodecInfo {
    int id;
    std::string name;
    Codec codec;
};

struct TimingResult {
    std::string tool;  // "chdlite" or "chdman"
    std::string preset_name;  // e.g. "best", "lzma"
    std::string input_file;
    std::string format;
    std::vector<int> codec_combo;  // resolved CD or DVD codec IDs
    double compression_time_ms = 0.0;
    double decompression_time_ms = 0.0;
    double compression_speed_mbps = 0.0;
    double decompression_speed_mbps = 0.0;
    double compression_ratio = 0.0;
    size_t peak_memory_bytes = 0;
    uint64_t original_size = 0;
    uint64_t compressed_size = 0;
};

// ============================================================================
// Codec Mapping
// ============================================================================

std::map<int, CodecInfo> g_codec_map = {
    {1, {1, "Zlib", Codec::Zlib}},
    {2, {2, "ZlibPlus", Codec::ZlibPlus}},
    {3, {3, "Zstd", Codec::Zstd}},
    {4, {4, "LZMA", Codec::LZMA}},
    {5, {5, "FLAC", Codec::FLAC}},
    {6, {6, "Huffman", Codec::Huffman}},
    {9, {9, "CD_Zlib", Codec::CD_Zlib}},
    {10, {10, "CD_Zstd", Codec::CD_Zstd}},
    {11, {11, "CD_LZMA", Codec::CD_LZMA}},
    {12, {12, "CD_FLAC", Codec::CD_FLAC}}
};

// Codec ID → chdman command-line codec name mapping
std::map<int, std::string> g_chdman_codec_name = {
    {1, "zlib"},
    {3, "zstd"},
    {4, "lzma"},
    {5, "flac"},
    {6, "huff"},
    {9, "cdzl"},
    {10, "cdzs"},
    {11, "cdlz"},
    {12, "cdfl"}
};

// Format-agnostic codec presets: auto-selects CD or DVD codecs based on input
struct CodecPreset {
    std::string name;
    std::vector<int> cd_codecs;   // codec IDs for CD/GD-ROM content
    std::vector<int> dvd_codecs;  // codec IDs for DVD/ISO content
};

std::map<std::string, CodecPreset> g_codec_presets = {
    {"best",      {"best",      {11, 10, 9, 12}, {3, 4, 1, 6}}},
    {"lzma",      {"lzma",      {11},            {4}}},
    {"zlib",      {"zlib",      {9},             {1}}},
    {"flac_huff", {"flac_huff", {12},            {6}}},
    {"zstd",      {"zstd",      {10},            {3}}},
};

// Resolve a preset name to actual codec IDs based on whether input is CD or DVD
bool is_cd_format(const std::string& format) {
    return format == "CUE" || format == "GDI";
}

std::vector<int> resolve_preset(const std::string& preset_name, const std::string& format) {
    auto it = g_codec_presets.find(preset_name);
    if (it == g_codec_presets.end()) return {};
    return is_cd_format(format) ? it->second.cd_codecs : it->second.dvd_codecs;
}

// ============================================================================
// Utility Functions
// ============================================================================

// Forward declarations
void trim_string(std::string& str);
std::vector<std::string> split_string(const std::string& str, char delim);

size_t get_peak_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize;
    }
    return 0;
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmPeak:") == 0) {
            size_t kb;
            if (sscanf(line.c_str(), "VmPeak: %zu", &kb) == 1) {
                return kb * 1024;
            }
        }
    }
    return 0;
#endif
}

std::string expand_path(const std::string& path, const fs::path& basedir) {
    std::string result = path;
    
    // Replace ${basedir}
    size_t pos = result.find("${basedir}");
    if (pos != std::string::npos) {
        result.replace(pos, 10, basedir.string());
    }
    
    // Replace ~ with home directory
    if (result[0] == '~') {
        const char* home = std::getenv("HOME");
        if (!home) home = std::getenv("USERPROFILE"); // Windows
        if (home) {
            result.replace(0, 1, home);
        }
    }
    
    return result;
}

std::vector<std::string> split_string(const std::string& str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delim)) {
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

void trim_string(std::string& str) {
    str.erase(0, str.find_first_not_of(" \t\r\n"));
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

std::string detect_format(const std::string& filepath) {
    fs::path p(filepath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".cue") return "CUE";
    if (ext == ".gdi") return "GDI";
    if (ext == ".iso") return "ISO";
    if (ext == ".chd") return "CHD";
    if (ext == ".nrg") return "NRG";
    if (ext == ".toc") return "TOC";
    
    return "UNKNOWN";
}

std::vector<std::string> discover_input_files(const BenchmarkConfig& config) {
    std::vector<std::string> files;
    // Include all disc image formats: .iso for DVD, .cue/.gdi for CD/GD-ROM
    std::vector<std::string> extensions = {".chd", ".iso", ".cue", ".gdi"};

    for (const auto& path_str : config.input_paths) {
        try {
            fs::path path(path_str);
            
            if (config.recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                            files.push_back(entry.path().string());
                        }
                    }
                }
            } else {
                for (const auto& entry : fs::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                            files.push_back(entry.path().string());
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error discovering files in " << path_str << ": " << e.what() << std::endl;
        }
    }
    
    std::sort(files.begin(), files.end());
    return files;
}

// ============================================================================
// INI Parser
// ============================================================================

class IniParser {
public:
    bool parse(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << filename << std::endl;
            return false;
        }

        std::string line;
        std::string current_section;
        std::string last_section;
        std::string last_key;

        while (std::getline(file, line)) {
            trim_string(line);
            
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line[0] == '[' && line[line.length() - 1] == ']') {
                current_section = line.substr(1, line.length() - 2);
                trim_string(current_section);
                last_key.clear();
                continue;
            }

            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);
                trim_string(key);
                trim_string(value);
                
                m_data[current_section][key] = value;
                last_section = current_section;
                last_key = key;
            } else if (!last_key.empty()) {
                // Continuation line: append to previous key with newline separator
                m_data[last_section][last_key] += "\n" + line;
            }
        }

        return true;
    }

    std::string get(const std::string& section, const std::string& key, const std::string& default_val = "") {
        auto it = m_data.find(section);
        if (it != m_data.end()) {
            auto kit = it->second.find(key);
            if (kit != it->second.end()) {
                return kit->second;
            }
        }
        return default_val;
    }

    bool get_bool(const std::string& section, const std::string& key, bool default_val = false) {
        std::string val = get(section, key, "");
        if (val.empty()) return default_val;
        return val == "true" || val == "True" || val == "TRUE" || val == "1" || val == "yes";
    }

    int get_int(const std::string& section, const std::string& key, int default_val = 0) {
        std::string val = get(section, key, "");
        if (val.empty()) return default_val;
        try {
            return std::stoi(val);
        } catch (...) {
            return default_val;
        }
    }

private:
    std::map<std::string, std::map<std::string, std::string>> m_data;
};

// ============================================================================
// Config Loading
// ============================================================================

bool load_config(const std::string& config_file, BenchmarkConfig& config) {
    IniParser ini;
    if (!ini.parse(config_file)) {
        return false;
    }

    fs::path basedir = fs::path(config_file).parent_path();

    std::string paths_str = ini.get("input", "paths");
    if (!paths_str.empty()) {
        auto paths = split_string(paths_str, ';');
        for (auto& p : paths) {
            trim_string(p);
            config.input_paths.push_back(expand_path(p, basedir));
        }
    }

    config.recursive = ini.get_bool("input", "recursive", true);

    std::string skip_str = ini.get("input", "platform_skip", "");
    if (!skip_str.empty()) {
        config.platform_skip = split_string(skip_str, ',');
        for (auto& s : config.platform_skip) trim_string(s);
    }

    config.auto_detect_format = ini.get_bool("input", "auto_detect_format", true);

    // Tool paths
    std::string chdman_str = ini.get("tools", "chdman_path", "");
    if (!chdman_str.empty()) {
        config.chdman_path = expand_path(chdman_str, basedir);
    }

    std::string chdlite_str = ini.get("tools", "chdlite_path", "");
    if (!chdlite_str.empty()) {
        config.chdlite_path = expand_path(chdlite_str, basedir);
    } else {
        config.chdlite_path = "chdlite.exe";  // Default to PATH
    }

    // Parse codec presets (format-agnostic names like "best", "lzma", etc.)
    std::string codec_str = ini.get("codecs", "list", "");
    if (!codec_str.empty()) {
        auto lines = split_string(codec_str, '\n');
        for (auto& line : lines) {
            trim_string(line);
            if (!line.empty() && g_codec_presets.count(line)) {
                config.codec_presets.push_back(line);
            } else if (!line.empty()) {
                std::cerr << "Warning: unknown codec preset '" << line << "', skipping\n";
            }
        }
    }

    config.repetitions = ini.get_int("benchmark", "repetitions", 1);
    config.num_processors = ini.get_int("benchmark", "num_processors", 0);
    config.verify_integrity = ini.get_bool("benchmark", "verify_integrity", true);
    
    std::string output_root_str = ini.get("benchmark", "output_root", "./benchmark_results");
    config.output_root = expand_path(output_root_str, basedir);
    config.keep_last_output = ini.get_bool("benchmark", "keep_last_output", false);

    // Benchmark selection
    config.benchmark_chdlite = ini.get_bool("benchmark_selection", "benchmark_chdlite", true);
    config.benchmark_chdman = ini.get_bool("benchmark_selection", "benchmark_chdman", false);

    std::string formats_str = ini.get("output", "output_formats", "text,csv,json");
    config.output_formats = split_string(formats_str, ',');
    for (auto& fmt : config.output_formats) trim_string(fmt);

    config.output_log = ini.get_bool("output", "output_log", true);

    return true;
}

// ============================================================================
// Benchmark Execution
// ============================================================================

// Get total input size for a disc image (sums all tracks for cue/gdi)
uint64_t get_input_size(const std::string& input_file) {
    std::string fmt = detect_format(input_file);
    if (fmt == "ISO" || fmt == "CHD") {
        return fs::file_size(input_file);
    }
    // For CUE/GDI, sum all files in the same directory with the same base name prefix
    // or just sum all bin/raw/iso files in the directory
    fs::path dir = fs::path(input_file).parent_path();
    uint64_t total = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".bin" || ext == ".raw" || ext == ".img" || ext == ".iso") {
                total += entry.file_size();
            }
        }
    }
    return total > 0 ? total : fs::file_size(input_file);
}

// Build chdman codec string from codec combo IDs: e.g. "cdlz,cdzs,cdzl,cdfl"
std::string build_chdman_codec_string(const std::vector<int>& codec_combo) {
    std::string result;
    for (size_t i = 0; i < codec_combo.size(); ++i) {
        if (i > 0) result += ",";
        if (g_chdman_codec_name.count(codec_combo[i])) {
            result += g_chdman_codec_name[codec_combo[i]];
        }
    }
    return result;
}

// Determine if resolved codec IDs are CD compound codecs
// Run an external process and return its exit code
int run_process(const std::string& command) {
#ifdef _WIN32
    // Use CreateProcess for proper timing on Windows
    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    // Redirect stdout/stderr to NUL
    HANDLE hNull = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    si.hStdOutput = hNull;
    si.hStdError = hNull;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::string cmd_copy = command;  // CreateProcess needs non-const
    if (!CreateProcessA(nullptr, cmd_copy.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    return static_cast<int>(exit_code);
#else
    return system((command + " > /dev/null 2>&1").c_str());
#endif
}

// Build a quoted command string
std::string quote(const std::string& s) {
    return "\"" + s + "\"";
}

// ---- CHDlite benchmark (uses library directly) ----
TimingResult run_benchmark_chdlite(const std::string& input_file, 
                          const std::vector<int>& codec_combo,
                          const BenchmarkConfig& config) {
    TimingResult result;
    result.tool = "chdlite";
    result.input_file = input_file;
    result.codec_combo = codec_combo;
    result.format = detect_format(input_file);

    fs::path temp_output = config.output_root / "temp_chdlite.chd";
    fs::create_directories(config.output_root);

    ChdArchiver archiver;
    ArchiveOptions opts;

    // Enforce thread count so chdlite matches chdman -np
    opts.num_processors = config.num_processors;
    if (config.num_processors > 0)
        osd_num_processors = config.num_processors;

    // Convert codec combination to compression array
    for (size_t i = 0; i < codec_combo.size() && i < 4; ++i) {
        int codec_id = codec_combo[i];
        if (g_codec_map.count(codec_id)) {
            opts.compression[i] = g_codec_map[codec_id].codec;
        }
    }

    try {
        result.original_size = get_input_size(input_file);

        get_peak_memory(); // Warmup

        // In-place progress: overwrite same line with compress %
        int last_pct = -1;
        opts.progress_callback = [&last_pct](uint64_t done, uint64_t total) -> bool {
            int pct = total > 0 ? static_cast<int>(100 * done / total) : 0;
            if (pct != last_pct) {
                last_pct = pct;
                std::cout << "\r  [chdlite] compressing... " << pct << "%   ";
                std::cout.flush();
            }
            return true;
        };

        auto comp_start = high_resolution_clock::now();
        ArchiveResult comp_result = archiver.archive(input_file, temp_output.string(), opts);
        auto comp_end = high_resolution_clock::now();

        // Clear the progress line
        std::cout << "\r" << std::string(60, ' ') << "\r";
        std::cout.flush();

        if (!comp_result.success) {
            std::cerr << "Compression failed: " << input_file << std::endl;
            if (fs::exists(temp_output)) fs::remove(temp_output);
            return result;
        }

        result.compression_time_ms = duration<double, std::milli>(comp_end - comp_start).count();
        result.peak_memory_bytes = get_peak_memory();
        result.compressed_size = fs::file_size(temp_output);
        result.compression_ratio = 100.0 * result.compressed_size / result.original_size;
        result.compression_speed_mbps = (result.original_size / (1024.0 * 1024.0)) / (result.compression_time_ms / 1000.0);

        // Decompression phase: extract to disk (same as chdman extractcd/extractdvd)
        {
            fs::path temp_extract = config.output_root / "temp_chdlite_extract";
            if (fs::exists(temp_extract)) fs::remove_all(temp_extract);
            fs::create_directories(temp_extract);

            try {
                ChdExtractor extractor;
                ExtractOptions ext_opts;
                ext_opts.output_dir = temp_extract.string();
                ext_opts.force_overwrite = true;

                auto decomp_start = high_resolution_clock::now();
                ExtractionResult ext_result = extractor.extract(temp_output.string(), ext_opts);
                auto decomp_end = high_resolution_clock::now();

                if (ext_result.success) {
                    result.decompression_time_ms = duration<double, std::milli>(decomp_end - decomp_start).count();
                    result.decompression_speed_mbps = (result.original_size / (1024.0 * 1024.0)) / (result.decompression_time_ms / 1000.0);
                } else {
                    std::cerr << "chdlite decompression (extract) failed" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error during chdlite extraction: " << e.what() << std::endl;
            }

            if (fs::exists(temp_extract)) fs::remove_all(temp_extract);
        }

    } catch (const std::exception& e) {
        std::cerr << "Benchmark exception: " << e.what() << std::endl;
    }

    if (fs::exists(temp_output)) {
        fs::remove(temp_output);
    }

    return result;
}

// ---- CHDman benchmark (shells out to chdman.exe) ----
TimingResult run_benchmark_chdman(const std::string& input_file,
                                  const std::vector<int>& codec_combo,
                                  const BenchmarkConfig& config) {
    TimingResult result;
    result.tool = "chdman";
    result.input_file = input_file;
    result.codec_combo = codec_combo;
    result.format = detect_format(input_file);

    fs::path temp_chd = config.output_root / "temp_chdman.chd";
    fs::path temp_extract = config.output_root / "temp_chdman_extract";
    fs::create_directories(config.output_root);

    // Clean up any leftover temp files
    if (fs::exists(temp_chd)) fs::remove(temp_chd);

    bool cd_mode = is_cd_format(result.format);
    std::string codec_str = build_chdman_codec_string(codec_combo);
    std::string np_str = std::to_string(config.num_processors == 0 ? 0 : config.num_processors);

    try {
        result.original_size = get_input_size(input_file);

        // === Compression phase ===
        // chdman createcd -i input -o output.chd -c codecs -np N
        // chdman createdvd -i input -o output.chd -c codecs -np N
        std::string create_cmd;
        if (cd_mode) {
            create_cmd = quote(config.chdman_path.string()) +
                " createcd -i " + quote(input_file) +
                " -o " + quote(temp_chd.string()) +
                " -c " + codec_str;
        } else {
            create_cmd = quote(config.chdman_path.string()) +
                " createdvd -i " + quote(input_file) +
                " -o " + quote(temp_chd.string()) +
                " -c " + codec_str;
        }
        if (config.num_processors > 0) {
            create_cmd += " -np " + np_str;
        }

        auto comp_start = high_resolution_clock::now();
        int comp_exit = run_process(create_cmd);
        auto comp_end = high_resolution_clock::now();

        if (comp_exit != 0 || !fs::exists(temp_chd)) {
            std::cerr << "chdman compression failed (exit=" << comp_exit << "): " << input_file << std::endl;
            if (fs::exists(temp_chd)) fs::remove(temp_chd);
            return result;
        }

        result.compression_time_ms = duration<double, std::milli>(comp_end - comp_start).count();
        result.compressed_size = fs::file_size(temp_chd);
        result.compression_ratio = 100.0 * result.compressed_size / result.original_size;
        result.compression_speed_mbps = (result.original_size / (1024.0 * 1024.0)) / (result.compression_time_ms / 1000.0);
        result.peak_memory_bytes = get_peak_memory(); // approximation

        // === Decompression phase ===
        if (config.verify_integrity) {
            // Clean up extract temp dir
            if (fs::exists(temp_extract)) fs::remove_all(temp_extract);
            fs::create_directories(temp_extract);

            std::string extract_output;
            std::string extract_cmd;

            if (cd_mode) {
                // Use .gdi output for GDI sources, .cue for everything else
                // -sb = --splitbin: one .bin per track (flag, no argument)
                std::string out_ext = (result.format == "GDI") ? ".gdi" : ".cue";
                extract_output = (temp_extract / ("temp_out" + out_ext)).string();
                extract_cmd = quote(config.chdman_path.string()) +
                    " extractcd -i " + quote(temp_chd.string()) +
                    " -o " + quote(extract_output) +
                    " -sb";
            } else {
                extract_output = (temp_extract / "temp_out.iso").string();
                extract_cmd = quote(config.chdman_path.string()) +
                    " extractdvd -i " + quote(temp_chd.string()) +
                    " -o " + quote(extract_output);
            }

            auto decomp_start = high_resolution_clock::now();
            int decomp_exit = run_process(extract_cmd);
            auto decomp_end = high_resolution_clock::now();

            if (decomp_exit == 0) {
                result.decompression_time_ms = duration<double, std::milli>(decomp_end - decomp_start).count();
                result.decompression_speed_mbps = (result.compressed_size / (1024.0 * 1024.0)) / (result.decompression_time_ms / 1000.0);
            } else {
                std::cerr << "chdman decompression failed (exit=" << decomp_exit << ")" << std::endl;
            }

            // Clean up extract temp
            if (fs::exists(temp_extract)) fs::remove_all(temp_extract);
        }

    } catch (const std::exception& e) {
        std::cerr << "chdman benchmark exception: " << e.what() << std::endl;
    }

    if (fs::exists(temp_chd)) fs::remove(temp_chd);

    return result;
}

// ============================================================================
// Output Generation
// ============================================================================

void generate_text_report(const std::vector<TimingResult>& results, const fs::path& output_file) {
    std::ofstream out(output_file);
    
    out << "CHDlite vs CHDman Benchmark Report\n";
    out << "===================================\n\n";

    std::map<std::tuple<std::string, std::string, std::string, std::vector<int>>, std::vector<TimingResult>> grouped;
    for (const auto& r : results) {
        grouped[{r.tool, r.preset_name, r.input_file, r.codec_combo}].push_back(r);
    }

    for (const auto& group : grouped) {
        out << "Tool: " << std::get<0>(group.first) << "\n";
        out << "Preset: " << std::get<1>(group.first) << "\n";
        out << "File: " << fs::path(std::get<2>(group.first)).filename().string() << "\n";
        out << "Format: " << group.second[0].format << "\n";
        out << "Codecs: ";
        for (size_t i = 0; i < std::get<3>(group.first).size(); ++i) {
            if (i > 0) out << " + ";
            if (g_codec_map.count(std::get<3>(group.first)[i])) {
                out << g_codec_map[std::get<3>(group.first)[i]].name;
            }
        }
        out << "\n";
        out << "-----------------------------------------\n";

        if (group.second.size() == 1) {
            const auto& r = group.second[0];
            out << "Original Size:        " << r.original_size << " bytes\n";
            out << "Compressed Size:      " << r.compressed_size << " bytes\n";
            out << "Compression Ratio:    " << std::fixed << std::setprecision(2) << r.compression_ratio << "%\n";
            out << "Compression Time:     " << r.compression_time_ms << " ms\n";
            out << "Compression Speed:    " << r.compression_speed_mbps << " MB/s\n";
            out << "Decompression Time:   " << r.decompression_time_ms << " ms\n";
            out << "Decompression Speed:  " << r.decompression_speed_mbps << " MB/s\n";
            out << "Peak Memory:          " << r.peak_memory_bytes / (1024*1024) << " MB\n";
        } else {
            // Calculate averages
            double avg_comp_time = 0, avg_decomp_time = 0, avg_comp_speed = 0, avg_decomp_speed = 0;
            double std_comp_time = 0, std_decomp_time = 0, std_comp_speed = 0, std_decomp_speed = 0;
            size_t avg_peak_mem = 0;

            for (const auto& r : group.second) {
                avg_comp_time += r.compression_time_ms;
                avg_decomp_time += r.decompression_time_ms;
                avg_comp_speed += r.compression_speed_mbps;
                avg_decomp_speed += r.decompression_speed_mbps;
                avg_peak_mem += r.peak_memory_bytes;
            }
            avg_comp_time /= group.second.size();
            avg_decomp_time /= group.second.size();
            avg_comp_speed /= group.second.size();
            avg_decomp_speed /= group.second.size();
            avg_peak_mem /= group.second.size();

            // Calculate stddev
            for (const auto& r : group.second) {
                std_comp_time += (r.compression_time_ms - avg_comp_time) * (r.compression_time_ms - avg_comp_time);
                std_decomp_time += (r.decompression_time_ms - avg_decomp_time) * (r.decompression_time_ms - avg_decomp_time);
                std_comp_speed += (r.compression_speed_mbps - avg_comp_speed) * (r.compression_speed_mbps - avg_comp_speed);
                std_decomp_speed += (r.decompression_speed_mbps - avg_decomp_speed) * (r.decompression_speed_mbps - avg_decomp_speed);
            }
            std_comp_time = std::sqrt(std_comp_time / group.second.size());
            std_decomp_time = std::sqrt(std_decomp_time / group.second.size());
            std_comp_speed = std::sqrt(std_comp_speed / group.second.size());
            std_decomp_speed = std::sqrt(std_decomp_speed / group.second.size());

            out << "Runs: " << group.second.size() << "\n";
            out << "Original Size:        " << group.second[0].original_size << " bytes\n";
            out << "Compressed Size:      " << group.second[0].compressed_size << " bytes\n";
            out << "Compression Ratio:    " << std::fixed << std::setprecision(2) << group.second[0].compression_ratio << "%\n";
            out << "\n=== Averages ===\n";
            out << "Compression Time:     " << avg_comp_time << " ± " << std_comp_time << " ms\n";
            out << "Compression Speed:    " << avg_comp_speed << " ± " << std_comp_speed << " MB/s\n";
            out << "Decompression Time:   " << avg_decomp_time << " ± " << std_decomp_time << " ms\n";
            out << "Decompression Speed:  " << avg_decomp_speed << " ± " << std_decomp_speed << " MB/s\n";
            out << "Peak Memory:          " << avg_peak_mem / (1024*1024) << " MB\n";
        }

        out << "\n";
    }
}

void generate_csv_report(const std::vector<TimingResult>& results, const fs::path& output_file) {
    std::ofstream out(output_file);
    
    out << "Tool,Preset,File,Format,Codecs,Original_Size,Compressed_Size,Ratio_%,";
    out << "Compression_Time_ms,Compression_Speed_MBps,Decompression_Time_ms,Decompression_Speed_MBps,Peak_Memory_MB\n";

    for (const auto& r : results) {
        out << r.tool << ",";
        out << r.preset_name << ",";
        out << fs::path(r.input_file).filename().string() << ",";
        out << r.format << ",";
        for (size_t i = 0; i < r.codec_combo.size(); ++i) {
            if (i > 0) out << "+";
            if (g_codec_map.count(r.codec_combo[i])) {
                out << g_codec_map[r.codec_combo[i]].name;
            }
        }
        out << ",";
        out << r.original_size << "," << r.compressed_size << ",";
        out << std::fixed << std::setprecision(2) << r.compression_ratio << ",";
        out << r.compression_time_ms << "," << r.compression_speed_mbps << ",";
        out << r.decompression_time_ms << "," << r.decompression_speed_mbps << ",";
        out << r.peak_memory_bytes / (1024*1024) << "\n";
    }
}

void generate_json_report(const std::vector<TimingResult>& results, const fs::path& output_file) {
    std::ofstream out(output_file);
    
    out << "{\n  \"results\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << "    {\n";
        out << "      \"tool\": \"" << r.tool << "\",\n";
        out << "      \"preset\": \"" << r.preset_name << "\",\n";
        out << "      \"file\": \"" << fs::path(r.input_file).filename().string() << "\",\n";
        out << "      \"format\": \"" << r.format << "\",\n";
        out << "      \"codecs\": [";
        for (size_t j = 0; j < r.codec_combo.size(); ++j) {
            if (j > 0) out << ", ";
            out << r.codec_combo[j];
        }
        out << "],\n";
        out << "      \"original_size\": " << r.original_size << ",\n";
        out << "      \"compressed_size\": " << r.compressed_size << ",\n";
        out << "      \"ratio_percent\": " << std::fixed << std::setprecision(2) << r.compression_ratio << ",\n";
        out << "      \"compression_time_ms\": " << r.compression_time_ms << ",\n";
        out << "      \"compression_speed_mbps\": " << r.compression_speed_mbps << ",\n";
        out << "      \"decompression_time_ms\": " << r.decompression_time_ms << ",\n";
        out << "      \"decompression_speed_mbps\": " << r.decompression_speed_mbps << ",\n";
        out << "      \"peak_memory_mb\": " << r.peak_memory_bytes / (1024*1024) << "\n";
        out << "    }";
        if (i < results.size() - 1) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
}

// ============================================================================
// Main
// ============================================================================

void print_usage() {
    std::cout << "Usage: benchmark_chd [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --config FILE          Config file path (default: benchmark.conf)\n";
    std::cout << "  --input PATH           Input directory (repeatable)\n";
    std::cout << "  --codecs LIST          Comma-separated presets: best,lzma,zlib,flac_huff,zstd\n";
    std::cout << "  --reps N               Number of repetitions\n";
    std::cout << "  --processors N         Processor count (0=auto, 1=single, N=specific)\n";
    std::cout << "  --output DIR           Output directory\n";
    std::cout << "  --verify               Enable integrity verification\n";
    std::cout << "  --formats FORMAT       Output formats (text,csv,json)\n";
    std::cout << "  --chdman-path PATH     Path to CHDman executable\n";
    std::cout << "  --chdlite-path PATH    Path to CHDlite executable\n";
    std::cout << "  --benchmark-chdman     Benchmark CHDman\n";
    std::cout << "  --benchmark-chdlite    Benchmark CHDlite\n";
    std::cout << "  --help                 Show this message\n";
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config;
    std::string config_file = "benchmark.conf";
    bool config_loaded = false;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            config.input_paths.push_back(argv[++i]);
        } else if (arg == "--codecs" && i + 1 < argc) {
            std::string codec_str = argv[++i];
            auto parts = split_string(codec_str, ',');
            for (auto& part : parts) {
                trim_string(part);
                if (!part.empty() && g_codec_presets.count(part)) {
                    config.codec_presets.push_back(part);
                }
            }
        } else if (arg == "--reps" && i + 1 < argc) {
            config.repetitions = std::stoi(argv[++i]);
        } else if (arg == "--processors" && i + 1 < argc) {
            config.num_processors = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            config.output_root = argv[++i];
        } else if (arg == "--verify") {
            config.verify_integrity = true;
        } else if (arg == "--formats" && i + 1 < argc) {
            config.output_formats = split_string(argv[++i], ',');
            for (auto& fmt : config.output_formats) trim_string(fmt);
        } else if (arg == "--chdman-path" && i + 1 < argc) {
            config.chdman_path = argv[++i];
        } else if (arg == "--chdlite-path" && i + 1 < argc) {
            config.chdlite_path = argv[++i];
        } else if (arg == "--benchmark-chdman") {
            config.benchmark_chdman = true;
        } else if (arg == "--benchmark-chdlite") {
            config.benchmark_chdlite = true;
        }
    }

    // Load config file if exists and not all parameters provided
    if (config.input_paths.empty() || config.codec_presets.empty()) {
        if (!load_config(config_file, config)) {
            std::cerr << "Error loading config file or no input paths provided\n";
            print_usage();
            return 1;
        }
        config_loaded = true;
    }

    if (config.input_paths.empty()) {
        std::cerr << "No input paths configured\n";
        return 1;
    }

    if (config.codec_presets.empty()) {
        std::cerr << "No codec presets configured\n";
        return 1;
    }

    // Validate that at least one benchmarking tool is enabled
    if (!config.benchmark_chdlite && !config.benchmark_chdman) {
        std::cerr << "No benchmarking tools enabled (set benchmark_chdlite or benchmark_chdman to true)\n";
        return 1;
    }

    // Discover input files
    std::cout << "\nDiscovering input files...\n";
    auto input_files = discover_input_files(config);
    
    if (input_files.empty()) {
        std::cerr << "No input files found\n";
        return 1;
    }

    std::cout << "Found " << input_files.size() << " input files:\n";
    for (const auto& f : input_files) {
        uint64_t size = fs::file_size(f);
        std::cout << "  - " << fs::path(f).filename().string() 
                  << " (" << size / (1024*1024) << " MB)\n";
    }

    // Display benchmark settings
    std::cout << "\nBenchmark Settings:\n";
    std::cout << "  Benchmarking Tools: ";
    if (config.benchmark_chdlite) std::cout << "CHDlite ";
    if (config.benchmark_chdman) std::cout << "CHDman ";
    std::cout << "\n";
    if (!config.chdlite_path.empty()) {
        std::cout << "  CHDlite Path: " << config.chdlite_path.string() << "\n";
    }
    if (!config.chdman_path.empty()) {
        std::cout << "  CHDman Path: " << config.chdman_path.string() << "\n";
    }
    std::cout << "  Repetitions: " << config.repetitions << "\n";
    std::cout << "  Processors: " << (config.num_processors == 0 ? "auto" : std::to_string(config.num_processors)) << "\n";
    std::cout << "  Verify Integrity: " << (config.verify_integrity ? "yes" : "no") << "\n";
    std::cout << "  Output Root: " << config.output_root.string() << "\n";
    std::cout << "  Output Formats: ";
    for (size_t i = 0; i < config.output_formats.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << config.output_formats[i];
    }
    std::cout << "\n";

    std::cout << "\nCodec Presets:\n";
    for (const auto& preset : config.codec_presets) {
        auto it = g_codec_presets.find(preset);
        if (it != g_codec_presets.end()) {
            std::cout << "  " << preset << "  (CD: ";
            for (size_t i = 0; i < it->second.cd_codecs.size(); ++i) {
                if (i > 0) std::cout << "+";
                if (g_codec_map.count(it->second.cd_codecs[i]))
                    std::cout << g_codec_map[it->second.cd_codecs[i]].name;
            }
            std::cout << " | DVD: ";
            for (size_t i = 0; i < it->second.dvd_codecs.size(); ++i) {
                if (i > 0) std::cout << "+";
                if (g_codec_map.count(it->second.dvd_codecs[i]))
                    std::cout << g_codec_map[it->second.dvd_codecs[i]].name;
            }
            std::cout << ")\n";
        }
    }

    // Prompt user
    std::cout << "\nReady to begin benchmarking. Continue? (y/n): ";
    char response;
    std::cin >> response;
    if (response != 'y' && response != 'Y') {
        std::cout << "Benchmark cancelled.\n";
        return 0;
    }

    // Run benchmarks
    std::vector<TimingResult> all_results;

    // Build list of tools to benchmark
    std::vector<std::string> tools;
    if (config.benchmark_chdlite) tools.push_back("chdlite");
    if (config.benchmark_chdman)  tools.push_back("chdman");

    // Pre-compute total run count for progress tracking
    int total_runs = 0;
    for (const auto& input_file : input_files) {
        std::string fmt = detect_format(input_file);
        for (const auto& preset : config.codec_presets) {
            if (!resolve_preset(preset, fmt).empty())
                total_runs += static_cast<int>(tools.size()) * config.repetitions;
        }
    }
    int completed_runs = 0;
    auto bench_start = high_resolution_clock::now();

    int file_idx = 0;
    for (const auto& input_file : input_files) {
        ++file_idx;
        std::string format = detect_format(input_file);
        std::string type_label = is_cd_format(format) ? "CD" : "DVD";
        uint64_t input_size = get_input_size(input_file);
        std::cout << "\n[File " << file_idx << "/" << input_files.size() << "] "
                  << fs::path(input_file).filename().string()
                  << " (" << type_label << ", " << input_size / (1024*1024) << " MB)\n";

        for (const auto& preset : config.codec_presets) {
            // Resolve preset to actual codec IDs based on file format
            auto codec_combo = resolve_preset(preset, format);
            if (codec_combo.empty()) {
                std::cerr << "  Skipping unknown preset: " << preset << "\n";
                continue;
            }

            // Build codec label for display
            std::string codec_label;
            for (size_t i = 0; i < codec_combo.size(); ++i) {
                if (i > 0) codec_label += "+";
                if (g_codec_map.count(codec_combo[i])) {
                    codec_label += g_codec_map[codec_combo[i]].name;
                }
            }

            for (const auto& tool : tools) {
                for (int rep = 1; rep <= config.repetitions; ++rep) {
                    ++completed_runs;
                    double pct = 100.0 * completed_runs / total_runs;
                    double elapsed_s = duration<double>(high_resolution_clock::now() - bench_start).count();
                    double eta_s = (completed_runs > 1 && pct < 100.0)
                        ? elapsed_s / (completed_runs - 1) * (total_runs - completed_runs + 1)
                        : 0.0;

                    // Format ETA as HH:MM:SS
                    char eta_buf[32] = "?";
                    if (completed_runs > 1) {
                        int eta_i = static_cast<int>(eta_s);
                        std::snprintf(eta_buf, sizeof(eta_buf), "%02d:%02d:%02d",
                            eta_i / 3600, (eta_i % 3600) / 60, eta_i % 60);
                    }

                    std::cout << "  [" << completed_runs << "/" << total_runs
                              << " " << std::fixed << std::setprecision(1) << pct << "%"
                              << " ETA:" << eta_buf << "]"
                              << " [" << tool << "] " << preset << " (" << codec_label << ")"
                              << " rep " << rep << "/" << config.repetitions << "... ";
                    std::cout.flush();

                    TimingResult result;
                    if (tool == "chdlite") {
                        result = run_benchmark_chdlite(input_file, codec_combo, config);
                    } else {
                        result = run_benchmark_chdman(input_file, codec_combo, config);
                    }
                    result.preset_name = preset;
                    all_results.push_back(result);

                    std::cout << std::fixed << std::setprecision(2);
                    std::cout << result.compression_ratio << "% ratio, ";
                    std::cout << result.compression_speed_mbps << " MB/s comp";
                    if (result.decompression_speed_mbps > 0) {
                        std::cout << ", " << result.decompression_speed_mbps << " MB/s decomp";
                    }
                    std::cout << "\n";
                }
            }
        }
    }

    // Generate output reports with timestamp to avoid overwriting
    fs::create_directories(config.output_root);

    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    char ts_buf[32];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%d_%H%M%S", std::localtime(&now_t));
    std::string ts(ts_buf);

    if (std::find(config.output_formats.begin(), config.output_formats.end(), "text") != config.output_formats.end()) {
        auto fname = "benchmark_" + ts + ".txt";
        generate_text_report(all_results, config.output_root / fname);
        std::cout << "Generated: " << fname << "\n";
    }

    if (std::find(config.output_formats.begin(), config.output_formats.end(), "csv") != config.output_formats.end()) {
        auto fname = "benchmark_" + ts + ".csv";
        generate_csv_report(all_results, config.output_root / fname);
        std::cout << "Generated: " << fname << "\n";
    }

    if (std::find(config.output_formats.begin(), config.output_formats.end(), "json") != config.output_formats.end()) {
        auto fname = "benchmark_" + ts + ".json";
        generate_json_report(all_results, config.output_root / fname);
        std::cout << "Generated: " << fname << "\n";
    }

    std::cout << "\nBenchmark complete!\n";
    return 0;
}
