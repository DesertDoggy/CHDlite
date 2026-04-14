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
│   │   │   ├── cdrom.h, cdrom.cpp      # CD format parsing (GDI, CUE, NRG)
│   │   │   ├── hashing.h, hash.h       # SHA-1, MD5 utilities
│   │   │   ├── corefile.h, corefile.cpp
│   │   │   ├── coretmpl.h
│   │   │   ├── corestr.h
│   │   │   ├── multibyte.h
│   │   │   ├── path.h
│   │   │   ├── strformat.h
│   │   │   ├── avhuff.h, avhuff.cpp
│   │   │   └── flac.h
│   │   └── osd-adapter/                # Compatibility layer replacing MAME's src/osd/
│   │       ├── eminline.h              # Platform macros (GCC/MSVC optimizations)
│   │       ├── osdfile.h, osdfile.cpp  # File I/O abstraction (POSIX/Win32)
│   │       ├── osdcomm.h               # OS detection macros
│   │       └── osd_work_queue.h        # Thread pool for async compression
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
