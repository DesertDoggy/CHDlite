cmake_minimum_required(VERSION 3.20)

# Force static linking and Windows triplet setup
if(WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET x64-windows-static)
endif()

# Set vcpkg toolchain if not already set
if(NOT CMAKE_TOOLCHAIN_FILE AND WIN32)
    set(CMAKE_TOOLCHAIN_FILE "C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()

project(CHDlite VERSION 0.1.0 LANGUAGES C CXX)

set(BUILD_SHARED_LIBS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output directories for release
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Release/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Release/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Release/lib")
    
    # Override for multi-config generators (Visual Studio)
    foreach(CONF Release Debug RelWithDebInfo MinSizeRel)
        string(TOUPPER ${CONF} CONF_UPPER)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONF_UPPER} "${CMAKE_SOURCE_DIR}/Release/bin")
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONF_UPPER} "${CMAKE_SOURCE_DIR}/Release/lib")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONF_UPPER} "${CMAKE_SOURCE_DIR}/Release/lib")
    endforeach()
endif()

# Compiler options
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# =====================================================================
# Architecture-specific SIMD compile flags
# =====================================================================
# x86-64: AVX2 baseline (Intel Haswell 2013+ / AMD Excavator 2015+)
# ARM64:  NEON always available; crypto extensions on most modern SoCs
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|x86-64")
    if(MSVC)
        add_compile_options(/arch:AVX2)
    else()
        add_compile_options(-mavx2 -msse4.2 -mssse3)
    endif()
    add_compile_definitions(CHDLITE_X86_SIMD=1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    # Apple Silicon and most modern ARM64 enable crypto extensions by default.
    # On Linux arm64 toolchains that don't, uncomment:
    #   add_compile_options(-march=armv8-a+crypto)
    add_compile_definitions(CHDLITE_ARM64_SIMD=1)
endif()

# Find external libraries
find_package(ZLIB REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(FLAC CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

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
target_include_directories(lzma_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/3rdparty/lzma/C>
)
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

# strconv.cpp is Windows-specific and conflicts with osdcore_impl.cpp on Windows
# Only include it on non-Windows platforms
if(NOT WIN32)
    list(APPEND MAME_OSD_SOURCES
        src/mame/osd/strconv.cpp
    )
endif()

add_library(mame_chd_core STATIC
    ${MAME_UTIL_SOURCES}
    ${MAME_OSD_SOURCES}
)

target_include_directories(mame_chd_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/mame/util>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/mame/osd>
    PRIVATE
        src/3rdparty                   # for "lzma/C/LzmaDec.h"
)

# Suppress warnings from MAME code we don't own
if(NOT MSVC)
    target_compile_options(mame_chd_core PRIVATE -Wno-sign-compare -Wno-unused-variable -Wno-missing-field-initializers)
endif()

# Handle zstd target name differences across platforms
if(TARGET zstd::libzstd_static)
    set(ZSTD_TARGET zstd::libzstd_static)
else()
    set(ZSTD_TARGET zstd::libzstd)
endif()

target_link_libraries(mame_chd_core
    PUBLIC
        ZLIB::ZLIB
        ${ZSTD_TARGET}
        FLAC::FLAC
        FLAC::FLAC++
        lzma_sdk
        xxHash::xxhash
)

target_link_libraries(mame_chd_core PUBLIC utf8proc::utf8proc)

# Platform defines
target_compile_definitions(mame_chd_core PRIVATE CRLF=2)  # LF line endings
if(WIN32)
    target_compile_definitions(mame_chd_core PRIVATE OSD_WINDOWS CRLF=3 NOMINMAX)  # NOMINMAX prevents min/max macros
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
    src/chd-api/detect_game_platform.cpp
)

target_include_directories(chdlite
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/chd-api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/chdlite>
    PRIVATE
        src/mame/util
        src/mame/osd
)

target_link_libraries(chdlite PUBLIC mame_chd_core)

# =====================================================================
# CHDlite shared library (DLL + import lib)
# =====================================================================
add_library(chdlite_shared SHARED
    src/chd-api/chd_reader.cpp
    src/chd-api/chd_processor.cpp
    src/chd-api/chd_extractor.cpp
    src/chd-api/chd_archiver.cpp
    src/chd-api/detect_game_platform.cpp
)

target_include_directories(chdlite_shared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/chd-api>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/chdlite>
    PRIVATE
        src/mame/util
        src/mame/osd
)

target_link_libraries(chdlite_shared PUBLIC mame_chd_core)

# Configure DLL and import library output names
set_target_properties(chdlite_shared PROPERTIES
    OUTPUT_NAME chdlite                # DLL named: chdlite.dll
    PREFIX ""                          # No lib prefix
    SUFFIX ".dll"
)

# Windows: copy import library with new name (cdhilite_shared.lib)
if(WIN32)
    add_custom_command(TARGET chdlite_shared POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/chdlite.lib"
            "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/cdhilite_shared.lib"
        COMMENT "Creating cdhilite_shared.lib copy"
        VERBATIM
    )
endif()

# =====================================================================
# Install rules (cmake --install build --prefix /some/path)
# Produces headers + static libs consumable via find_package(CHDlite)
# =====================================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS chdlite mame_chd_core lzma_sdk
    EXPORT CHDliteTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    src/chd-api/chd_api.hpp
    src/chd-api/chd_types.hpp
    src/chd-api/chd_reader.hpp
    src/chd-api/chd_processor.hpp
    src/chd-api/chd_extractor.hpp
    src/chd-api/chd_archiver.hpp
    src/chd-api/detect_game_platform.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/chdlite
)

install(EXPORT CHDliteTargets
    FILE CHDliteTargets.cmake
    NAMESPACE CHDlite::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CHDlite
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CHDliteConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CHDliteConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CHDlite
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CHDliteConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/CHDliteConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CHDliteConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CHDlite
)

# =====================================================================
# Only build tests and CLI when this is the top-level project
# (skip when pulled in via add_subdirectory)
# =====================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)

# Smoke test executable
add_executable(smoke_test tests/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE chdlite)

add_executable(detect_test tests/detect_test.cpp)
target_link_libraries(detect_test PRIVATE chdlite)

add_executable(archive_test tests/archive_test.cpp)
target_link_libraries(archive_test PRIVATE chdlite)

add_executable(cli_test tests/cli_test.cpp)
# cli_test only uses std::system() to invoke the CLI binary, no library link needed

# CLI tool
add_executable(chdlite_cli src/cli/chdlite_cli.cpp)
target_link_libraries(chdlite_cli PRIVATE chdlite spdlog::spdlog)
set_target_properties(chdlite_cli PROPERTIES OUTPUT_NAME chdlite)

# Create chdread / chdhash aliases (symlinks on Unix, copies on Windows)
if(WIN32)
    add_custom_command(TARGET chdlite_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:chdlite_cli>" "$<TARGET_FILE_DIR:chdlite_cli>/chdread.exe"
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:chdlite_cli>" "$<TARGET_FILE_DIR:chdlite_cli>/chdhash.exe"
        COMMENT "Creating chdread.exe and chdhash.exe copies"
    )
else()
    add_custom_command(TARGET chdlite_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "chdlite" "$<TARGET_FILE_DIR:chdlite_cli>/chdread"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "chdlite" "$<TARGET_FILE_DIR:chdlite_cli>/chdhash"
        COMMENT "Creating chdread and chdhash symlinks"
    )
    # Self-sign on macOS to avoid Gatekeeper warnings
    if(APPLE)
        add_custom_command(TARGET chdlite_cli POST_BUILD
            COMMAND codesign -s - --force --deep "$<TARGET_FILE:chdlite_cli>"
            COMMENT "Self-signing chdlite binary"
        )
    endif()
endif()

endif()
