# CHDlite Benchmark Tool Specification

## Overview
A comprehensive benchmarking tool for CHDlite and CHDman compression performance analysis. Tests compression/decompression across codec combinations with configurable parallelization, supporting both config file and CLI modes. Compare CHDlite vs CHDman baseline for performance benchmarking.

## Architecture

### Execution Model
- **Benchmark execution**: Sequential (one file → one codec combo → measure)
- **Tool selection**: Test CHDlite and/or CHDman based on config (benchmark_chdlite, benchmark_chdman flags)
- **Internal compression**: Uses executable's parallelization (configurable via `num_processors`)
- **Comparison scenarios**: Test with `num_processors=1` (single-threaded baseline) vs `num_processors=0` (auto/all cores) to measure speedup and compare tools

### Configuration Priority
1. CLI arguments (highest priority, overrides config)
2. Config file settings
3. Built-in defaults

## Config File Format (INI)

### [input]
```ini
paths = ${basedir}/test_roms; ${basedir}/samples
recursive = true
platform_skip = PS2,Dreamcast,PSP
auto_detect_format = true
```
- `paths`: Semicolon-separated input directories (supports `${basedir}` expansion)
- `recursive`: Boolean, enable recursive directory walk
- `platform_skip`: Comma-separated platform names to skip (PS2, Dreamcast, PSP, Saturn, PCEngine)
- `auto_detect_format`: Boolean, auto-detect input file format (CUE/GDI/ISO) from extension+header magic

### [tools]
```ini
chdman_path = /usr/local/bin/chdman
cdhplite_path = ${basedir}/Release/bin/cdhplite.exe
```
- `chdman_path`: Path to CHDman executable (leave empty to skip chdman benchmarks)
- `cdhplite_path`: Path to CHDlite executable (leave empty to use PATH or default)

### [codecs]
```ini
list = chdman_best_cd
cdhplite_default_cd
10,12
11
```
- List of codec combinations: preset names or custom codec lines (one per line)
- **Codec ID Mapping** (organized by type):
  - **Generic Codecs** (DVD/ISO/Raw):
    - 1 = Zlib
    - 2 = ZlibPlus (legacy)
    - 3 = Zstd
    - 4 = LZMA
    - 5 = FLAC
    - 6 = Huffman
  - **CD/GD-ROM Compound Codecs** (data + subcode):
    - 9 = CD_Zlib
    - 10 = CD_Zstd
    - 11 = CD_LZMA
    - 12 = CD_FLAC
- **Preset Combinations**:
  - `chdman_best_cd` = [11,10,9,12] = cdlz+cdzs+cdzl+cdfl (all CD compound codecs)
  - `chdman_best_dvd` = [3,4,1,6] = zstd+lzma+zlib+huff (all DVD generic codecs)
  - `chdlite_default_cd` = [10,12] = CHDlite default for CD (cdzs+cdfl, fast)
  - `chdlite_default_dvd` = [3] = CHDlite default for DVD (zstd, balanced)
  - `individual_cd` = [11],[10],[9],[12] = Test each CD codec separately
  - `individual_dvd` = [1],[3],[4],[6] = Test each DVD codec separately

### [benchmark]
```ini
repetitions = 1
num_processors = 0
verify_integrity = true
output_root = ${basedir}/benchmark_results
keep_last_output = false
```
- `repetitions`: Number of runs per file/codec combo (int)
- `num_processors`: 0=auto (all cores), 1=single-threaded, N=specific core count
- `verify_integrity`: Boolean, verify decompressed data matches original via SHA1
- `output_root`: Output directory for results
- `keep_last_output`: Boolean, keep previous results or overwrite

### [benchmark_selection]
```ini
benchmark_cdhplite = true
benchmark_chdman = false
```
- `benchmark_cdhplite`: Boolean, enable CHDlite benchmarking
- `benchmark_chdman`: Boolean, enable CHDman benchmarking (requires chdman_path)

