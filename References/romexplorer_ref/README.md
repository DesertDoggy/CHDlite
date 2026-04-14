# RomExplorer CHD DLL Reference

This is a reference implementation for extracting CHD reader functionality into a separate DLL/shared library suitable for reuse or adaptation (e.g., building a chdman-like tool using MAME code).

## Dependencies

### Required Libraries

- **libchdr** (BSD-3-Clause) - CHD file format support
  - Automatically fetches transitive dependencies via vcpkg
  - Dependencies:
    - zlib
    - openssl
    - liblzma
    - libzstd
    - flac (optional, for FLAC compression)

- **magic_enum** - Compile-time enum name/value mapping
- **xxhash** - Fast 128-bit hashing (XXHASH3-128)
- **openssl** - Cryptographic hashing (MD5, SHA1, SHA256, SHA384, SHA512)
- **zlib** - CRC32 computation and compression

### Optional (for testing)

- **Catch2 3.x** - C++ unit testing framework (if `BUILD_TESTS=ON`)

## Build Instructions

### macOS

```bash
# Prerequisites
brew install cmake

# Configure with vcpkg package management
mkdir -p build && cd build

cmake -DCMAKE_TOOLCHAIN_FILE=$(vcpkg fetch cmake)/toolchains/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      ..

# Build shared library
cmake --build . --config Release

# Optional: Run tests
cmake --build . --config Release --target chd_dll_tests
ctest
```

### Linux

```bash
mkdir -p build && cd build

cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      ..

cmake --build . --config Release
```

### Windows (MSVC)

```bash
mkdir build && cd build

cmake -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake ^
      -DCMAKE_BUILD_TYPE=Release ^
      ..

cmake --build . --config Release
```

## Output

- **libRomExplorer-CHD.dylib** (macOS)
- **libRomExplorer-CHD.so** (Linux)
- **RomExplorer-CHD.dll** (Windows)

## Public API

All APIs in `namespace romexplorer`:

### Models (enums & structs)
- `ArchiveSubtype` - CHD compression type
- `ChdContentType` - Detected content (CD, ISO, GD-ROM, HDD, etc.)
- `HashAlgorithm` - Supported hashing algorithms
- `ScanError` - Error codes
- `ChdHeaderInfo` - CHD header information
- `ChdTrackInfo` - CD/GD-ROM track metadata
- `HashesResults` - Multi-algorithm hash results

### CHD Reading
- `read_chd_header()` - Fast header-only extraction
- `get_chd_tracks()` - Extract track metadata
- `detect_chd_content_type()` - Identify content type
- `extract_chd()` - Extract to CUE/BIN, GDI, or ISO
- `hash_chd_tracks()` - Hash per-track data (Redump compatible)
- `hash_chd_cue()` - Hash reconstructed CUE file
- `reconstruct_cue_content()` - Generate CUE sheet
- `reconstruct_gdi_content()` - Generate GDI descriptor

### Hashing
- `calculate_hashes()` - Hash file with multiple algorithms
- `calculate_hashes_from_memory()` - Hash buffer in memory
- `get_hash_from_HashesResults()` - Extract single hash
- `get_supported_algorithms()` - List available algos

### Logging
- `Logger::instance()` - Singleton logger
- `Logger::initialize()` - Set log level
- `LOG_DEBUG()`, `LOG_INFO()` - Logging macros
- `LOG_DEBUG_STREAM()` - Stream-based logging

## Notes

- All includes use `#include <romexplorer-chd/...>` namespace
- Suitable for adaptation with MAME chdman code (GPL-compatible)
- Licensing: Inherits from dependencies (BSD-3-Clause via libchdr)
- No GUI dependencies; pure C++17 library
