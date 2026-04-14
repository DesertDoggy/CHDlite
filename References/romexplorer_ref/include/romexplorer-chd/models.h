#pragma once

#include <cstdint>
#include <string>

namespace romexplorer {

// ============================================================================
// CHD-Specific Enums
// ============================================================================

/**
 * Archive compression format for CHD files
 */
enum class ArchiveSubtype : std::uint32_t {
    NOT_ARCHIVE = 0,
    CHD_ZLIB = 3,
    CHD_LZMA = 4,
    CHD_ZSTD = 5,
    CHD_UNCOMPRESSED = 6,
    CHD_FLAC = 7,
    OTHER = 255
};

/**
 * CHD file content type (detected by metadata probing)
 * Used to determine extraction format (CUE/BIN vs GDI vs ISO)
 */
enum class ChdContentType : std::uint32_t {
    UNKNOWN = 0,
    ISO = 1,              // Single-track ISO (DVD, Xbox, PS2, Wii, etc.)
    CD_CUE_BIN = 2,       // CD with metadata (single-track CUE/BIN)
    CD_CUE_MULTI = 3,     // Multi-track CD (CUE/BIN per track)
    GDROM = 4,            // GD-ROM (Dreamcast)
    HDD = 5,              // Hard drive image
    LASERDISC = 6,        // LaserDisc
    MAME_ARCADE = 7,      // MAME arcade
    OTHER = 255
};

/**
 * Hash algorithms supported by CHD reader
 */
enum class HashAlgorithm : std::uint32_t {
    CRC32 = 0,
    MD5 = 1,
    SHA1 = 2,
    SHA256 = 3,
    SHA384 = 4,
    SHA512 = 5,
    XXHASH3_128 = 16  // Fast 128-bit hash
};

/**
 * Scan/operation error codes
 */
enum class ScanError : std::uint32_t {
    NONE = 0,
    FILE_NOT_FOUND = 1,
    READ_ERROR = 4,
    HASH_ERROR = 5,
    CORRUPT_ARCHIVE = 6,
    UNKNOWN_ERROR = 255
};

}  // namespace romexplorer
