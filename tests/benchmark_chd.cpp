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
    std::vector<std::vector<int>> codec_combinations;
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
    std::string input_file;
    std::string format;
    std::vector<int> codec_combo;
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

// Codec presets — match CHDlite/CHDman actual codec arrays
std::map<std::string, std::vector<std::vector<int>>> g_codec_presets = {
    // --best for CD: cdlz, cdzs, cdzl, cdfl (all 4 CD compound codecs)
    {"chdman_best_cd", {{11, 10, 9, 12}}},
    // --best for DVD: zstd, lzma, zlib, huff (all 4 generic codecs)
    {"chdman_best_dvd", {{3, 4, 1, 6}}},
    // CHDlite smart default for CD: cdzs + cdfl (Zstd fast + FLAC audio)
    {"chdlite_default_cd", {{10, 12}}},
    // CHDlite smart default for DVD: zstd (fast balanced)
    {"chdlite_default_dvd", {{3}}},
    // Test each CD codec individually
    {"individual_cd", {{11}, {10}, {9}, {12}}},
    // Test each DVD codec individually
    {"individual_dvd", {{1}, {3}, {4}, {6}}}
};

// ============================================================================
// Utility Functions
// ============================================================================

// Forward declarations
void trim_string(std::string& str);
std::vector<std::string> split_string(const std::string& str, char delim);

