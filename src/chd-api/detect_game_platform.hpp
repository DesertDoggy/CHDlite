// license:GPLv3
// CHDlite - Cross-platform CHD library
// detect_game_platform.hpp - Platform auto-detection for disc images

#ifndef CHDLITE_DETECT_GAME_PLATFORM_HPP
#define CHDLITE_DETECT_GAME_PLATFORM_HPP

#include "chd_types.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace chdlite {

class ChdReader;

// Flags to control which detection loops run (bitmask)
enum class DetectFlags : uint32_t
{
    None       = 0,
    Sector0    = 1 << 0,   // Loop 1: sector 0 magic bytes (3DO, MegaCD, Saturn, Dreamcast)
    Iso9660    = 1 << 1,   // Loop 2: ISO 9660 filesystem checks (PS1, PS2, PSP, NeoGeoCD, DVD-Video)
    Heuristic  = 1 << 2,   // Loop 3: heuristic checks (PC Engine CD)
    All        = (1 << 0) | (1 << 1) | (1 << 2)
};

inline DetectFlags operator|(DetectFlags a, DetectFlags b) { return DetectFlags(uint32_t(a) | uint32_t(b)); }
inline DetectFlags operator&(DetectFlags a, DetectFlags b) { return DetectFlags(uint32_t(a) & uint32_t(b)); }
inline bool has_detect_flag(DetectFlags flags, DetectFlags f) { return (uint32_t(flags) & uint32_t(f)) != 0; }

// Abstraction for reading 2048-byte cooked sectors by LBA.
// Returns true on success, false on error.
using SectorReader = std::function<bool(uint32_t lba, uint8_t* buffer)>;

// Result of detection — system, optional title/game ID, and format hint for raw input
struct DetectionResult
{
    GamePlatform    game_platform = GamePlatform::Unknown;
    std::string title;       // extracted game title (empty if not requested or unavailable)
    std::string manufacturer_id;     // product/serial number (e.g. SCPS_100.50, T-9527G)
    std::string format;      // "cd", "dvd", or "raw" (used by detect_input)
};

// Info about the input disc layout (for raw file detection)
struct InputDiscInfo
{
    ContentType content_type = ContentType::Unknown;    // CD, DVD, or Raw
    uint32_t    sector_size  = 0;                       // bytes per sector (2048, 2336, 2352)
    uint32_t    num_tracks   = 0;
    std::vector<TrackInfo> tracks;                      // populated for CUE/GDI parsed inputs
};

// Core detection: works on any SectorReader (CHD-backed or file-backed).
// content_type hints the dispatch path (CDROM/GDROM/DVD).
// tracks is needed for heuristic checks (e.g. PC Engine).
// If detect_title is true, also extracts game title from filesystem.
DetectionResult detect_game_platform(const SectorReader& read_sector,
                              ContentType content_type,
                              const std::vector<TrackInfo>& tracks = {},
                              DetectFlags flags = DetectFlags::All,
                              bool detect_title = false);

// Convenience: detect from an open CHD
DetectionResult detect_game_platform(const ChdReader& reader,
                              DetectFlags flags = DetectFlags::All,
                              bool detect_title = false);

// Detect system from a raw input file (bin/iso/img/cue/gdi) BEFORE archiving.
// Returns system, format hint ("cd"/"dvd"/"raw"), and optional title.
DetectionResult detect_input(const std::string& input_path,
                             bool detect_title = false);

// Convert GamePlatform enum to a human-readable string
const char* game_platform_name(GamePlatform sys);

} // namespace chdlite

#endif // CHDLITE_DETECT_GAME_PLATFORM_HPP
