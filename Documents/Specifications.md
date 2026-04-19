# CHDlite DLL Specifications

## TL;DR

Build a **single cross-platform DLL** (C++17 + C wrapper) that reads CHD files, extracts to disc formats (auto-detecting GD/GDI, CUE/BIN, DVD/ISO), and archives files/images to CHD. Use MAME's battle-tested CHD codec implementation (embedded) with smart defaults for ROM users: auto-format detection, PS2→ZLIB, PSP→ZSTD, other ISO→ZLIB. Three executables: `chdlite` (full CLI), `read` (drag-drop quick-hash), DLL (library). Hash mode: always compute from actual content (not header), header SHA-1 only with `--fast-hash` flag. No auto-hashing except in `read` utility.

---

## Project Structure

```
CHDlite/
├── CMakeLists.txt                      # Root CMake config
├── LICENSE                             # GPLv3
├── README.md                           # Project overview + build instructions
├── .gitignore                          # Standard C++/.build artifacts
│
├── src/
│   ├── CMakeLists.txt                  # Source build config
│   ├── mame/                           # Embedded MAME CHD code
│   │   ├── util/                       # Direct extract from MAME src/lib/util/
│   │   │   ├── chd.h, chd.cpp          # Core CHD format v1-v5
│   │   │   ├── chdcodec.h, chdcodec.cpp # All codecs (zlib, zstd, LZMA, FLAC, etc)
│   │   │   ├── cdrom.h, cdrom.cpp      # CD format parsing (GDI, CUE, NRG, ISO)
│   │   │   ├── hashing.h, hashing.cpp  # SHA-1, MD5, CRC16 (self-contained)
│   │   │   ├── md5.h                   # MD5 implementation (public domain)
│   │   │   ├── corefile.h, corefile.cpp # Buffered file I/O
│   │   │   ├── ioprocs.h, ioprocsimpl.h # I/O abstractions (random_read/write)
│   │   │   ├── coretmpl.h              # Template utilities
│   │   │   ├── corestr.h, corestr.cpp  # String utilities
│   │   │   ├── multibyte.h             # Big-endian read/write helpers
│   │   │   ├── path.h                  # Path utilities
│   │   │   ├── strformat.h, strformat.cpp # Type-safe printf
│   │   │   ├── utilfwd.h               # Forward declarations
│   │   │   ├── vecstream.h             # Vector stream
│   │   │   ├── endianness.h            # Endian definitions
│   │   │   ├── avhuff.h, avhuff.cpp    # A/V Huffman codec (LaserDisc)
│   │   │   ├── huffman.h, huffman.cpp   # Huffman codec
│   │   │   ├── bitstream.h             # Bit-level I/O
│   │   │   └── flac.h, flac.cpp        # FLAC audio wrapper
│   │   └── osd-adapter/                # Compatibility layer replacing MAME's src/osd/
│   │       ├── osdcomm.h               # OS detection, type defs, endian macros
│   │       ├── osdcore.h               # Logging (osd_printf_*), timing (osd_ticks)
│   │       ├── eminline.h              # Platform macros (portable C++ fallbacks)
│   │       ├── osdfile.h, osdfile.cpp  # File I/O (POSIX + Win32 #ifdef)
│   │       └── osdsync.h, osdsync.cpp  # Work queue (C++17 thread pool)
│   │
│   ├── platform/                       # Cross-platform utilities
│   │   ├── platform.h                  # Logging, error handling, platform abstraction
│   │   ├── logger.h, logger.cpp        # Structured logging (debug/info/warn/error)
│   │   ├── error_codes.h               # Error definitions + mapping
│   │   ├── codec_defaults.h            # Built-in codec default map
│   │   ├── config_parser.h, config_parser.cpp # INI file parser
│   │   └── thread_pool.h               # Simple C++17 thread pool (for work queue)
│   │
│   ├── chd-api/                        # CHDlite public API
│   │   ├── chd_api.hpp                 # C++ public API (main header)
│   │   ├── chd_api.cpp                 # Implementation wrappers around MAME
│   │   ├── chd_reader.hpp              # Read/hash operations
│   │   ├── chd_extractor.hpp           # Extract with smart defaults
│   │   ├── chd_archiver.hpp            # Archive files/images to CHD
│   │   ├── chd_types.hpp               # Enums, structs (ChdContentType, CompressionType, etc)
│   │   └── chd_options.hpp             # Configuration/options (matching chdman)
│   │
│   ├── c-wrapper/                      # C API layer (optional, optional feature)
│   │   ├── chd_c_api.h                 # C binding (opaque pointers, char*  arrays)
│   │   └── chd_c_api.cpp               # C implementation
│   │
│   └── cli/                            # CLI binaries + shared code
│       ├── CMakeLists.txt              # CLI build config (links DLL)
│       ├── chdlite/
│       │   ├── chdlite.cpp             # Main 'chdlite' CLI entry point
│       │   ├── commands/
│       │   │   ├── read_cmd.cpp        # 'chdlite read' info command
│       │   │   ├── extract_cmd.cpp     # 'chdlite extract' command
│       │   │   ├── archive_cmd.cpp     # 'chdlite archive' command
│       │   │   └── hash_cmd.cpp        # 'chdlite hash' command
│       │   └── arg_parser.h            # CLI argument parsing
│       │
│       └── read-util/
│           ├── read.cpp                # 'read' utility: drag-drop auto-hash (Windows/Mac)
│           └── read_utils.h            # Auto-hash, format detection
│
├── include/
│   └── chdlite/                        # Public headers for DLL consumers
│       ├── chd_api.hpp                 # Re-export main C++ API
│       └── chd_c_api.h                 # Re-export C API
│
├── tests/
│   ├── CMakeLists.txt                  # Test build config
│   ├── test_chd_reader.cpp             # Unit tests: read operations, hash validation
│   ├── test_chd_extract.cpp            # Unit tests: format auto-detection, extraction
│   ├── test_chd_archive.cpp            # Unit tests: archive creation, codec selection
│   └── fixtures/                       # Sample CHD files for testing (add as submodule?)
│
├── build/                              # (created during build)
│   ├── CMakeCache.txt
│   ├── Release/                        # Windows: CHDlite.dll, chdlite.exe, read.exe
│   └── ...                             # macOS: libCHDlite.dylib, chdlite, read
│                                       # Linux: libCHDlite.so, chdlite, read
│
└── scripts/
    ├── extract_mame_chd.sh             # Helper: copies MAME files from References/
    ├── platform_detect.cmake           # CMake platform detection
    └── build.sh                        # One-liner build script (win/mac/linux)
```

---

## Implementation Phases

### Phase 1: Foundation & Setup

**Goal:** Establish build system, adapter layer, project structure.

1. **CMake root config** (`CMakeLists.txt`)
   - Set C++17 standard minimum
   - Define platform detection (WIN32, APPLE, UNIX)
   - Find external deps (zlib, zstd, lzma, FLAC) via vcpkg or system
   - Configure DLL/dylib/so output paths
   - Add subdirectories: src/, tests/, scripts/

2. **Extract MAME CHD files** into `src/mame/util/`
   - Script to copy from `References/mame_ref/src/lib/util/` (chd.*, chdcodec.*, cdrom.*, hashing.*, etc.)
   - Extract OSD files into `src/mame/osd-adapter/` (eminline.h, etc.)

