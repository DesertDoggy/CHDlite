# CHDlite Updates & Release Notes
## v0.2.0
### New Binaries
- chdcomp binary with automatic `--best` compression (CD: `cdzs,cdlz,cdzl,cdfl` / DVD: `zstd,lzma,zlib`) now compresses to chdman default + zstd to get best compression
- benchmark binary for benchmarking chdman and chdlite

### Speed Optimization
- zlib-ng integration
- xxHash AVX2 auto-dispatch
- LZMA encoder persistent instance
- FLAC 3→2 encode elimination
- CRC16 slice-by-16
- Sequential read hints (cross-platform)
- SHA1 SIMD dispatch
- CMakeLists.txt SIMD compile flags (-mavx2 -msse4.2 -mssse3)
- Per-file pipeline deferred hash computation
- N_SLOTS=3 triple-buffer pipeline
- Audio byte-swap auto-vectorization
- Remove 4-core cap on thread pool
- Multi-file batch thread budget
- Proper CUE sheet parsing for all disc formats
- Tests: 106 PASS / 1 FAIL
- Thread Distribution Optimized on multicodec trys. (Fixed issue where multicodec was slower than chdman. One codec per thread, distribution by codec complexity)

### Logging and Error Handling
- Dual-log system: Structured `error.log` (pipe-delimited) + command-specific pretty logs (`chdread.log`, `chdhash.log`, etc.)
- `--result` flag: Pretty log on/off control
- `-log <level>` control: Verbosity levels (debug, info, warning, error, critical, none)
- N/A logging for empty metadata fields


### Others
- Extend read to support non-CHD files (ISO images, CD images, etc.)
- Terminology change: "system" → "platform" (detect_system → detect_game_platform)
- `--best` compression preset for create command
- `-c chdman` compression preset for create command
- Default `-o` flags for auto-generated output paths
- `verify`, `copy`, `dumpmeta` commands with full option support

---
## v0.2.1

### New binaries
- Added D&D GUI App for Mac

### Features
- Added function to take dir for -o and create output files with same name as input file.
- Default auto codec selection (smart default) now displays media type, platform and chosen codec. on compression.

### Bug Fixes
- Restructured Toplevel chdlite command to api for gui/cli compatibility.
- Added --version args.
- Fixed version display to 0.2.1 (Was 0.1.0 in 0.2.0)
- Fixed read not reading platform for ps1,ps2,pce chd.
- Fixed read not checking magic sector for pce even on cue read.
- Fixed -c chdman not passing codecs.
  delete chdman from -c --compression options, and only take actual codecs.
- Add -chdman, --chdman option for original codec compression.
- Add -best in addition to --best.