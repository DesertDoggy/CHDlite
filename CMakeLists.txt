cmake_minimum_required(VERSION 3.20)
project(CHDlite VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler options
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Find external libraries
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(FLAC CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)

# utf8proc
find_package(utf8proc CONFIG REQUIRED)

# =====================================================================
# LZMA SDK (embedded 3rdparty)
# =====================================================================
add_library(lzma_sdk STATIC
    src/3rdparty/lzma/C/LzmaDec.c
    src/3rdparty/lzma/C/LzmaEnc.c
    src/3rdparty/lzma/C/LzFind.c
    src/3rdparty/lzma/C/CpuArch.c
)
target_include_directories(lzma_sdk PUBLIC src/3rdparty/lzma/C)
target_compile_definitions(lzma_sdk PRIVATE _7ZIP_ST Z7_ST)   # single-threaded LZMA
set_target_properties(lzma_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================
# MAME CHD core library
# =====================================================================

# Collected source files
set(MAME_UTIL_SOURCES
    src/mame/util/chd.cpp
    src/mame/util/chdcodec.cpp
    src/mame/util/cdrom.cpp
    src/mame/util/hashing.cpp
    src/mame/util/md5.cpp
    src/mame/util/corefile.cpp
    src/mame/util/ioprocs.cpp
    src/mame/util/avhuff.cpp
    src/mame/util/huffman.cpp
    src/mame/util/flac.cpp
    src/mame/util/bitmap.cpp
    src/mame/util/palette.cpp
    src/mame/util/unicode.cpp
    src/mame/util/corestr.cpp
    src/mame/util/path.cpp
    src/mame/util/strformat.cpp
    src/mame/util/vecstream.cpp
    src/mame/util/hash.cpp
)

set(MAME_OSD_SOURCES
    src/mame/osd/osdcore_impl.cpp
    src/mame/osd/osdfile_impl.cpp
    src/mame/osd/osdsync.cpp
    src/mame/osd/strconv.cpp
)

add_library(mame_chd_core STATIC
    ${MAME_UTIL_SOURCES}
    ${MAME_OSD_SOURCES}
)

target_include_directories(mame_chd_core
    PUBLIC
        src/mame/util
        src/mame/osd
    PRIVATE
        src/3rdparty                   # for "lzma/C/LzmaDec.h"
)

# Suppress warnings from MAME code we don't own
if(NOT MSVC)
    target_compile_options(mame_chd_core PRIVATE -Wno-sign-compare -Wno-unused-variable -Wno-missing-field-initializers)
endif()

target_link_libraries(mame_chd_core
    PUBLIC
        ZLIB::ZLIB
        zstd::libzstd_static
        FLAC::FLAC
        FLAC::FLAC++
        lzma_sdk
        xxHash::xxhash
)

target_link_libraries(mame_chd_core PUBLIC utf8proc::utf8proc)

# Platform defines
target_compile_definitions(mame_chd_core PRIVATE CRLF=2)  # LF line endings
if(WIN32)
    target_compile_definitions(mame_chd_core PRIVATE OSD_WINDOWS CRLF=3)
elseif(APPLE)
    target_compile_definitions(mame_chd_core PRIVATE SDLMAME_MACOSX)
else()
    target_compile_definitions(mame_chd_core PRIVATE SDLMAME_LINUX)
endif()

set_target_properties(mame_chd_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# =====================================================================
# CHDlite public API library
# =====================================================================
add_library(chdlite STATIC
    src/chd-api/chd_reader.cpp
    src/chd-api/chd_processor.cpp
    src/chd-api/chd_extractor.cpp
    src/chd-api/chd_archiver.cpp
)

target_include_directories(chdlite
    PUBLIC
        src/chd-api
    PRIVATE
        src/mame/util
        src/mame/osd
)

target_link_libraries(chdlite PUBLIC mame_chd_core)

# =====================================================================
# Smoke test executable
# =====================================================================
add_executable(smoke_test tests/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE chdlite)