### [output]
```ini
output_formats = text,csv,json
output_log = true
```
- `output_formats`: Comma-separated formats (text, csv, json)
- `output_log`: Boolean, enable detailed logging

## CLI Mode

```bash
# Config file mode (default)
benchmark_chd benchmark.conf

# CLI override mode (overrides config file)
benchmark_chd --config benchmark.conf --input /path/to/roms --codecs cdhplite_default_cd --reps 3 --processors 1

# CLI-only (no config file needed)
benchmark_chd --input /path --codecs 10,12 --reps 5 --output ./results

# Compare CHDlite vs CHDman baseline
benchmark_chd --cdhplite-path /usr/bin/cdhplite --chdman-path /usr/bin/chdman --benchmark-cdhplite --benchmark-chdman --codecs chdman_best_cd --reps 3

# Single-threaded baseline
benchmark_chd --input /path --codecs 10,12 --reps 5 --processors 1

# Help
benchmark_chd --help
```

### CLI Arguments
- `--config FILE`: Config file path (default: benchmark.conf)
- `--input PATH`: Input directory (can repeat)
- `--codecs LIST`: Codec presets or combos: `chdman_best_cd` or `10,12 1 3` (space=new combo)
- `--reps N`: Number of repetitions
- `--processors N`: Number of processors (0=auto, 1=single-threaded, N=specific)
- `--output DIR`: Output directory
- `--verify`: Enable integrity verification
- `--formats FORMAT`: Output formats (text, csv, json)
- `--chdman-path PATH`: Path to CHDman executable
- `--cdhplite-path PATH`: Path to CHDlite executable
- `--benchmark-cdhplite`: Benchmark CHDlite
- `--benchmark-chdman`: Benchmark CHDman
- `--help`: Show help message

## Startup Prompt

When benchmarking begins, display:
1. **Found files**: List with filename, format (CUE/GDI/ISO), size (MB), detected platform
2. **Benchmark settings**:
   - Codec combinations (decoded names)
   - Repetitions
   - Processors setting
   - Output formats
   - Verification enabled/disabled
3. **User confirmation**: `Start benchmarking? (y/n):`

## Logging Output

### Per-Run Data (each repetition)
- File: filename
- Format: CUE/GDI/ISO/CHD
- Original Size: bytes
- Codecs: [codec names]
- Compression Time: ms
- Compression Speed: MB/s
- Compressed Size: bytes
- Compression Ratio: %
- Decompression Time: ms
- Decompression Speed: MB/s
- Peak Memory: MB

### Aggregated Data (if repetitions > 1)
For each file + codec combo:
- **Individual runs**: All N repetition results
- **Averages section**: 
  - Mean compression time ± stddev
  - Mean compression speed ± stddev
  - Mean ratio ± stddev
  - Mean decompression time ± stddev
  - Mean decompression speed ± stddev
  - Mean peak memory ± stddev

## Output Formats

### Text (human-readable)
```
CHDlite Benchmark Report
========================

File: rom1.cue
Codecs: Zlib
-----------------------------------------
Original Size:        12345678 bytes
Compressed Size:      5432100 bytes
Compression Ratio:    44.05%
Compression Time:     1234.56 ms
Compression Speed:    10.23 MB/s
Decompression Time:   567.89 ms
Decompression Speed:  21.74 MB/s
Peak Memory:          256000000 bytes

[If multiple reps]
Runs: 3
...individual results...

=== Averages ===
Compression Time:     1200.00 ± 50.00 ms
Compression Speed:    10.50 ± 0.30 MB/s
...
```

### CSV (machine-readable)
```csv
File,Codecs,Original_Size,Compressed_Size,Ratio_%,Compression_Time_ms,Compression_Speed_MBps,Decompression_Time_ms,Decompression_Speed_MBps,Peak_Memory_bytes,Run
rom1.cue,Zlib,12345678,5432100,44.05,1234.56,10.23,567.89,21.74,256000000,1
rom1.cue,Zlib,12345678,5432100,44.05,1220.45,10.35,555.67,22.10,255500000,2
rom1.cue,Zlib,12345678,5432100,44.05,1198.32,10.55,580.01,21.38,256500000,3
```