3. **Create OSD adapter layer** (`src/mame/osd-adapter/`)
   - **osdfile.h/cpp** — Wraps POSIX `open()`, `read()`, `seek()` and Win32 `CreateFile()`. Provides MAME's `osd_file` interface via std::error_condition.
   - **eminline.h** — Replace MAME's platform-specific macros with portable versions or stubs (inline assembly optional).
   - **osd_work_queue.h** — Simple C++17 thread pool using `std::thread` + `std::queue` (replaces MAME's worker queue).
   - Implement mapping: `osd_printf_error()` → platform logger

4. **Platform layer** (`src/platform/`)
   - **logger.h/cpp** — Structured logging with level support (DEBUG/INFO/WARN/ERROR).
   - **platform.h** — Detect OS, expose `get_temp_dir()`, `path_join()`, etc.
   - **error_codes.h** — Central error definitions + MAME error → CHDlite error mapping.
   - **codec_defaults.h** — Built-in codec default map (PS2→ZLIB, PSP→ZSTD, others provisional)
   - **config_parser.h/cpp** — INI file parser for `~/.chdlite/config.ini` and `./chdlite.conf`
     - Load-order: CLI flag > user config > project config > built-in defaults
     - Supports overriding codec selections per CD system
     - No external dependencies (simple string parsing)

5. **CMake for src/** — Builds archive target `CHDlite` (DLL/dylib/so)
   - Compile MAME files + adapter layer + platform layer
   - Link external libs (zlib, zstd, lzma, FLAC)
   - Generate DLL with version info (platform-specific)
   - Export C++ symbols (no C++ name mangling on Windows via .def)

**Verification:**
- CMake configures and builds successfully on macOS
- DLL builds without linker errors
- OSD adapter correctly redirects to std::filesystem / POSIX

---

### Phase 2: C++ Public API

**Goal:** Expose MAME CHD functionality as modern C++ API.

1. **Define public API** (`src/chd-api/chd_types.hpp`)
   - Enums: `ChdContentType` (CD, DVD, ISO, GD-ROM, HDD), `CompressionType` (ZLIB, ZSTD, LZMA, FLAC, HUFFMAN)
   - Structs: `ChdHeader`, `ChdTrack`, `HashResult` (data + algorithm type)
   - Options struct: `ChdExtractOptions`, `ChdArchiveOptions` (matching chdman flags)

2. **Reader API** (`src/chd-api/chd_reader.hpp`)
   - `class ChdReader`
     - `open(path, mode)` → open CHD file
     - `read_header()` → struct ChdHeader with metadata (includes embedded hashes)
     - `read_bytes(offset, length) → std::vector<uint8_t>` 
     - `read_hunk(hunk_num) → std::vector<uint8_t>`
     - `get_tracks() → std::vector<ChdTrack>` (for CD/DVD)
     - **Hash operations (always compute from actual content):**
       - `hash_content(algorithm) → HashResult` — compute hash from actual track data
       - `get_embedded_sha1() → std::string` — fast SHA-1 from CHD header (no recalculation)
       - `get_embedded_md5() → std::string` — fast MD5 from CHD header (no recalculation)
     - Multi-track CD/DVD: hash operation computes hash across all track data sequentially
     - **Never forces double-read:** hash computed during same I/O pass as data reads

3. **Extractor API** (`src/chd-api/chd_extractor.hpp`)
   - `class ChdExtractor`
     - `extract(chd_file, output_dir, options) → ExtractionResult`
     - **Smart defaults:**
       - If format not specified: auto-detect from CHD metadata + filename
       - If CD-ROM → extract as GDI or CUE/BIN (detect GD-ROM vs. standard CD)
       - If DVD/ISO → extract as ISO
     - Progress callback support
     - Support all chdman extract options (e.g., `--hunkbytes`, `--compression`)

4. **Archiver API** (`src/chd-api/chd_archiver.hpp`)
   - `class ChdArchiver`
     - `archive(source_file_or_dir, output_chd, options) → ArchiveResult`
     - **Format detection:**
       - If source = `.iso`, `.gdi`, `.cue`, `.bin`, `.nrg` → auto-detect as disc image
       - If source = directory or other file → treat as raw file/directory archive
     - **Compression defaults by CD system detected (see codec defaults table below)**
     - Multi-file detection: auto-merge .bin/.cue/.gdi referenced files by format parser
     - User can override codec selection via `--codec` flag
     - Config-aware codec selection: load from config file with proper precedence
     - Support all chdman archive options

5. **Central API** (`src/chd-api/chd_api.hpp`)
   - Aggregate header importing all the above
   - Exception class: `ChdException`
   - Convenience functions: `is_chd_file(path)`, `detect_format(path)`
   - Version info: `CHDlite::get_version()`, `CHDlite::get_mame_version()`

6. **Implementation** (`src/chd-api/chd_api.cpp`)
   - Wrap MAME `chd_file`, `cdrom_file` classes
   - Call MAME's extraction/compression/hashing directly
   - Implement format detection via `cdrom.cpp::is_gdicue()`, file extension checks, metadata inspection
   - Perform CD system detection: PS2 (boot 0x2E 0x58), PSP (UMD magic), PS1, PC Engine, Mega CD, Saturn, Neo Geo CD, 3DO (check boot sectors)
   - Codec selection logic:
     1. CLI `--codec` flag (if specified)
     2. Load user config `~/.chdlite/config.ini` (parse [codecs] section)
     3. Load project config `./chdlite.conf` (if exists in working dir)
     4. Fall back to `codec_defaults.h` built-in map
   - Generate archive `.log` file with detected system, codec choice reason, compression results

**Verification:**
- Can open CHD file and read header
- Can hash in-memory without extraction
- Extract auto-detects format (GDI CUE vs. CUE/BIN)
- Archive creates valid CHD with correct codec

---

### Phase 3: C Wrapper API

**Goal:** Enable calling DLL from C/C#/.NET/Python.

1. **C API** (`src/c-wrapper/chd_c_api.h`)
   - Opaque pointers: `chd_reader_t`, `chd_extractor_t`, `chd_archiver_t`
   - Flat C functions:
     - `chd_reader_create()`, `chd_reader_open()`, `chd_reader_read_bytes()`, `chd_reader_hash_all()`, `chd_reader_destroy()`
     - `chd_extractor_create()`, `chd_extractor_extract()`, `chd_extractor_destroy()`
     - `chd_archiver_create()`, `chd_archiver_archive()`, `chd_archiver_destroy()`
     - `chd_get_error_string()` — convert error codes to readable text
   - Struct mirror C++ types: `chd_header_t`, `chd_track_t`, `hash_result_t`

2. **C Implementation** (`src/c-wrapper/chd_c_api.cpp`)
   - Wrap C++ API objects in opaque structs
   - Handle exceptions → error codes
   - String marshaling (std::string ↔️ char*)
   - Vector marshaling (std::vector ↔️ arrays + length)

3. **CMake** — Export C API
   - Single DLL: both C++ and C symbols

**Verification:**
- C code can call DLL and read CHD file
- Error handling works correctly
- P/Invoke can load DLL from C#

---

### Phase 4: CLI Tools

**Goal:** Two executables with different purposes.

#### 4a. `chdlite` — Full-featured CLI

1. **Commands** (`src/cli/chdlite/commands/`)
   - **read** — List CHD info, metadata, tracks (`chdlite read file.chd`)
   - **extract** — Extract CHD to disc format; auto-detects output format (`chdlite extract input.chd output/`)
   - **archive** — Create CHD from image/file/directory (`chdlite archive input.iso output.chd`)
   - **hash** — Hash CHD/files with algorithm selection (`chdlite hash file.chd [--algo sha1,md5 --output sfv]`)
   - **help** — Usage documentation

2. **Hash behavior (chdlite hash):**
   - Default: SHA-1 only, computed from actual content (tracks)
   - `--algo sha1,md5,sha256,crc32,xxhash3_128` — select algorithms (comma-separated, multiple OK)
   - `--fast-hash` — use embedded SHA-1 from CHD header if available (single-file only)
   - **Output formats (default: simple, all 3 available):**
     - `--output simple` (DEFAULT) — stdout text: `filename hash_hex` (best for batch hashing, no file clutter)
     - `--output sfv` — creates `.sfv` file (standard verification format, ROM community standard)
     - `--output json` — creates `.json` file OR outputs JSON data to stdout (machine-readable, useful for DLL consumers)
   - `--embed-hash` (OPT-IN, non-default) — embed integrity data inside CHD for later verification (caveat: may affect emulator reads, disabled by default)
   - Never forces double-read for hashing

3. **No auto-hashing:** Extract/archive operations do NOT hash output unless user explicitly requests

4. **Archive result logging:**
   - **Batch mode (multiple files):** All logs consolidated into single `archive_batch.log` file (default)
   - **Single file mode:** `.log` file alongside CHD (e.g., `output.chd.log`)
   - **Option:** `--separate-logs` to create individual `.log` per archive (overrides batch consolidation)
   - Log contains:
     - Input analysis (type detected, CD system ID if applicable, file size)
     - Compression settings (codec selected, hunk size, reason for choice)
     - Results (output size, compression ratio, time, speed)
     - Hashes (SHA-1, MD5 if computed during archive)
   - Log also printed to console
   - Optional: `--embed-log` flag to embed log metadata inside CHD (non-default; caveat: may affect emulator reads)

5. **Main CLI** (`src/cli/chdlite/chdlite.cpp`)
   - Argument parsing (CLI11 library)
   - Dispatch to command classes
   - Progress bar for long operations
   - Exit codes: 0 = success, 1 = usage error, 2 = runtime error

6. **CMake build:**
   - Windows: `chdlite.exe`
   - macOS/Linux: `chdlite`

#### 4b. `read` — Drag-and-drop Quick-Hash Utility

1. **Purpose:** User drags file/folder onto `read.exe` → auto-extracts or hashes

2. **Behavior:**
   - Input = CHD file → auto-extract to temp folder, show output
   - Input = ISO/GDI/CUE → auto-archive to CHD, show result
   - Input = directory → auto-archive contents to CHD
   - Always hashes output (SHA-1 default, or use config file for algorithm choice)
   - Shows progress in GUI window (platform-specific: Win32 console, macOS Finder, Linux file manager)

3. **Implementation** (`src/cli/read-util/read.cpp`)
   - Parse command-line args (file paths from shell drag-drop)
   - Detect input type (CHD vs. image vs. directory)
   - Call appropriate API (extract/archive)
   - Compute hash on output
   - Display results (auto-open output folder)

4. **Cross-platform drag-drop:**
   - Windows: Shell context menu association (optional .reg file)
   - macOS: Can be added to Finder quick actions
   - Linux: File manager integration (optional)

**Verification:**
- `chdlite read file.chd` shows CHD info
- `chdlite extract input.chd output/` extracts with auto-format detection, NO hash
- `chdlite archive game.iso output.chd` creates CHD with codec defaults, NO hash
- `chdlite hash file.chd` computes SHA-1 from actual content (or use --fast-hash for header SHA-1)
- `chdlite hash file.chd --algo sha1,md5,sha256 --output sfv` outputs SFV file
- `read.exe game.iso` (drag-drop) auto-archives to CHD and auto-hashes output
- `read.exe game.chd` (drag-drop) auto-extracts and auto-hashes extracted files
- No double-reads for hashing operations

---

### Phase 5: Testing & CI

**Goal:** Validate functionality across platforms.

1. **Unit tests** (`tests/`)
   - Test CHD reading (valid/invalid files)
   - Test format detection (GDI, CUE, ISO detection)
   - Test hashing (CRC, MD5, SHA-1 match MAME)
   - Test extraction to each format
   - Test archiving with codec selection
   - Test CD system detection

2. **Test fixtures** — Add small CHD test files (or generate in-memory)
   - Valid CD CHD (GD-ROM)
   - Valid DVD CHD (PS2 ISO)
   - Damaged CHD (should fail gracefully)

3. **CI** (GitHub Actions)
   - Build on macOS, Linux, Windows
   - Run tests
   - Sign binaries (optional)

---

## CD System Codec Defaults

| System | Detection Method | Default Codec | Status |
|--------|------------------|---------------|--------|
| **PS2** | Boot magic 0x2E 0x58, DVD structure | ZLIB | Verified (Android PS2 emus) |
| **PSP** | UMD magic 0x55 0x4D 0x44 | ZSTD | Verified (PPSSPP) |
| **PS1** | "PLAYSTATION" in system area (0x8000) | ZSTD | Provisional |
| **PC Engine** | "PC ENGINE" magic in boot | ZSTD | Provisional |
| **Mega CD** | "SEGADISCSYSTEM" boot identifier | ZSTD | Provisional |
| **Saturn** | Saturn disc ID + structure | ZSTD | Provisional |
| **Neo Geo CD** | Neo Geo copyright string | ZSTD | Provisional |
| **3DO** | 3DO disc header 0x5A 0x5A 0x5A | ZSTD | Provisional |
| **GD-ROM** (Dreamcast) | Already detected at CHD level | MAME "zip" or ZSTD | Provisional |
| **DVD ISO** | DVD structure, all ISOs unmatched | ZLIB | Fallback |
| **Raw files/directory** | Non-disc input | ZSTD | Default |
| **Unknown/unmatched ISO** | Generic disc, no system match | ZLIB | Safe fallback |

---

## Configuration System

### Load Priority (highest to lowest)

1. Command-line `--codec` flag (if specified)
2. Project config file `./chdlite.conf` (current directory, if exists)
3. User config file `~/.chdlite/config.ini` (platform-specific home dir)
4. Built-in defaults from `codec_defaults.h`

### Config File Format (INI)

```ini
[codecs]
ps2=zlib          # Verified
psp=zstd          # Verified
ps1=zstd          # Provisional
pc_engine=zstd    # Provisional
mega_cd=zstd      # Provisional
saturn=zstd       # Provisional
neo_geo_cd=zstd   # Provisional
3do=zstd          # Provisional
gdrom=zstd        # Provisional
unknown=zlib      # Fallback
raw=zstd          # Raw data
```

### Usage Examples

```bash
# Uses built-in defaults (PS2 → ZLIB, PSP → ZSTD, etc.)
chdlite archive game.iso out.chd

# Override with command-line flag (highest priority)
chdlite archive game.iso out.chd --codec zstd

# Uses ~/.chdlite/config.ini (if exists; user's preference)
# Then falls back to built-in defaults

# Uses ./chdlite.conf if in project directory
# Then ~/.chdlite/config.ini, then built-in
```

---

## Key Features Summary

- **Single DLL** (CHDlite.dll/libCHDlite.dylib/libCHDlite.so) with embedded MAME CHD code
- **Two CLI tools**: `chdlite` (full-featured) and `read` (drag-drop utility)
- **Auto-detection**: 9 CD system types (PS1/PS2/PSP/PC Engine/Mega CD/Saturn/Neo Geo CD/3DO/GD-ROM)
- **Smart codec defaults**: Verified for PS2/PSP, configurable for others
- **Hash modes**: Content-based (default), header-based (--fast-hash), multiple algorithms
- **Hash output formats**: Simple (default), SFV, JSON
- **Configuration system**: Built-in defaults + user config + project config + CLI overrides
- **Batch logging**: Single archive_batch.log for multiple operations
- **No forced reads**: All hashing done during primary I/O operations (streaming)
- **GPL compliance**: MAME code embedded, CHDlite GPLv3 licensed

---

## Further Considerations

1. **Test Fixtures:** Start with generated in-memory test CHDs; add real samples later if needed

2. **Performance Optimization:** Simple streaming initially; profile later if needed

3. **Licensing:** CHDlite exposes MAME copyright/license in about/help

4. **Future Extensions:** Phase 1 focus: CHD ↔ disc images. File→CHD as phase 2

---

## Build Instructions

### Linux — All C++ Artifacts in One Pass

Builds all C++ targets in a single invocation, including:

- `chdlite` CLI binary
- Alias binaries (`chdread`, `chdhash`, `chdcomp`)
- Static libraries (`chdlite`, `mame_chd_core`, `lzma_sdk`)
- Shared library (`chdlite_shared`)
- FFI library (`chdlite_ffi`, auto-copied to `gui/app/linux/lib/libchdlite_ffi.so`)
- Test and benchmark binaries

**Quick build:**

```bash
bash scripts/build_all_linux.sh
```

**With optional flags:**

```bash
bash scripts/build_all_linux.sh --build-dir build --type Release --jobs 0 --with-flutter
```

**Options:**

- `--build-dir <path>` — CMake build directory (default: `build`)
- `--type <Debug|Release|RelWithDebInfo|MinSizeRel>` — CMake build type (default: `Release`)
- `--jobs <n>` — Parallel build jobs; `0` = auto-detect (default: `0`)
- `--with-flutter` — Also build Flutter Linux app in `gui/app/` (requires Flutter SDK, clang++, GTK dev packages)

**Notes:**

- FFI library is auto-copied to `gui/app/linux/lib/libchdlite_ffi.so` after the build completes.
- Flutter Linux build requires `clang++` compiler and GTK development files. Install with:
  ```bash
  sudo apt install -y clang libgtk-3-dev pkg-config
  ```
- If `cmake` or local tools are unavailable, see project root README for platform-specific setup instructions.

---
---

# APPENDIX A: MAME CHD Implementation Reference

> Research findings from full analysis of MAME source code. This appendix documents
> the exact MAME APIs, file dependencies, and OSD adapter requirements needed to
> build CHDlite.

---

## A1. CHD File Format (All Versions)

### Binary Layout

All CHD files begin with the magic tag `MComprHD` (8 bytes), followed by a version-specific header.

| Version | Header Size | Checksums | Compression | Notes |
|---------|-------------|-----------|-------------|-------|
| V1 | 76 bytes | MD5 | Single uint32 (zlib) | Hard-disk only, CHS geometry |
| V2 | 80 bytes | MD5 | Single uint32 (zlib) | Added seclen (sector size) |
| V3 | 120 bytes | MD5 + SHA-1 | Single uint32 (zlib) | Added logicalbytes, metaoffset, parentsha1 |
| V4 | 108 bytes | SHA-1 (combined + raw) | Single uint32 (zlib) | Dropped MD5, added rawsha1 |
| V5 | 124 bytes | SHA-1 (combined + raw + parent) | Array of 4 codecs | Current version. unitbytes, compressed map |

### V5 Header (Current)

```
[ 0] char     tag[8]          // 'MComprHD'
[ 8] uint32_t length          // Header length (124)
[12] uint32_t version         // 5
[16] uint32_t compressors[4]  // 4-char codec tags (e.g., 'cdlz', 'cdzl', 'cdfl', 0)
[32] uint64_t logicalbytes    // Logical size of uncompressed data
[40] uint64_t mapoffset       // Offset to compressed hunk map
[48] uint64_t metaoffset      // Offset to first metadata blob
[56] uint32_t hunkbytes       // Bytes per hunk (max 512KB)
[60] uint32_t unitbytes       // Bytes per unit within each hunk
[64] uint8_t  rawsha1[20]     // SHA-1 of raw data only
[84] uint8_t  sha1[20]        // Combined raw+metadata SHA-1
[104] uint8_t parentsha1[20]  // Parent CHD's combined SHA-1
[124] (end of header)
```

### Metadata Storage

Metadata is stored as a linked list of blobs after the main data. Each entry:
- 4-byte tag (e.g., `CHTR` for CD track, `GDDD` for hard disk)
- 4-byte length
- 1-byte flags (`CHD_MDFLAGS_CHECKSUM = 0x01`)
- 8-byte next-offset pointer
- Payload (text or binary)

### Standard Metadata Tags

| Tag | Constant | Format | Content |
|-----|----------|--------|---------|
| `GDDD` | `HARD_DISK_METADATA_TAG` | `"CYLS:%d,HEADS:%d,SECS:%d,BPS:%d"` | Hard disk geometry |
| `IDNT` | `HARD_DISK_IDENT_METADATA_TAG` | Binary | ATA identify data |
| `KEY ` | `HARD_DISK_KEY_METADATA_TAG` | Binary | Encryption key |
| `CIS ` | `PCMCIA_CIS_METADATA_TAG` | Binary | PCMCIA card info |
| `CHCD` | `CDROM_OLD_METADATA_TAG` | Legacy | Old CD format |
| `CHTR` | `CDROM_TRACK_METADATA_TAG` | `"TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d"` | CD track (V1 format) |
| `CHT2` | `CDROM_TRACK_METADATA2_TAG` | `"TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d"` | CD track (V2 format, with pregap) |
| `CHGT` | `GDROM_OLD_METADATA_TAG` | Legacy | Old GD-ROM format |
| `CHGD` | `GDROM_TRACK_METADATA_TAG` | `"TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d"` | GD-ROM track (with padframes) |
| `DVD ` | `DVD_METADATA_TAG` | Empty | DVD marker |
| `AVAV` | `AV_METADATA_TAG` | `"FPS:%d.%06d WIDTH:%d HEIGHT:%d INTERLACED:%d CHANNELS:%d SAMPLERATE:%d"` | A/V format |
| `AVLD` | `AV_LD_METADATA_TAG` | Binary | LaserDisc VBI data |

### Pseudo-Codecs (Map Entries)

| Value | Constant | Meaning |
|-------|----------|---------|
| 0 | `CHD_CODEC_NONE` | Uncompressed |
| 1 | `CHD_CODEC_SELF` | Copy from another hunk in same file |
| 2 | `CHD_CODEC_PARENT` | Copy from parent CHD |
| 3 | `CHD_CODEC_MINI` | Legacy 8-byte repeat pattern |

---

## A2. MAME `chd_file` Class API

### Core Accessors

```cpp
bool     opened() const noexcept;
uint32_t version() const noexcept;       // 1-5
uint64_t logical_bytes() const noexcept;
uint32_t hunk_bytes() const noexcept;
uint32_t hunk_count() const noexcept;
uint32_t unit_bytes() const noexcept;
uint64_t unit_count() const noexcept;
bool     compressed() const noexcept;     // true if compression[0] != NONE
chd_codec_type compression(int index) const noexcept; // index 0-3
chd_file* parent() const noexcept;
bool     parent_missing() const noexcept;
```

### SHA-1 Accessors

```cpp
util::sha1_t sha1() const noexcept;        // Combined (raw + metadata)
util::sha1_t raw_sha1() const noexcept;     // Raw data only
util::sha1_t parent_sha1() const noexcept;  // Parent's combined SHA-1
```

### File Operations

```cpp
// Open existing CHD
std::error_condition open(std::string_view filename, bool writeable = false,
                          chd_file* parent = nullptr,
                          const open_parent_func& open_parent = nullptr);
std::error_condition open(util::random_read_write::ptr&& file, ...);
void close();

// Create new CHD (4 overloads: filename/file × with/without parent)
std::error_condition create(std::string_view filename, uint64_t logicalbytes,
                            uint32_t hunkbytes, uint32_t unitbytes,
                            const chd_codec_type (&compression)[4]);
std::error_condition create(..., chd_file& parent);  // Delta CHD
```

### Read/Write

```cpp
std::error_condition read_hunk(uint32_t hunknum, void* buffer);
std::error_condition write_hunk(uint32_t hunknum, const void* buffer);
std::error_condition read_units(uint64_t unitnum, void* buffer, uint32_t count = 1);
std::error_condition write_units(uint64_t unitnum, const void* buffer, uint32_t count = 1);
std::error_condition read_bytes(uint64_t offset, void* buffer, uint32_t bytes);
std::error_condition write_bytes(uint64_t offset, const void* buffer, uint32_t bytes);
```

### Metadata

```cpp
std::error_condition read_metadata(chd_metadata_tag tag, uint32_t index, std::string& output);
std::error_condition read_metadata(chd_metadata_tag tag, uint32_t index, std::vector<uint8_t>& output);
std::error_condition write_metadata(chd_metadata_tag tag, uint32_t index, const std::string& input,
                                    uint8_t flags = CHD_MDFLAGS_CHECKSUM);
std::error_condition delete_metadata(chd_metadata_tag tag, uint32_t index);
std::error_condition clone_all_metadata(chd_file& source);
```

### Content Type Checks

```cpp
std::error_condition check_is_hd() const noexcept;   // Has HARD_DISK_METADATA_TAG
std::error_condition check_is_cd() const noexcept;   // Has CDROM_TRACK_METADATA*_TAG
std::error_condition check_is_gd() const noexcept;   // Has GDROM_*_METADATA_TAG
std::error_condition check_is_dvd() const noexcept;  // Has DVD_METADATA_TAG
std::error_condition check_is_av() const noexcept;   // Has AV_METADATA_TAG
```

### Compression Helper

```cpp
util::sha1_t compute_overall_sha1(util::sha1_t rawsha1);  // Combine raw + metadata hashes
std::error_condition hunk_info(uint32_t hunknum, chd_codec_type& compressor, uint32_t& compbytes);
```

---

## A3. MAME `cdrom_file` Class API

### Construction

```cpp
cdrom_file(chd_file* chd);              // Open from CHD
cdrom_file(std::string_view inputfile); // Open from CUE/GDI/TOC/NRG/ISO
~cdrom_file();
```

### Key Constants

```cpp
static constexpr uint32_t MAX_TRACKS       = 99;
static constexpr uint32_t MAX_SECTOR_DATA  = 2352;   // Max data per sector
static constexpr uint32_t MAX_SUBCODE_DATA = 96;      // Subchannel per sector
static constexpr uint32_t FRAME_SIZE       = 2448;    // 2352 + 96
static constexpr uint32_t FRAMES_PER_HUNK  = 8;       // Default hunk = 8 frames = 19584 bytes
static constexpr uint32_t TRACK_PADDING    = 4;       // Tracks padded to 4-frame boundary
```

### Track Types

| Enum | Value | Data Size | Description |
|------|-------|-----------|-------------|
| `CD_TRACK_MODE1` | 0 | 2048 | Mode 1 cooked (ISO) |
| `CD_TRACK_MODE1_RAW` | 1 | 2352 | Mode 1 raw |
| `CD_TRACK_MODE2` | 2 | 2336 | Mode 2 |
| `CD_TRACK_MODE2_FORM1` | 3 | 2048 | Mode 2 Form 1 |
| `CD_TRACK_MODE2_FORM2` | 4 | 2324 | Mode 2 Form 2 |
| `CD_TRACK_MODE2_FORM_MIX` | 5 | 2336 | Mode 2 Mixed |
| `CD_TRACK_MODE2_RAW` | 6 | 2352 | Mode 2 raw |
| `CD_TRACK_AUDIO` | 7 | 2352 | Audio (588 samples × 4 bytes) |

### Subcode Types

| Enum | Description |
|------|-------------|
| `CD_SUB_NORMAL` | Cooked 96-byte subcode |
| `CD_SUB_RAW` | Raw uninterleaved 96-byte |
| `CD_SUB_NONE` | No subcode data |

### Reading

```cpp
bool read_data(uint32_t lbasector, void* buffer, uint32_t datatype, bool phys = false);
bool read_subcode(uint32_t lbasector, void* buffer, bool phys = false);
```

### Track Accessors

```cpp
uint32_t get_track(uint32_t frame) const;
uint32_t get_track_start(uint32_t track) const;       // Logical frame offset
uint32_t get_track_start_phys(uint32_t track) const;   // Physical frame offset
const toc& get_toc() const;
bool is_gdrom() const;
int get_last_track() const;
```

### TOC Structure

```cpp
struct track_info {
    uint32_t trktype;        // CD_TRACK_MODE1, CD_TRACK_AUDIO, etc.
    uint32_t subtype;        // CD_SUB_NORMAL, CD_SUB_NONE, etc.
    uint32_t datasize;       // Bytes per sector for this track type
    uint32_t subsize;        // Subcode bytes per sector
    uint32_t frames;         // Number of frames
    uint32_t extraframes;    // Padding to 4-frame boundary
    uint32_t pregap;         // Pregap frame count
    uint32_t postgap;        // Postgap frame count
    uint32_t pgtype;         // Pregap sector type
    uint32_t pgsub;          // Pregap subcode type
    uint32_t padframes;      // GD-ROM gap frames
    uint32_t splitframes;    // Redump split-bin overlap
    uint32_t logframeofs;    // Logical frame offset
    uint32_t physframeofs;   // Physical frame offset
    uint32_t chdframeofs;    // CHD frame offset
    uint32_t session;        // Session number
    uint32_t control_flags;  // DCP, 4CH, PRE, DATA
    uint32_t multicuearea;   // 0=SINGLE_DENSITY, 1=HIGH_DENSITY (GDI)
};

struct toc {
    uint32_t numtrks;
    uint32_t numsessions;
    uint32_t flags;          // CD_FLAG_GDROM | CD_FLAG_MULTISESSION
    track_info tracks[MAX_TRACKS + 1];
};
```

### Static Parse Functions (for archiving)

```cpp
static std::error_condition parse_toc(std::string_view tocfname, toc& outtoc, track_input_info& outinfo);
static std::error_condition parse_cue(std::string_view tocfname, toc& outtoc, track_input_info& outinfo);
static std::error_condition parse_gdi(std::string_view tocfname, toc& outtoc, track_input_info& outinfo);
static std::error_condition parse_iso(std::string_view tocfname, toc& outtoc, track_input_info& outinfo);
static std::error_condition parse_nero(std::string_view tocfname, toc& outtoc, track_input_info& outinfo);
static bool is_gdicue(std::string_view tocfname);  // Checks for Redump multi-CUE GDI markers

// CHD metadata → TOC
static std::error_condition parse_metadata(chd_file* chd, toc& toc);
// TOC → CHD metadata
static std::error_condition write_metadata(chd_file* chd, const toc& toc);
```

---

## A4. Codec System

### Real Codecs

| Tag | Constant | Name | Description |
|-----|----------|------|-------------|
| `zlib` | `CHD_CODEC_ZLIB` | Deflate | Standard zlib compression |
| `zstd` | `CHD_CODEC_ZSTD` | Zstandard | Modern fast compression |
| `lzma` | `CHD_CODEC_LZMA` | LZMA | High-ratio compression |
| `huff` | `CHD_CODEC_HUFFMAN` | Huffman | Self-contained, no external lib |
| `flac` | `CHD_CODEC_FLAC` | FLAC | Lossless audio |
| `cdzl` | `CHD_CODEC_CD_ZLIB` | CD Deflate | CD frontend (splits data/subcode) + zlib |
| `cdzs` | `CHD_CODEC_CD_ZSTD` | CD Zstandard | CD frontend + zstd |
| `cdlz` | `CHD_CODEC_CD_LZMA` | CD LZMA | CD frontend + LZMA (data) + zlib (subcode) |
| `cdfl` | `CHD_CODEC_CD_FLAC` | CD FLAC | CD frontend + FLAC (audio) + zlib (subcode) |
| `avhu` | `CHD_CODEC_AVHUFF` | A/V Huffman | Audio/video combined (LaserDisc) |

### CD Codec Architecture

CD codecs use a template pattern that splits each CD frame (2448 bytes) into:
- **Data** (2352 bytes): Compressed with the primary algorithm (LZMA/ZLIB/ZSTD/FLAC)
- **Subcode** (96 bytes): Compressed separately (usually ZLIB)

Additionally, the CD compressor:
1. Detects and strips ECC data from Mode 1 sectors (regenerated on decompress)
2. Strips sync headers from Mode 1 sectors (regenerated on decompress)
3. Creates an ECC bitmap indicating which sectors had ECC stripped

### Chdman Default Compression Arrays

```cpp
// Raw/generic files
s_default_raw_compression = { CHD_CODEC_LZMA, CHD_CODEC_ZLIB, CHD_CODEC_HUFFMAN, CHD_CODEC_FLAC }

// Hard disk images
s_default_hd_compression  = { CHD_CODEC_LZMA, CHD_CODEC_ZLIB, CHD_CODEC_HUFFMAN, CHD_CODEC_FLAC }

// CD-ROM images (GDI/CUE/BIN)
s_default_cd_compression  = { CHD_CODEC_CD_LZMA, CHD_CODEC_CD_ZLIB, CHD_CODEC_CD_FLAC, 0 }

// LaserDisc images
s_default_ld_compression  = { CHD_CODEC_AVHUFF, 0, 0, 0 }
```

The compressor group tries all 4 codecs on each hunk and picks the smallest result.

### Compression Selection at Runtime

```cpp
// chd_compressor_group::find_best_compressor()
// For each hunk:
//   1. Try each of the 4 codecs in the array
//   2. Keep the one that produces the smallest output
//   3. If none compress smaller than the hunk, store uncompressed
//   4. If hunk is identical to another hunk, use CHD_CODEC_SELF
//   5. If hunk matches parent CHD, use CHD_CODEC_PARENT
```

---

## A5. Chdman Extract Logic

### Raw Extract (`do_extract_raw`)

Straightforward byte-streaming from CHD to file:
```
1. Open CHD (with parent if needed)
2. Parse start/end offsets (optional sub-range extraction)
3. Open output file
4. Loop: read_bytes(offset, buffer, chunk_size) → write to file
5. Progress callback at ~0.5s intervals
```

### CD Extract (`do_extract_cd`)

Complex format-aware extraction:
```
1. Open CHD
2. Create cdrom_file from CHD (parses track metadata into TOC)
3. Determine output mode from filename extension:
   - .cue → MODE_CUEBIN (split-bin or single-bin)
   - .gdi → MODE_GDI (one file per track)
   - .toc → MODE_NORMAL (raw with subcode)
4. For each track:
   a. Open track binary file
   b. Write track metadata to TOC/CUE/GDI descriptor
   c. Read frames via cdrom->read_data(physical_lba, buffer, track_type, phys=true)
   d. Byte-swap CDDA audio for CUE/GDI (big-endian CHD → little-endian BIN)
   e. Optionally read subcode via cdrom->read_subcode() (MODE_NORMAL only)
   f. Buffer sectors and flush to disk
5. Handle GD-ROM split frames (data spanning track boundaries)
6. Handle padframes (GD-ROM density area gaps)
```

### CD Archive (`do_create_cd`)

```
1. Parse input CUE/GDI/TOC via cdrom_file::parse_toc()
2. Pad tracks to 4-frame boundary (extraframes calculation)
3. Calculate total sector count
4. Create chd_cd_compressor wrapping track files
5. Create output CHD with CD-specific codecs and unit_bytes=FRAME_SIZE(2448)
6. Write track metadata via cdrom_file::write_metadata()
7. Run compress_common() which:
   a. Reads source data via compressor's read_data() override
   b. Tries all 4 codecs via find_best_compressor()
   c. Uses multi-threaded osd_work_queue for parallel compression
   d. Writes compressed hunks + SHA-1 computation
8. Set raw_sha1 on completion
```

### Raw/HD Archive (`do_create_raw`, `do_create_hd`)

```
1. Open input file
2. Parse geometry (for HD: CHS from template/params/ident file)
3. Parse compression (defaults or user-specified)
4. Create chd_rawfile_compressor wrapping input
5. Create output CHD with appropriate unit_bytes
6. Write type-specific metadata (HARD_DISK_METADATA_TAG, etc.)
7. Run compress_common()
```

---

## A6. File Dependency Graph

### Core CHD Files to Extract

```
src/lib/util/
├── chd.h, chd.cpp              # Core CHD format v1-v5 (REQUIRED)
├── chdcodec.h, chdcodec.cpp    # All compression codecs (REQUIRED)
├── cdrom.h, cdrom.cpp          # CD/GDI/CUE/NRG/ISO format (REQUIRED for disc images)
├── hashing.h, hashing.cpp      # SHA-1, MD5, CRC32 (REQUIRED - used by CHD internally)
├── md5.h                       # MD5 implementation (dependency of hashing.h)
├── flac.h, flac.cpp            # FLAC wrapper (REQUIRED for FLAC codec)
├── avhuff.h, avhuff.cpp        # A/V Huffman (REQUIRED for LaserDisc/AV codec)
├── huffman.h, huffman.cpp       # Huffman codec (REQUIRED for HUFFMAN codec + avhuff)
├── bitstream.h                 # Bit-level I/O (dependency of huffman)
├── corefile.h, corefile.cpp    # Buffered file I/O (REQUIRED - used by chd.cpp)
├── ioprocs.h, ioprocsimpl.h    # I/O abstractions (REQUIRED - random_read/write interfaces)
├── coretmpl.h                  # Template utilities (REQUIRED)
├── corestr.h, corestr.cpp      # String utilities (REQUIRED by cdrom.cpp)
├── multibyte.h                 # Big-endian helpers (REQUIRED)
├── path.h                      # Path utilities (REQUIRED by cdrom.cpp)
├── strformat.h, strformat.cpp  # Type-safe printf (REQUIRED)
├── utilfwd.h                   # Forward declarations (REQUIRED)
├── vecstream.h                 # Vector stream (dependency of coretmpl)
└── endianness.h                # Endian definitions (dependency of osdcomm)
```

### #include Dependency Tree

```
chd.cpp includes:
  chd.h → chdcodec.h, hashing.h, ioprocs.h, osdcore.h
  avhuff.h → huffman.h, flac.h, bitmap.h
  cdrom.h → chd.h, ioprocs.h, osdcore.h
  corefile.h → ioprocs.h, osdfile.h, strformat.h
  coretmpl.h → osdcomm.h, vecstream.h
  flac.h → utilfwd.h, <FLAC/all.h>
  hashing.h → md5.h, eminline.h
  multibyte.h → coretmpl.h, osdcomm.h
  eminline.h → osdcomm.h, osdcore.h
  <zlib.h>

chdcodec.cpp includes:
  chdcodec.h → chd.h
  avhuff.h, cdrom.h, chd.h, flac.h, hashing.h, multibyte.h
  lzma/C/LzmaDec.h, lzma/C/LzmaEnc.h
  <zlib.h>, <zstd.h>

cdrom.cpp includes:
  cdrom.h → chd.h, ioprocs.h, osdcore.h
  corestr.h, multibyte.h, osdfile.h, path.h, strformat.h
```

### Third-Party Libraries (All Required, No Conditional Compilation)

| Library | vcpkg Name | Used By | Purpose |
|---------|------------|---------|---------|
| zlib | `zlib` | chdcodec.cpp, hashing.cpp | Deflate codec, CRC32 |
| zstd | `zstd` | chdcodec.cpp | Zstandard codec |
| LZMA SDK | Embedded `3rdparty/lzma/C/` | chdcodec.cpp | LZMA codec |
| FLAC | `flac` | flac.cpp, avhuff.cpp | Audio codec |

---

## A7. OSD Adapter Layer (Detailed)

The OSD (Operating System Dependent) layer is the boundary between MAME's portable code and
platform-specific implementations. CHDlite must provide drop-in replacements for:

### 1. File I/O (`osdfile.h` → `osd-adapter/osdfile.h`)

MAME's `osd_file` provides:
```cpp
class osd_file {
public:
    using ptr = std::unique_ptr<osd_file>;
    
    static std::error_condition open(std::string_view path, uint32_t openflags,
                                     ptr& file, std::uint64_t& filesize);
    static std::error_condition remove(std::string_view filename);
    
    virtual std::error_condition read(void* buffer, std::uint64_t offset,
                                      std::uint32_t count, std::uint32_t& actual) = 0;
    virtual std::error_condition write(const void* buffer, std::uint64_t offset,
                                       std::uint32_t count, std::uint32_t& actual) = 0;
    virtual std::error_condition truncate(std::uint64_t offset) = 0;
    virtual std::error_condition flush() = 0;
};

// Open flags
constexpr uint32_t OPEN_FLAG_READ    = 0x0001;
constexpr uint32_t OPEN_FLAG_WRITE   = 0x0002;
constexpr uint32_t OPEN_FLAG_CREATE  = 0x0004;
constexpr uint32_t OPEN_FLAG_NO_BOM  = 0x0100;
```

**Adapter strategy:** Implement via POSIX `open()/read()/write()/lseek()/ftruncate()` on
macOS/Linux and `CreateFile()/ReadFile()/WriteFile()` on Windows, selected via `#ifdef`.

### 2. I/O Abstractions (`ioprocs.h` + `ioprocsimpl.h`)

MAME wraps `osd_file` into abstract stream interfaces:
```cpp
namespace util {
    struct read_stream {
        virtual std::pair<std::error_condition, std::size_t> read(void* buf, std::size_t count) = 0;
    };
    struct random_read : virtual read_stream {
        virtual std::error_condition seek(std::int64_t offset, int whence) = 0;
        virtual std::pair<std::error_condition, std::uint64_t> tell() = 0;
        virtual std::pair<std::error_condition, std::uint64_t> length() = 0;
    };
    struct random_read_write : virtual random_read {
        virtual std::pair<std::error_condition, std::size_t> write(const void* buf, std::size_t count) = 0;
        virtual std::error_condition truncate(std::uint64_t size) = 0;
        virtual std::error_condition flush() = 0;
    };
    
    // Factory wrappers
    random_read::ptr osd_file_read(osd_file::ptr&& file, ...);
    random_read_write::ptr osd_file_read_write(osd_file::ptr&& file, ...);
}
```

**Adapter strategy:** Extract `ioprocs.h` and `ioprocsimpl.h` as-is; they depend only on
`osdfile.h` which we're already adapting.

### 3. Core File (`corefile.h`)

`util::core_file` wraps `random_read_write` with buffered I/O and text line reading:
```cpp
class core_file : public random_read_write {
public:
    using ptr = std::unique_ptr<core_file>;
    
    static std::error_condition open(std::string_view filename, uint32_t flags, ptr& file);
    static std::error_condition load(std::string_view filename, std::vector<uint8_t>& data);
    
    int printf(const char* fmt, ...);  // Used by chdman for CUE/GDI generation
    char* gets(char* buf, int n);      // Used for line reading
};
```

**Adapter strategy:** Extract as-is. `core_file` depends on `osd_file` + `ioprocs` which
we're already adapting.

### 4. Work Queue / Threading (`osdcore.h`, `osdsync.h`)

Used by `chd_file_compressor` for parallel hunk compression:
```cpp
// Thread pool
osd_work_queue* osd_work_queue_alloc(int flags);
void osd_work_queue_free(osd_work_queue* queue);
int osd_work_queue_items(osd_work_queue* queue);
bool osd_work_queue_wait(osd_work_queue* queue, osd_ticks_t timeout);

// Work items
osd_work_item* osd_work_item_queue(osd_work_queue* queue,
                                    osd_work_callback callback,
                                    void* param, uint32_t flags);
bool osd_work_item_wait(osd_work_item* item, osd_ticks_t timeout);
void osd_work_item_release(osd_work_item* item);

// Timing
osd_ticks_t osd_ticks();
osd_ticks_t osd_ticks_per_second();

// Threading
int osd_get_num_processors();
```

**Adapter strategy:** Implement a C++17 thread pool using `std::thread` + `std::mutex` +
`std::condition_variable`. Map `osd_work_queue_alloc` → create thread pool with N workers,
`osd_work_item_queue` → submit task, `osd_work_item_wait` → future.get().
Use `std::chrono` for timing and `std::thread::hardware_concurrency()` for processor count.

### 5. Logging (`osdcore.h`)

```cpp
void osd_printf_error(const char* fmt, ...);
void osd_printf_warning(const char* fmt, ...);
void osd_printf_info(const char* fmt, ...);
void osd_printf_verbose(const char* fmt, ...);
void osd_printf_debug(const char* fmt, ...);
```

**Adapter strategy:** Forward to CHDlite's logger (or stderr as default).

### 6. Platform Macros (`osdcomm.h`, `eminline.h`)

`osdcomm.h` provides:
- Type definitions (already C++17 standard types)
- Endian detection macros
- `CLIB_DECL`, `ATTR_COLD`, `ATTR_HOT`, `ATTR_FORCE_INLINE` — map to compiler attributes

`eminline.h` provides optional optimization macros:
```cpp
inline int32_t mul_32x32(int32_t a, int32_t b);
inline uint32_t mulu_32x32_hi(uint32_t a, uint32_t b);
inline int64_t mul_32x32_shift(int32_t a, int32_t b, int shift);
constexpr uint32_t rotl_32(uint32_t val, int shift);
// ... all have portable C++ fallback implementations
```

**Adapter strategy:** Keep the portable C++ fallback implementations; drop inline asm
optimizations. All functions already have vanilla C++ versions.

### 7. Directory/Path Functions

```cpp
std::error_condition osd_stat(std::string_view path, osd_directory_entry& entry);
bool osd_is_absolute_path(std::string_view path);
```

**Adapter strategy:** Implement via `std::filesystem::status()` and `std::filesystem::path::is_absolute()`.

---

## A8. romexplorer → MAME API Migration Map

This table maps every romexplorer (libchdr C API) call to its MAME (C++ class) equivalent.
CHDlite will use the MAME column exclusively.

| Operation | romexplorer (libchdr) | MAME (chd_file) |
|-----------|----------------------|------------------|
| Open CHD | `chd_open(path, CHD_OPEN_READ, nullptr, &chd)` | `chd_file chd; chd.open(path)` |
| Close CHD | `chd_close(chd)` | `chd.close()` or destructor |
| Read header | `chd_read_header(path, &hdr)` / `chd_get_header(chd)` | `chd.version()`, `chd.logical_bytes()`, `chd.hunk_bytes()`, etc. |
| Read hunk | `chd_read(chd, hunk_num, buffer)` | `chd.read_hunk(hunk_num, buffer)` |
| Read bytes | Manual hunk reads + offset math | `chd.read_bytes(offset, buffer, size)` |
| Get metadata | `chd_get_metadata(chd, tag, idx, buf, bufsize, &rlen, &rtag, &rflags)` | `chd.read_metadata(tag, idx, string_or_vector)` |
| Parse tracks | Manual sscanf on metadata strings | `cdrom_file::parse_metadata(&chd, toc)` |
| Detect CD | Loop over metadata tags with `chd_get_metadata()` | `chd.check_is_cd()` |
| Detect GD-ROM | Check `GDROM_TRACK_METADATA_TAG` | `chd.check_is_gd()` |
| Detect DVD | Check `DVD_METADATA_TAG` | `chd.check_is_dvd()` |
| Detect HDD | Check `HARD_DISK_METADATA_TAG` | `chd.check_is_hd()` |
| Detect A/V | Check `AV_METADATA_TAG` | `chd.check_is_av()` |
| SHA-1 from header | `raw_hdr.sha1[20]` → `bytes_to_hex()` | `chd.sha1().as_string()` |
| Raw SHA-1 | `raw_hdr.rawsha1[20]` → `bytes_to_hex()` | `chd.raw_sha1().as_string()` |
| Compression type | `raw_hdr.compression[0]` switch | `chd.compression(0)` through `chd.compression(3)` |
| Read CD data | Manual hunk reads + sector extraction | `cdrom.read_data(lba, buffer, track_type, phys)` |
| Read subcode | Manual extraction from hunk | `cdrom.read_subcode(lba, buffer, phys)` |
| CUE reconstruction | Manual string building from metadata | Use `cdrom_file::get_toc()` + format per chdman logic |
| GDI reconstruction | Manual string building from metadata | Use `cdrom_file::get_toc()` + format per chdman logic |
| Audio byte-swap | Always swap on extraction | Conditional: check `CD_FLAG_GDROMLE`, version, output mode |
| Hashing engine | OpenSSL EVP + zlib CRC32 + xxhash | MAME: `sha1_creator`, `md5_creator` for SHA-1/MD5; keep external lib for SHA-256/384/512, CRC32, XXHASH3-128 |
| Create CHD | Not supported in romexplorer | `chd_file_compressor` subclass with `read_data()` override |
| Write metadata | Not supported | `chd.write_metadata(tag, index, data)` |

### Key Behavioral Differences

1. **Audio byte-swapping**: romexplorer always byte-swaps audio sectors. MAME only swaps
   conditionally based on:
   - GD-ROM LE flag (`CD_FLAG_GDROMLE`) — only old CHD versions
   - Output mode (CUE/GDI modes swap; NORMAL mode does not)
   - CHD version > 4 for GDI mode
   
   **CHDlite should follow MAME's conditional logic** to match chdman output exactly.

2. **Track offset calculation**: romexplorer calculates cumulative frame offsets manually.
   MAME's `cdrom_file` does this internally in `parse_metadata()`, populating
   `track_info::chdframeofs`, `logframeofs`, `physframeofs`.
   
   **CHDlite should use `cdrom_file::get_track_start_phys()`** instead of manual math.

3. **Pregap handling**: romexplorer uses `t.pregap` and `t.pgtype` from metadata strings.
   MAME's `cdrom_file::read_data()` handles pregap internally (returns zeros for pregap
   sectors not physically present in CHD).
   
   **CHDlite should let `cdrom_file::read_data()` handle pregap** automatically.

4. **ECC regeneration**: romexplorer does not regenerate ECC (extracts raw bytes).
   MAME's CD decompressor automatically regenerates sync headers and ECC data on
   decompression, so extracted raw sectors are complete.
   
   **CHDlite gets correct ECC automatically** through MAME's codec layer.

5. **Split-frame GD-ROM**: romexplorer doesn't handle split frames (data from previous
   track stored at end). MAME's chdman explicitly handles this.
   
   **CHDlite should replicate chdman's split-frame logic** for bit-perfect GDI extraction.

6. **Content type → CHDlite enum mapping**: romexplorer's `ChdContentType` enum needs
   expansion to cover all MAME content types:

   | MAME check | romexplorer enum | CHDlite enum (proposed) |
   |-----------|-----------------|------------------------|
   | `check_is_cd()` | `CD_CUE_BIN` / `CD_CUE_MULTI` | `CD` (single field, track count separate) |
   | `check_is_gd()` | `GDROM` | `GDROM` |
   | `check_is_dvd()` | `ISO` | `DVD` |
   | `check_is_hd()` | `HDD` | `HDD` |
   | `check_is_av()` | `LASERDISC` | `LASERDISC` |
   | None match | `UNKNOWN` | `RAW` (generic binary) |

7. **Compression enum expansion**: romexplorer's `ArchiveSubtype` should be expanded:

   | romexplorer | CHDlite (proposed) |
   |-------------|-------------------|
   | `CHD_ZLIB` | `ZLIB` |
   | `CHD_ZSTD` | `ZSTD` |
   | `CHD_LZMA` | `LZMA` |
   | `CHD_FLAC` | `FLAC` |
   | `CHD_UNCOMPRESSED` | `NONE` |
   | *(missing)* | `HUFFMAN` |
   | *(missing)* | `CD_ZLIB` |
   | *(missing)* | `CD_ZSTD` |
   | *(missing)* | `CD_LZMA` |
   | *(missing)* | `CD_FLAC` |
   | *(missing)* | `AVHUFF` |

### Hashing Strategy

MAME provides built-in `sha1_creator` and `md5_creator` (self-contained, no external deps).
However, CHDlite needs SHA-256, SHA-384, SHA-512, CRC32, and XXHASH3-128 which MAME doesn't have.

**Recommended approach:**
- **SHA-1, MD5**: Use MAME's built-in implementations (already compiled in, zero deps)
- **CRC32**: Use zlib's `crc32()` (already linked for codec support)
- **SHA-256, SHA-384, SHA-512**: Add lightweight implementation (e.g., embedded public-domain
  code or use platform crypto: CommonCrypto on macOS, BCrypt on Windows, OpenSSL on Linux)
- **XXHASH3-128**: Embed xxHash source (single .h file, BSD license)

This avoids forcing an OpenSSL dependency on all platforms while still supporting all
hash algorithms from the original romexplorer.
