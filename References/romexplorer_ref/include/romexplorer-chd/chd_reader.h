#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <expected>
#include "hashing.h"
#include "models.h"

namespace romexplorer {

// Info extracted from CHD header (fast, no decompression)
struct ChdHeaderInfo {
    uint32_t version = 0;
    uint64_t logical_bytes = 0;
    uint32_t hunk_bytes = 0;
    uint32_t total_hunks = 0;
    std::string sha1_hex;        // combined raw+meta SHA1 → matches MAME_REDUMP DATs
    std::string rawsha1_hex;     // raw data only SHA1 (V3-V5)
    std::string md5_hex;         // V1/V2/V3 only
    ArchiveSubtype compression_type = ArchiveSubtype::NOT_ARCHIVE; // V5: primary compressor; V1-V4: derived from version
};

// Track info parsed from CHD metadata
struct ChdTrackInfo {
    uint32_t track_num = 0;
    std::string type;            // "MODE1/2048", "MODE2/2352", "AUDIO", etc.
    std::string subtype;         // "NONE", "RW_RAW", etc.
    uint32_t frames = 0;         // number of data frames in this track (includes pregap data when pgtype starts with "V")
    uint32_t pad = 0;            // GD-ROM PAD field (NOT the same as extraframes padding)
    uint32_t pregap = 0;         // pregap frame count (metadata only; not separate storage in CHD)
    uint32_t postgap = 0;
    std::string pgtype;          // pregap type from metadata (e.g. "MODE1", "VAUDIO", "VMODE1_RAW")
                                 // "V" prefix means pregap data IS included in frames count
};

// Returns header info including the SHA1 used by MAME_REDUMP DATs.
// Fast: reads only the header bytes, no decompression.
ChdHeaderInfo read_chd_header(const std::string& file_path);

// Returns the track listing from CHD metadata.
// Fast: no decompression needed.
std::vector<ChdTrackInfo> get_chd_tracks(const std::string& file_path);

// Hashes raw sector data (2352 bytes/sector) for each track.
// Returns one HashesResults per track, in track order.
// Use for matching against Redump split-bin track hashes (CRC32/MD5/SHA1).
// Each track's BIN contains exactly 'frames' sectors (includes pregap data when present).
std::vector<HashesResults> hash_chd_tracks(
    const std::string& file_path,
    const std::vector<HashAlgorithm>& hash_types = {
        HashAlgorithm::CRC32, HashAlgorithm::MD5, HashAlgorithm::SHA1
    });

// Reconstructs the CUE file content from CHD track metadata.
// Uses chdman split-bin format: one FILE directive per track.
// base_name: the stem name for track files (e.g. "Game (Japan)").
//            Track filenames: "base_name (Track N).bin" with appropriate zero-padding.
// Uses CRLF line endings, trailing CRLF.
std::string reconstruct_cue_content(const std::string& file_path, const std::string& base_name);

// Hashes the reconstructed CUE file content.
// Use for matching against Redump CUE file hashes in DATs.
// base_name: the stem name for track files (e.g. "Game (Japan)").
HashesResults hash_chd_cue(
    const std::string& file_path,
    const std::string& base_name,
    const std::vector<HashAlgorithm>& hash_types = {
        HashAlgorithm::CRC32, HashAlgorithm::MD5, HashAlgorithm::SHA1
    });

// Extracts CHD contents to output_dir with format-specific naming.
// 
// base_name: output filename prefix (without extension). If empty, uses the CHD filename without extension.
//            Examples: "game_name" → outputs files; "" → uses CHD name
//
// Naming patterns by format:
//   - Single-track (ISO, simple CD):   game_name.iso or game_name.bin
//                                      ⚠️  NOT_TESTED 
//   - CD (CUE/BIN):                    game_name.cue + game_name.bin (single interleaved file)
//   - GDI (Dreamcast):                 game_name.gdi + track01.bin, track02.bin, ...
//                                      ⚠️  PROTOTYPE: Limited testing on Dreamcast samples. May need refinement.
//   - Multi-track CUE/BIN (future):    game_name.cue + track01.bin, track02.bin, track01.wav, track02.wav, ...
//                                      ⚠️  PROTOTYPE: Not yet fully researched across diverse disc samples.
//
// Returns list of written file paths.
std::expected<std::vector<std::string>, ScanError>
extract_chd(const std::string& chd_path, const std::string& output_dir, const std::string& base_name = "");

// Reconstructs GDI file content from CHD GD-ROM metadata.
// Uses chdman GDI naming: "basename%02d.ext" with quoted filenames.
// LBA derived from cumulative frame counts (no pregap, no pad).
// base_name: stem name for track files. If empty, derived from CHD filename.
// Uses CRLF line endings.
std::string reconstruct_gdi_content(const std::string& file_path, const std::string& base_name = "");

// Hashes the reconstructed GDI file content.
// Use for matching against Redump GDI file hashes in DATs.
// ⚠️  PROTOTYPE: GDI support is under development.
HashesResults hash_chd_gdi(
    const std::string& file_path,
    const std::vector<HashAlgorithm>& hash_types = {
        HashAlgorithm::CRC32, HashAlgorithm::MD5, HashAlgorithm::SHA1
    });

// Detects CHD content type (ISO, CD, GDROM, HDD, LaserDisc, MAME arcade, etc.)
// Analyzes CHD header metadata tags and track structure to determine content format.
// Returns ChdContentType enum value for storage in FileData.
ChdContentType detect_chd_content_type(const std::string& chd_path);

} // namespace romexplorer