### JSON (structured)
```json
{
  "metadata": {
    "timestamp": "2026-04-18T10:30:45Z",
    "total_files": 6,
    "total_codec_combos": 4,
    "total_reps": 3
  },
  "results": [
    {
      "file": "rom1.cue",
      "format": "CUE",
      "original_size": 12345678,
      "codecs": [1],
      "codec_names": ["Zlib"],
      "runs": [
        {
          "run": 1,
          "compression_time_ms": 1234.56,
          "compression_speed_mbps": 10.23,
          "compressed_size": 5432100,
          "compression_ratio": 44.05,
          "decompression_time_ms": 567.89,
          "decompression_speed_mbps": 21.74,
          "peak_memory_bytes": 256000000
        }
      ],
      "averages": {
        "compression_time_ms": 1200.00,
        "compression_time_stddev": 50.00,
        "compression_speed_mbps": 10.50,
        "compression_speed_stddev": 0.30,
        ...
      }
    }
  ]
}
```

## Implementation Details

### File Discovery
- Recursive directory walk (if enabled)
- File extensions: `.cue`, `.gdi`, `.iso`, `.chd`
- Auto-format detection: file extension + header magic bytes
- Platform detection: (optional) scan for platform-specific metadata

### Memory Tracking
- **Windows**: `GetProcessMemoryInfo()` → `PeakWorkingSetSize`
- **Linux**: Read `/proc/self/status` → `VmPeak` (KB → bytes)

### Codec Combination API
- ArchiveOptions::compression[4] = {Codec, Codec, Codec, Codec}
- Up to 4 codecs per hunk (best ratio wins during compression)
- Empty slots = `Codec::None`

### Timing Precision
- Use `std::chrono::high_resolution_clock`
- Results in milliseconds (convert to seconds as needed)
- Warmup call to get_peak_memory() before timing (improves cache warmth)

### Tool Invocation
- **CHDlite**: Use chdlite_api.hpp (ChdArchiver::archive(), ChdReader)
- **CHDman**: Spawn subprocess with appropriate arguments, capture timing from execution
- **Codec mapping**: Convert preset names to ID lists in benchmark code

### Verification
- If enabled: Read all hunks from decompressed CHD
- Compute SHA1 of decompressed data
- Compare with original file SHA1
- Report verification result in output (pass/fail flag)

## Error Handling
- Skip corrupted/unreadable input files (log warning)
- Cleanup incomplete CHD files on failure
- Report skipped platforms in final summary
- Validate codec IDs (warn on invalid)
- Validate config file syntax (abort on parse error)
- Check chdman_path exists before attempting benchmark_chdman
- Log tool invocation failures (CHDman subprocess errors)

## Performance Considerations
- Sequential file processing (no inter-file parallelism)
- intra-file parallelism via `num_processors` parameter
- Memory tracking adds minimal overhead
- Verification can be disabled for speed

## Example Workflows

### Scenario 1: Compare Single vs Parallel (LZMA)
```bash
# Single-threaded
benchmark_chd --input roms/ --codecs 4 --reps 5 --processors 1 --output results_single

# Multi-threaded (auto cores)
benchmark_chd --input roms/ --codecs 4 --reps 5 --processors 0 --output results_parallel
```

### Scenario 2: Evaluate Codec Mix
```bash
# Test Zlib vs LZMA vs combined
benchmark_chd --input roms/ --codecs "1" "4" "1,4" --reps 3 --output results_codecs
```

### Scenario 3: Platform-Specific Benchmark
```ini
[input]
paths = ${basedir}/roms
platform_skip = PS2,Dreamcast
[codecs]
list = 1
3
4
```

## Testing & Validation
1. Build: `./build.ps1` or `cmake --build build`
2. Run: `./Release/bin/benchmark_chd benchmark.conf`
3. Verify output files: `benchmark_results/benchmark_report.{txt,csv,json}`
4. Check log for errors/warnings
5. Compare results across processor counts