std::vector<std::vector<int>> expand_codec_preset(const std::string& input) {
    std::string trimmed = input;
    trim_string(trimmed);
    
    // Check if it's a preset name
    if (g_codec_presets.count(trimmed)) {
        return g_codec_presets[trimmed];
    }
    
    // Otherwise, parse as custom codec list
    std::vector<std::vector<int>> result;
    auto parts = split_string(trimmed, ',');
    std::vector<int> combo;
    for (auto& part : parts) {
        trim_string(part);
        try {
            combo.push_back(std::stoi(part));
        } catch (...) {}
    }
    if (!combo.empty()) {
        result.push_back(combo);
    }
    return result;
}

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
    // Only self-contained formats; .cue/.gdi/.toc are TOC files that need raw track data
    std::vector<std::string> extensions = {".chd", ".iso"};

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

        while (std::getline(file, line)) {
            trim_string(line);
            
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            if (line[0] == '[' && line[line.length() - 1] == ']') {
                current_section = line.substr(1, line.length() - 2);
                trim_string(current_section);
                continue;
            }

            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);
                trim_string(key);
                trim_string(value);
                
                m_data[current_section][key] = value;
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

    // Parse codec combinations (now supporting presets)
    std::string codec_str = ini.get("codecs", "list", "");
    if (!codec_str.empty()) {
        auto lines = split_string(codec_str, '\n');
        for (auto& line : lines) {
            trim_string(line);
            if (!line.empty()) {
                auto expanded = expand_codec_preset(line);
                for (auto& combo : expanded) {
                    config.codec_combinations.push_back(combo);
                }
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

TimingResult run_benchmark(const std::string& input_file, 
                          const std::vector<int>& codec_combo,
                          const BenchmarkConfig& config) {
    TimingResult result;
    result.input_file = input_file;
    result.codec_combo = codec_combo;
    result.format = detect_format(input_file);

    fs::path temp_output = config.output_root / "temp.chd";
    fs::create_directories(config.output_root);

    ChdArchiver archiver;
    ArchiveOptions opts;

    // Set processor count
    opts.num_processors = config.num_processors;

    // Convert codec combination to compression array
    for (size_t i = 0; i < codec_combo.size() && i < 4; ++i) {
        int codec_id = codec_combo[i];
        if (g_codec_map.count(codec_id)) {
            opts.compression[i] = g_codec_map[codec_id].codec;
        }
    }

    try {
        result.original_size = fs::file_size(input_file);

        get_peak_memory(); // Warmup

        auto comp_start = high_resolution_clock::now();
        ArchiveResult comp_result = archiver.archive(input_file, temp_output.string(), opts);
        auto comp_end = high_resolution_clock::now();

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

        // Decompression phase
        if (config.verify_integrity) {
            ChdReader reader;
            try {
                reader.open(temp_output.string());
                auto header = reader.read_header();

                auto decomp_start = high_resolution_clock::now();

                for (uint32_t i = 0; i < header.hunk_count; ++i) {
                    auto hunk = reader.read_hunk(i);
                    if (hunk.empty()) {
                        std::cerr << "Decompression verification failed at hunk " << i << std::endl;
                        reader.close();
                        if (fs::exists(temp_output)) fs::remove(temp_output);
                        return result;
                    }
                }

                auto decomp_end = high_resolution_clock::now();
                reader.close();

                result.decompression_time_ms = duration<double, std::milli>(decomp_end - decomp_start).count();
                result.decompression_speed_mbps = (result.compressed_size / (1024.0 * 1024.0)) / (result.decompression_time_ms / 1000.0);
            } catch (const std::exception& e) {
                std::cerr << "Error during decompression verification: " << e.what() << std::endl;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Benchmark exception: " << e.what() << std::endl;
    }

    if (fs::exists(temp_output)) {
        fs::remove(temp_output);
    }

    return result;
}

// ============================================================================
// Output Generation
// ============================================================================

void generate_text_report(const std::vector<TimingResult>& results, const fs::path& output_file) {
    std::ofstream out(output_file);
    
    out << "CHDlite Benchmark Report\n";
    out << "========================\n\n";

    std::map<std::pair<std::string, std::vector<int>>, std::vector<TimingResult>> grouped;
    for (const auto& r : results) {
        grouped[{r.input_file, r.codec_combo}].push_back(r);
    }

    for (const auto& group : grouped) {
        out << "File: " << fs::path(group.first.first).filename().string() << "\n";
        out << "Format: " << group.second[0].format << "\n";
        out << "Codecs: ";
        for (size_t i = 0; i < group.first.second.size(); ++i) {
            if (i > 0) out << " + ";
            if (g_codec_map.count(group.first.second[i])) {
                out << g_codec_map[group.first.second[i]].name;
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
    
    out << "File,Format,Codecs,Original_Size,Compressed_Size,Ratio_%,";
    out << "Compression_Time_ms,Compression_Speed_MBps,Decompression_Time_ms,Decompression_Speed_MBps,Peak_Memory_MB\n";

    for (const auto& r : results) {
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
    std::cout << "  --codecs LIST          Codec preset/combo: chdlite_default_cd or 10,12\n";
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
            auto parts = split_string(codec_str, ' ');
            for (auto& part : parts) {
                trim_string(part);
                if (!part.empty()) {
                    auto expanded = expand_codec_preset(part);
                    for (auto& combo : expanded) {
                        config.codec_combinations.push_back(combo);
                    }
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
    if (config.input_paths.empty() || config.codec_combinations.empty()) {
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

    if (config.codec_combinations.empty()) {
        std::cerr << "No codec combinations configured\n";
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

    std::cout << "\nCodec Combinations:\n";
    for (const auto& combo : config.codec_combinations) {
        std::cout << "  ";
        for (size_t i = 0; i < combo.size(); ++i) {
            if (i > 0) std::cout << " + ";
            if (g_codec_map.count(combo[i])) {
                std::cout << g_codec_map[combo[i]].name;
            }
        }
        std::cout << "\n";
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

    for (const auto& input_file : input_files) {
        std::cout << "\nProcessing: " << fs::path(input_file).filename().string() << "\n";

        for (const auto& codec_combo : config.codec_combinations) {
            for (int rep = 1; rep <= config.repetitions; ++rep) {
                std::cout << "  Run " << rep << "/" << config.repetitions << "... ";
                std::cout.flush();

                auto result = run_benchmark(input_file, codec_combo, config);
                all_results.push_back(result);

                std::cout << std::fixed << std::setprecision(2);
                std::cout << result.compression_ratio << "% ratio, ";
                std::cout << result.compression_speed_mbps << " MB/s\n";
            }
        }
    }

    // Generate output reports
    fs::create_directories(config.output_root);

    if (std::find(config.output_formats.begin(), config.output_formats.end(), "text") != config.output_formats.end()) {
        generate_text_report(all_results, config.output_root / "benchmark_report.txt");
        std::cout << "Generated: benchmark_report.txt\n";
    }

    if (std::find(config.output_formats.begin(), config.output_formats.end(), "csv") != config.output_formats.end()) {
        generate_csv_report(all_results, config.output_root / "benchmark_report.csv");
        std::cout << "Generated: benchmark_report.csv\n";
    }

    if (std::find(config.output_formats.begin(), config.output_formats.end(), "json") != config.output_formats.end()) {
        generate_json_report(all_results, config.output_root / "benchmark_report.json");
        std::cout << "Generated: benchmark_report.json\n";
    }

    std::cout << "\nBenchmark complete!\n";
    return 0;
}
