// license:GPLv3
// CHDlite - Cross-platform CHD library
// detect_system.cpp - Platform auto-detection implementation

#include "detect_game_platform.hpp"
#include "chd_reader.hpp"

#include "cdrom.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace chdlite {

namespace {

// ======================> ISO 9660 minimal parser

struct Iso9660DirEntry {
    std::string name;
    uint32_t extent_lba;
    uint32_t data_length;
    bool is_directory;
};

struct Iso9660Pvd {
    bool     valid = false;
    char     system_id[33] = {};
    char     volume_id[33] = {};
    uint32_t root_extent = 0;
    uint32_t root_length = 0;
};

static uint32_t read_le32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static uint16_t read_le16(const uint8_t* p)
{
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

// Read the Primary Volume Descriptor at LBA 16 (or base_lba + 16)
static Iso9660Pvd read_pvd(const SectorReader& read_sector, uint32_t base_lba = 0)
{
    Iso9660Pvd pvd;
    uint8_t sector[2048];

    if (!read_sector(base_lba + 16, sector))
        return pvd;

    // PVD: type 1, "CD001" at offset 1
    if (sector[0] != 0x01 || std::memcmp(&sector[1], "CD001", 5) != 0)
        return pvd;

    pvd.valid = true;
    std::memcpy(pvd.system_id, &sector[8], 32);
    pvd.system_id[32] = '\0';
    std::memcpy(pvd.volume_id, &sector[40], 32);
    pvd.volume_id[32] = '\0';

    // Root directory record at PVD offset 156
    const uint8_t* root = &sector[156];
    pvd.root_extent = read_le32(&root[2]);
    pvd.root_length = read_le32(&root[10]);

    return pvd;
}

// Parse directory entries from an ISO 9660 directory extent
static std::vector<Iso9660DirEntry> read_directory(const SectorReader& read_sector,
                                                    uint32_t extent_lba,
                                                    uint32_t length)
{
    std::vector<Iso9660DirEntry> entries;
    uint32_t sectors = (length + 2047) / 2048;

    for (uint32_t s = 0; s < sectors && s < 32; s++) {
        uint8_t sector[2048];
        if (!read_sector(extent_lba + s, sector))
            break;

        uint32_t offset = 0;
        while (offset + 33 < 2048) {
            uint8_t rec_len = sector[offset];
            if (rec_len < 33) break;
            if (offset + rec_len > 2048) break;

            const uint8_t* rec = &sector[offset];
            uint8_t name_len = rec[32];
            uint8_t flags    = rec[25];

            if (name_len > 1) {
                Iso9660DirEntry entry;
                entry.extent_lba  = read_le32(&rec[2]);
                entry.data_length = read_le32(&rec[10]);
                entry.is_directory = (flags & 0x02) != 0;

                std::string raw_name(reinterpret_cast<const char*>(&rec[33]), name_len);

                auto semi = raw_name.find(';');
                if (semi != std::string::npos)
                    raw_name.erase(semi);

                if (!raw_name.empty() && raw_name.back() == '.')
                    raw_name.pop_back();

                entry.name = raw_name;
                entries.push_back(entry);
            }

            offset += rec_len;
        }
    }

    return entries;
}

// Case-insensitive name match
static bool name_match(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Find a file/dir in a directory by name
static bool iso_find_in_dir(const SectorReader& read_sector,
                            uint32_t dir_extent, uint32_t dir_length,
                            const std::string& name, Iso9660DirEntry& result)
{
    auto entries = read_directory(read_sector, dir_extent, dir_length);
    for (const auto& e : entries) {
        if (name_match(e.name, name)) {
            result = e;
            return true;
        }
    }
    return false;
}

// Check if a path exists in the ISO filesystem
static bool iso_file_exists(const SectorReader& read_sector, const Iso9660Pvd& pvd,
                            const std::string& path)
{
    if (!pvd.valid) return false;

    uint32_t dir_extent = pvd.root_extent;
    uint32_t dir_length = pvd.root_length;

    std::string remaining = path;
    while (!remaining.empty()) {
        std::string component;
        auto slash = remaining.find('/');
        if (slash != std::string::npos) {
            component = remaining.substr(0, slash);
            remaining = remaining.substr(slash + 1);
        } else {
            component = remaining;
            remaining.clear();
        }

        Iso9660DirEntry entry;
        if (!iso_find_in_dir(read_sector, dir_extent, dir_length, component, entry))
            return false;

        if (remaining.empty())
            return true;

        if (!entry.is_directory)
            return false;

        dir_extent = entry.extent_lba;
        dir_length = entry.data_length;
    }

    return false;
}

// Read file content from ISO 9660 by path
static std::string iso_read_file(const SectorReader& read_sector, const Iso9660Pvd& pvd,
                                 const std::string& path, uint32_t max_bytes = 4096)
{
    if (!pvd.valid) return {};

    uint32_t dir_extent = pvd.root_extent;
    uint32_t dir_length = pvd.root_length;

    std::string remaining = path;
    while (!remaining.empty()) {
        std::string component;
        auto slash = remaining.find('/');
        if (slash != std::string::npos) {
            component = remaining.substr(0, slash);
            remaining = remaining.substr(slash + 1);
        } else {
            component = remaining;
            remaining.clear();
        }

        Iso9660DirEntry entry;
        if (!iso_find_in_dir(read_sector, dir_extent, dir_length, component, entry))
            return {};

        if (remaining.empty()) {
            if (entry.is_directory) return {};
            uint32_t to_read = std::min(entry.data_length, max_bytes);
            std::string content;
            content.reserve(to_read);

            uint32_t rem = to_read;
            uint32_t lba = entry.extent_lba;
            while (rem > 0) {
                uint8_t sector[2048];
                if (!read_sector(lba, sector))
                    break;
                uint32_t chunk = std::min(rem, uint32_t(2048));
                content.append(reinterpret_cast<const char*>(sector), chunk);
                rem -= chunk;
                lba++;
            }
            return content;
        }

        if (!entry.is_directory) return {};
        dir_extent = entry.extent_lba;
        dir_length = entry.data_length;
    }

    return {};
}

// ======================> Sector 0 magic checks

static GamePlatform check_3do(const uint8_t* sector0)
{
    if (sector0[0] == 0x01 &&
        sector0[1] == 0x5A && sector0[2] == 0x5A && sector0[3] == 0x5A &&
        sector0[4] == 0x5A && sector0[5] == 0x5A &&
        sector0[6] == 0x01)
        return GamePlatform::ThreeDO;
    return GamePlatform::Unknown;
}

static GamePlatform check_megacd(const uint8_t* sector0)
{
    if (std::memcmp(sector0, "SEGADISCSYSTEM", 14) == 0 ||
        std::memcmp(sector0, "SEGABOOTDISC",   12) == 0 ||
        std::memcmp(sector0, "SEGADISC",        8) == 0 ||
        std::memcmp(sector0, "SEGADATADISC",   12) == 0)
        return GamePlatform::MegaCD;
    return GamePlatform::Unknown;
}

static GamePlatform check_saturn(const uint8_t* sector0)
{
    if (std::memcmp(sector0, "SEGA SEGASATURN ", 16) == 0)
        return GamePlatform::Saturn;
    return GamePlatform::Unknown;
}

static GamePlatform check_dreamcast(const uint8_t* sector0)
{
    if (std::memcmp(sector0, "SEGA SEGAKATANA ", 16) == 0)
        return GamePlatform::Dreamcast;
    return GamePlatform::Unknown;
}

// ======================> ISO 9660 filesystem-based checks

static GamePlatform check_ps1_ps2(const SectorReader& read_sector, const Iso9660Pvd& pvd)
{
    std::string cnf = iso_read_file(read_sector, pvd, "SYSTEM.CNF");
    if (cnf.empty()) return GamePlatform::Unknown;

    if (cnf.find("BOOT2") != std::string::npos)
        return GamePlatform::PS2;
    if (cnf.find("BOOT") != std::string::npos)
        return GamePlatform::PS1;

    return GamePlatform::Unknown;
}

static GamePlatform check_psp(const SectorReader& read_sector, const Iso9660Pvd& pvd)
{
    if (iso_file_exists(read_sector, pvd, "PSP_GAME/PARAM.SFO"))
        return GamePlatform::PSP;
    return GamePlatform::Unknown;
}

static GamePlatform check_neogeocd(const SectorReader& read_sector, const Iso9660Pvd& pvd)
{
    if (iso_file_exists(read_sector, pvd, "IPL.TXT"))
        return GamePlatform::NeoGeoCD;
    return GamePlatform::Unknown;
}

static GamePlatform check_dvd_video(const SectorReader& read_sector, const Iso9660Pvd& pvd)
{
    if (iso_file_exists(read_sector, pvd, "VIDEO_TS/VIDEO_TS.IFO"))
        return GamePlatform::DVDISO;
    return GamePlatform::Unknown;
}

// ======================> PC Engine CD heuristic

static GamePlatform check_pcengine(const SectorReader& read_sector, const std::vector<TrackInfo>& tracks)
{
    // PC Engine CD: Track 1 = Audio, Track 2 = Data
    if (tracks.size() < 2)
        return GamePlatform::Unknown;
    if (!tracks[0].is_audio)
        return GamePlatform::Unknown;
    if (tracks[1].is_audio)
        return GamePlatform::Unknown;

    uint32_t data_lba = tracks[1].start_lba;
    uint8_t sector[2048];

    if (!read_sector(data_lba + 1, sector))
        return GamePlatform::Unknown;

    // Primary check: IPL header magic at byte 32
    // http://shu.sheldows.com/shu/download/pcedocs/pce_cdrom.html
    if (std::memcmp("PC Engine CD-ROM SYSTEM", &sector[32], 23) == 0)
        return GamePlatform::PCEngine;

    // Fallback heuristic: validate IPL header fields
    uint8_t  iplbln = sector[0x03];
    uint16_t iplsta = read_le16(&sector[0x04]);
    uint16_t ipljmp = read_le16(&sector[0x06]);

    if (iplbln == 0)
        return GamePlatform::Unknown;
    if (iplsta < 0x2000)
        return GamePlatform::Unknown;
    if (ipljmp == 0)
        return GamePlatform::Unknown;

    for (int i = 0; i < 5; i++) {
        if (sector[0x08 + i] > 0x7F)
            return GamePlatform::Unknown;
    }

    return GamePlatform::PCEngine;
}

// ======================> Title extraction

// Trim trailing whitespace/nulls
static std::string trim_right(const std::string& s)
{
    auto end = s.find_last_not_of(" \t\r\n\0");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

// Extract game title based on detected system
static std::string extract_title(const SectorReader& read_sector, GamePlatform system,
                                 const Iso9660Pvd& pvd, uint32_t data_lba = 0)
{
    switch (system) {
    case GamePlatform::PS1:
    case GamePlatform::PS2:
        // No title field on PS1/PS2 discs — SYSTEM.CNF only contains the boot executable ID
        break;
    case GamePlatform::PSP: {
        // PARAM.SFO is a binary key-value store
        std::string sfo = iso_read_file(read_sector, pvd, "PSP_GAME/PARAM.SFO", 16384);
        if (sfo.size() >= 20) {
            const auto* data = reinterpret_cast<const uint8_t*>(sfo.data());
            uint32_t sfo_size = static_cast<uint32_t>(sfo.size());
            // SFO header: magic 0x00505346, key_table_start, data_table_start, tables_entries
            if (sfo_size >= 20 && data[0] == 0x00 && data[1] == 0x50 &&
                data[2] == 0x53 && data[3] == 0x46) {
                uint32_t key_table  = read_le32(&data[8]);
                uint32_t data_table = read_le32(&data[12]);
                uint32_t num_entries = read_le32(&data[16]);
                // Each index entry is 16 bytes starting at offset 20
                for (uint32_t i = 0; i < num_entries && 20 + (i + 1) * 16 <= sfo_size; i++) {
                    const uint8_t* idx = &data[20 + i * 16];
                    uint16_t key_offset  = read_le16(&idx[0]);
                    uint32_t data_offset = read_le32(&idx[12]);
                    uint32_t key_pos = key_table + key_offset;
                    uint32_t data_pos = data_table + data_offset;
                    if (key_pos >= sfo_size || data_pos >= sfo_size) continue;
                    std::string key(reinterpret_cast<const char*>(&data[key_pos]));
                    if (key == "TITLE") {
                        std::string val(reinterpret_cast<const char*>(&data[data_pos]));
                        val = trim_right(val);
                        if (!val.empty()) return val;
                    }
                }
            }
        }
        break;
    }
    case GamePlatform::Saturn: {
        // Saturn header at sector 0: title at offset 0x60, 112 bytes (Shift-JIS)
        uint8_t sector0[2048];
        if (read_sector(0, sector0)) {
            std::string title(reinterpret_cast<const char*>(&sector0[0x60]), 112);
            title = trim_right(title);
            if (!title.empty())
                return title;
        }
        break;
    }
    case GamePlatform::MegaCD: {
        // Mega CD header at sector 0: domestic name at offset 0x120, 48 bytes
        uint8_t sector0[2048];
        if (read_sector(0, sector0)) {
            std::string title(reinterpret_cast<const char*>(&sector0[0x120]), 48);
            title = trim_right(title);
            if (!title.empty())
                return title;
        }
        break;
    }
    case GamePlatform::Dreamcast: {
        // Dreamcast IP.BIN: title at offset 0x80, 128 bytes
        uint8_t sector0[2048];
        if (read_sector(0, sector0)) {
            std::string title(reinterpret_cast<const char*>(&sector0[0x80]), 128);
            title = trim_right(title);
            if (!title.empty())
                return title;
        }
        break;
    }
    case GamePlatform::ThreeDO: {
        // 3DO Opera header: volume label at offset 0x28, 32 bytes
        uint8_t sector0[2048];
        if (read_sector(0, sector0)) {
            std::string title(reinterpret_cast<const char*>(&sector0[0x28]), 32);
            title = trim_right(title);
            if (!title.empty())
                return title;
        }
        break;
    }
    case GamePlatform::NeoGeoCD: {
        // IPL.TXT often has title info; fallback to volume ID
        std::string ipl = iso_read_file(read_sector, pvd, "IPL.TXT", 1024);
        if (!ipl.empty()) {
            // First non-empty line is often the title
            auto nl = ipl.find_first_of("\r\n");
            std::string first_line = (nl != std::string::npos) ? ipl.substr(0, nl) : ipl;
            first_line = trim_right(first_line);
            if (!first_line.empty())
                return first_line;
        }
        break;
    }
    case GamePlatform::PCEngine: {
        // PC Engine IPL header in sector data_lba+1:
        // "PC Engine CD-ROM SYSTEM" at byte 32, title at bytes 106-127 (22 bytes)
        // http://shu.sheldows.com/shu/download/pcedocs/pce_cdrom.html
        uint8_t sector1[2048];
        if (read_sector(data_lba + 1, sector1)) {
            if (std::memcmp("PC Engine CD-ROM SYSTEM", &sector1[32], 23) == 0) {
                std::string title(reinterpret_cast<const char*>(&sector1[106]), 22);
                title = trim_right(title);
                if (!title.empty()) return title;
            }
        }
        break;
    }
    default:
        break;
    }
    if (pvd.valid) {
        std::string vid = trim_right(std::string(pvd.volume_id));
        if (!vid.empty())
            return vid;
    }

    return {};
}

// Extract product/serial number (game ID) based on detected system
static std::string extract_manufacturer_id(const SectorReader& read_sector, GamePlatform system,
                                   const Iso9660Pvd& pvd, uint32_t data_lba = 0)
{
    switch (system) {
    case GamePlatform::PS1:
    case GamePlatform::PS2: {
        // Same as title — game ID from SYSTEM.CNF (e.g. SCPS_100.50, SLPM_655.55)
        std::string cnf = iso_read_file(read_sector, pvd, "SYSTEM.CNF");
        if (!cnf.empty()) {
            std::string boot_key = (system == GamePlatform::PS2) ? "BOOT2" : "BOOT";
            auto pos = cnf.find(boot_key);
            if (pos != std::string::npos) {
                auto bs = cnf.find('\\', pos);
                if (bs == std::string::npos) bs = cnf.find(':', pos);
                if (bs != std::string::npos) {
                    auto start = bs + 1;
                    auto semi = cnf.find(';', start);
                    auto nl = cnf.find_first_of("\r\n", start);
                    auto end = std::min(semi, nl);
                    if (end != std::string::npos && end > start)
                        return trim_right(cnf.substr(start, end - start));
                }
            }
        }
        break;
    }
    case GamePlatform::PSP: {
        // DISC_ID from PARAM.SFO (e.g. ULJM05325)
        std::string sfo = iso_read_file(read_sector, pvd, "PSP_GAME/PARAM.SFO", 16384);
        if (sfo.size() >= 20) {
            const auto* data = reinterpret_cast<const uint8_t*>(sfo.data());
            uint32_t sfo_size = static_cast<uint32_t>(sfo.size());
            if (sfo_size >= 20 && data[0] == 0x00 && data[1] == 0x50 &&
                data[2] == 0x53 && data[3] == 0x46) {
                uint32_t key_table  = read_le32(&data[8]);
                uint32_t data_table = read_le32(&data[12]);
                uint32_t num_entries = read_le32(&data[16]);
                for (uint32_t i = 0; i < num_entries && 20 + (i + 1) * 16 <= sfo_size; i++) {
                    const uint8_t* idx = &data[20 + i * 16];
                    uint16_t key_offset  = read_le16(&idx[0]);
                    uint32_t data_offset = read_le32(&idx[12]);
                    uint32_t key_pos = key_table + key_offset;
                    uint32_t data_pos = data_table + data_offset;
                    if (key_pos >= sfo_size || data_pos >= sfo_size) continue;
                    std::string key(reinterpret_cast<const char*>(&data[key_pos]));
                    if (key == "DISC_ID") {
                        std::string val(reinterpret_cast<const char*>(&data[data_pos]));
                        val = trim_right(val);
                        if (!val.empty()) return val;
                    }
                }
            }
        }
        break;
    }
    case GamePlatform::Saturn: {
        // Product number at sector 0 offset 0x20, 10 bytes
        uint8_t sector0[2048];
        if (read_sector(0, sector0)) {
            std::string id(reinterpret_cast<const char*>(&sector0[0x20]), 10);
            id = trim_right(id);
            if (!id.empty()) return id;
        }
        break;
    }
    case GamePlatform::Dreamcast: {
        // Product number at IP.BIN offset 0x40, 10 bytes
        uint8_t sector0[2048];
        if (read_sector(0, sector0)) {
            std::string id(reinterpret_cast<const char*>(&sector0[0x40]), 10);
            id = trim_right(id);
            if (!id.empty()) return id;
        }
        break;
    }
    case GamePlatform::MegaCD: {
        // Serial at sector 0 offset 0x183, 11 bytes (after "GM " at 0x180)
        uint8_t sector0[2048];
        if (read_sector(0, sector0)) {
            std::string id(reinterpret_cast<const char*>(&sector0[0x183]), 11);
            id = trim_right(id);
            if (!id.empty()) return id;
        }
        break;
    }
    default:
        break;
    }
    return {};
}

// Helper: fill both title and manufacturer_id in a DetectionResult
static void fill_metadata(const SectorReader& read_sector, GamePlatform system,
                          const Iso9660Pvd& pvd, DetectionResult& result, uint32_t data_lba = 0)
{
    result.title   = extract_title(read_sector, system, pvd, data_lba);
    result.manufacturer_id = extract_manufacturer_id(read_sector, system, pvd, data_lba);
}

// ======================> Raw file helpers

// CD-ROM sync pattern
static const uint8_t s_cd_sync[12] = {
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
};

static std::string path_ext_lower(const std::string& path)
{
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// Create a SectorReader that reads 2048-byte cooked sectors from a raw file.
// Handles raw (2352), mode2 (2336), and cooked (2048) sector sizes.
// For 2352-byte sectors, data starts at offset 16 (Mode1) or 24 (Mode2/XA).
static SectorReader make_file_reader(std::ifstream& file, uint32_t sector_size)
{
    return [&file, sector_size](uint32_t lba, uint8_t* buffer) -> bool {
        uint64_t offset = static_cast<uint64_t>(lba) * sector_size;
        file.seekg(static_cast<std::streamoff>(offset));
        if (!file.good()) return false;

        if (sector_size == 2048) {
            file.read(reinterpret_cast<char*>(buffer), 2048);
            return file.gcount() == 2048;
        }

        uint8_t raw[2352];
        uint32_t to_read = std::min(sector_size, uint32_t(2352));
        file.read(reinterpret_cast<char*>(raw), to_read);
        if (static_cast<uint32_t>(file.gcount()) != to_read) return false;

        if (sector_size == 2352) {
            // Check sync+header: Mode1 data at offset 16, Mode2/XA at offset 24
            if (raw[15] == 0x01) {
                // Mode 1
                std::memcpy(buffer, &raw[16], 2048);
            } else {
                // Mode 2 (form 1 or raw) — data at offset 24
                std::memcpy(buffer, &raw[24], 2048);
            }
        } else if (sector_size == 2336) {
            // Mode 2 without sync/header — first 8 bytes are subheader
            std::memcpy(buffer, &raw[8], 2048);
        } else {
            std::memcpy(buffer, raw, std::min(uint32_t(2048), to_read));
        }
        return true;
    };
}

// Detect sector size from file content and size
static uint32_t detect_sector_size(std::ifstream& file, uint64_t file_size)
{
    // Check for raw sector sync at offset 0
    uint8_t header[16] = {};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(header), sizeof(header));

    if (file.gcount() >= 12 && std::memcmp(header, s_cd_sync, 12) == 0) {
        // Verify sync at offset 2352
        if (file_size >= 2 * 2352) {
            file.seekg(2352);
            uint8_t sync2[12] = {};
            file.read(reinterpret_cast<char*>(sync2), 12);
            if (file.gcount() >= 12 && std::memcmp(sync2, s_cd_sync, 12) == 0)
                return 2352;
        }
        return 2352;
    }

    if (file_size % 2048 == 0) return 2048;
    if (file_size % 2336 == 0) return 2336;
    if (file_size % 2352 == 0) return 2352;
    return 2048;  // fallback
}

} // anonymous namespace

// ======================> Core detection (SectorReader-based)

DetectionResult detect_game_platform(const SectorReader& read_sector,
                              ContentType content_type,
                              const std::vector<TrackInfo>& tracks,
                              DetectFlags flags,
                              bool detect_title)
{
    DetectionResult result;

    // ===== GD-ROM path: always Dreamcast =====
    if (content_type == ContentType::GDROM) {
        result.game_platform   = GamePlatform::Dreamcast;
        result.platform_source = PlatformSource::Sector0;
        if (has_detect_flag(flags, DetectFlags::Sector0)) {
            // Verify with header check on last data track
            for (int i = static_cast<int>(tracks.size()) - 1; i >= 0; i--) {
                if (!tracks[i].is_audio) {
                    uint8_t sector[2048];
                    if (read_sector(tracks[i].start_lba, sector)) {
                        if (check_dreamcast(sector) != GamePlatform::Unknown) {
                            result.game_platform = GamePlatform::Dreamcast;
                        }
                    }
                    break;
                }
            }
        }
        if (detect_title) {
            Iso9660Pvd pvd;  // GD-ROM title from header, not ISO
            fill_metadata(read_sector, result.game_platform, pvd, result);
        }
        return result;
    }

    // ===== DVD path =====
    if (content_type == ContentType::DVD) {
        result.format = "dvd";
        if (has_detect_flag(flags, DetectFlags::Iso9660)) {
            Iso9660Pvd pvd = read_pvd(read_sector);
            if (pvd.valid) {
                GamePlatform sys;

                sys = check_psp(read_sector, pvd);
                if (sys != GamePlatform::Unknown) { result.game_platform = sys; result.platform_source = PlatformSource::Iso9660; if (detect_title) fill_metadata(read_sector, sys, pvd, result); return result; }

                sys = check_ps1_ps2(read_sector, pvd);
                if (sys != GamePlatform::Unknown) { result.game_platform = sys; result.platform_source = PlatformSource::Iso9660; if (detect_title) fill_metadata(read_sector, sys, pvd, result); return result; }

                sys = check_dvd_video(read_sector, pvd);
                if (sys != GamePlatform::Unknown) { result.game_platform = sys; result.platform_source = PlatformSource::Iso9660; if (detect_title) fill_metadata(read_sector, sys, pvd, result); return result; }

                // No system match but valid ISO — use volume ID as title
                if (detect_title)
                    fill_metadata(read_sector, GamePlatform::DVDISO, pvd, result);
            }
        }
        result.game_platform   = GamePlatform::DVDISO;
        result.platform_source = PlatformSource::Default;
        return result;
    }

    // ===== CD-ROM path =====
    if (content_type == ContentType::CDROM) {
        result.format = "cd";

        // Find first data track
        uint32_t data_lba = 0;
        bool has_data = false;
        for (const auto& t : tracks) {
            if (!t.is_audio) {
                data_lba = t.start_lba;
                has_data = true;
                break;
            }
        }

        if (has_data) {
            // --- Loop 1: Sector 0 magic (fastest) ---
            if (has_detect_flag(flags, DetectFlags::Sector0)) {
                uint8_t sector0[2048];
                if (read_sector(data_lba, sector0)) {
                    GamePlatform sys;

                    sys = check_3do(sector0);
                    if (sys != GamePlatform::Unknown) {
                        result.game_platform = sys; result.platform_source = PlatformSource::Sector0;
                        if (detect_title) { Iso9660Pvd pvd; fill_metadata(read_sector, sys, pvd, result); }
                        return result;
                    }

                    sys = check_megacd(sector0);
                    if (sys != GamePlatform::Unknown) {
                        result.game_platform = sys; result.platform_source = PlatformSource::Sector0;
                        if (detect_title) { Iso9660Pvd pvd; fill_metadata(read_sector, sys, pvd, result); }
                        return result;
                    }

                    sys = check_saturn(sector0);
                    if (sys != GamePlatform::Unknown) {
                        result.game_platform = sys; result.platform_source = PlatformSource::Sector0;
                        if (detect_title) { Iso9660Pvd pvd; fill_metadata(read_sector, sys, pvd, result); }
                        return result;
                    }

                    sys = check_dreamcast(sector0);
                    if (sys != GamePlatform::Unknown) {
                        result.game_platform = sys; result.platform_source = PlatformSource::Sector0;
                        if (detect_title) { Iso9660Pvd pvd; fill_metadata(read_sector, sys, pvd, result); }
                        return result;
                    }
                }
            }

            // --- Loop 2: ISO 9660 filesystem checks ---
            if (has_detect_flag(flags, DetectFlags::Iso9660)) {
                Iso9660Pvd pvd = read_pvd(read_sector);
                if (!pvd.valid && data_lba > 0)
                    pvd = read_pvd(read_sector, data_lba);

                if (pvd.valid) {
                    GamePlatform sys;

                    sys = check_ps1_ps2(read_sector, pvd);
                    if (sys != GamePlatform::Unknown) { result.game_platform = sys; result.platform_source = PlatformSource::Iso9660; if (detect_title) fill_metadata(read_sector, sys, pvd, result); return result; }

                    sys = check_neogeocd(read_sector, pvd);
                    if (sys != GamePlatform::Unknown) { result.game_platform = sys; result.platform_source = PlatformSource::Iso9660; if (detect_title) fill_metadata(read_sector, sys, pvd, result); return result; }

                    sys = check_psp(read_sector, pvd);
                    if (sys != GamePlatform::Unknown) { result.game_platform = sys; result.platform_source = PlatformSource::Iso9660; if (detect_title) fill_metadata(read_sector, sys, pvd, result); return result; }

                    sys = check_dvd_video(read_sector, pvd);
                    if (sys != GamePlatform::Unknown) { result.game_platform = sys; result.platform_source = PlatformSource::Iso9660; if (detect_title) fill_metadata(read_sector, sys, pvd, result); return result; }
                }
            }

            // --- Loop 3: PC Engine heuristic (after all other checks) ---
            if (has_detect_flag(flags, DetectFlags::Heuristic)) {
                GamePlatform sys = check_pcengine(read_sector, tracks);
                if (sys != GamePlatform::Unknown) {
                    result.game_platform   = sys;
                    result.platform_source = PlatformSource::Heuristic;
                    if (detect_title) {
                        Iso9660Pvd pvd = read_pvd(read_sector);
                        if (!pvd.valid && data_lba > 0)
                            pvd = read_pvd(read_sector, data_lba);
                        fill_metadata(read_sector, sys, pvd, result, data_lba);
                    }
                    return result;
                }
            }
        }

        result.game_platform   = GamePlatform::GenericCD;
        result.platform_source = PlatformSource::Default;
        if (detect_title) {
            Iso9660Pvd pvd = read_pvd(read_sector);
            if (!pvd.valid && data_lba > 0)
                pvd = read_pvd(read_sector, data_lba);
            fill_metadata(read_sector, GamePlatform::GenericCD, pvd, result);
        }
        return result;
    }

    return result;
}

// ======================> ChdReader convenience wrapper

DetectionResult detect_game_platform(const ChdReader& reader, DetectFlags flags, bool detect_title)
{
    ContentType content = reader.detect_content_type();

    // Build a SectorReader from the ChdReader
    SectorReader sector_reader = [&reader, content](uint32_t lba, uint8_t* buffer) -> bool {
        try {
            if (content == ContentType::CDROM || content == ContentType::GDROM) {
                return reader.read_sector(lba, buffer, TrackType::Mode1);
            } else {
                auto data = reader.read_bytes(static_cast<uint64_t>(lba) * 2048, 2048);
                if (data.size() == 2048) {
                    std::memcpy(buffer, data.data(), 2048);
                    return true;
                }
                return false;
            }
        } catch (...) {
            return false;
        }
    };

    // Get tracks for CD/GD content
    std::vector<TrackInfo> tracks;
    if (content == ContentType::CDROM || content == ContentType::GDROM) {
        try { tracks = reader.get_tracks(); } catch (...) {}
    }

    return detect_game_platform(sector_reader, content, tracks, flags, detect_title);
}

// ======================> Raw input file detection (pre-archive)

DetectionResult detect_input(const std::string& input_path, bool detect_title)
{
    DetectionResult result;
    std::string ext = path_ext_lower(input_path);

    // CUE/GDI/TOC/NRG: parse via MAME to get TOC + track files, then detect
    if (ext == ".cue" || ext == ".gdi" || ext == ".toc" || ext == ".nrg") {
        cdrom_file::toc toc = {};
        cdrom_file::track_input_info track_info;

        auto err = cdrom_file::parse_toc(input_path, toc, track_info);
        if (err) {
            result.format = "cd";
            return result;
        }

        // Compute logframeofs (same logic as MAME cdrom_file constructor)
        uint32_t physofs = 0, logofs = 0;
        for (uint32_t i = 0; i < toc.numtrks; i++) {
            auto& ti = toc.tracks[i];
            ti.logframeofs = 0;
            if (ti.pgdatasize == 0)
                logofs += ti.pregap;
            else
                ti.logframeofs = ti.pregap;
            ti.physframeofs = physofs;
            ti.chdframeofs = 0;
            ti.logframeofs += logofs;
            ti.logframes = ti.frames - ti.pregap;
            logofs += ti.postgap;
            physofs += ti.frames;
            logofs += ti.frames;
        }
        toc.tracks[toc.numtrks].logframeofs = logofs;
        toc.tracks[toc.numtrks].physframeofs = physofs;

        // Build TrackInfo vector
        std::vector<TrackInfo> tracks(toc.numtrks);
        for (uint32_t i = 0; i < toc.numtrks; i++) {
            auto& src = toc.tracks[i];
            auto& dst = tracks[i];
            dst.track_number = i + 1;
            dst.data_size    = src.datasize;
            dst.sub_size     = src.subsize;
            dst.frames       = src.frames;
            dst.pregap       = src.pregap;
            dst.postgap      = src.postgap;
            dst.session      = src.session;
            dst.start_lba    = src.logframeofs;
            dst.is_audio     = (src.trktype == cdrom_file::CD_TRACK_AUDIO);
            // Map track type
            switch (src.trktype) {
                case cdrom_file::CD_TRACK_MODE1:        dst.type = TrackType::Mode1; break;
                case cdrom_file::CD_TRACK_MODE1_RAW:    dst.type = TrackType::Mode1Raw; break;
                case cdrom_file::CD_TRACK_MODE2:        dst.type = TrackType::Mode2; break;
                case cdrom_file::CD_TRACK_MODE2_FORM1:  dst.type = TrackType::Mode2Form1; break;
                case cdrom_file::CD_TRACK_MODE2_FORM2:  dst.type = TrackType::Mode2Form2; break;
                case cdrom_file::CD_TRACK_MODE2_FORM_MIX: dst.type = TrackType::Mode2FormMix; break;
                case cdrom_file::CD_TRACK_MODE2_RAW:    dst.type = TrackType::Mode2Raw; break;
                case cdrom_file::CD_TRACK_AUDIO:        dst.type = TrackType::Audio; break;
                default: dst.type = TrackType::Mode1; break;
            }
        }

        bool is_gdrom = (toc.flags & (cdrom_file::CD_FLAG_GDROM | cdrom_file::CD_FLAG_GDROMLE)) != 0;
        ContentType ct = is_gdrom ? ContentType::GDROM : ContentType::CDROM;
        result.format = "cd";
        result.format_source = FormatSource::Extension;

        // Build per-track file readers. For each sector read, find the right track file.
        // Open the first data track file for sector reading.
        // For CUE/GDI, we need to open track files and read sectors.
        // Simple approach: open files on demand using track_info.
        struct TrackFile {
            std::string path;
            uint64_t offset;      // byte offset to start of INDEX 01 data in the file
            uint32_t sector_size;
            uint32_t start_frame;   // physical frame offset
            uint32_t num_frames;
        };
        std::vector<TrackFile> track_files(toc.numtrks);
        for (uint32_t i = 0; i < toc.numtrks; i++) {
            uint32_t sector_size = toc.tracks[i].datasize + toc.tracks[i].subsize;
            track_files[i].path = track_info.track[i].fname;
            // When pgdatasize != 0, the pregap is physically stored in the file before
            // INDEX 01. MAME returns offset=0 (file start), so we must skip past the pregap.
            uint64_t pregap_bytes = (toc.tracks[i].pgdatasize != 0)
                ? static_cast<uint64_t>(toc.tracks[i].pregap) * sector_size
                : 0;
            track_files[i].offset = track_info.track[i].offset + pregap_bytes;
            track_files[i].sector_size = sector_size;
            track_files[i].start_frame = toc.tracks[i].physframeofs;
            track_files[i].num_frames = toc.tracks[i].frames;
        }

        // SectorReader: find which track contains the LBA, open its file, read the sector
        std::ifstream current_file;
        std::string current_path;

        SectorReader sector_reader = [&](uint32_t lba, uint8_t* buffer) -> bool {
            // Find track containing this LBA
            int track_idx = -1;
            for (uint32_t i = 0; i < toc.numtrks; i++) {
                if (lba >= tracks[i].start_lba &&
                    (i + 1 >= toc.numtrks || lba < tracks[i + 1].start_lba)) {
                    track_idx = static_cast<int>(i);
                    break;
                }
            }
            if (track_idx < 0) return false;

            auto& tf = track_files[track_idx];
            if (tf.path.empty()) return false;

            // Open file if needed
            if (current_path != tf.path) {
                current_file.close();
                current_file.open(tf.path, std::ios::binary);
                if (!current_file.is_open()) return false;
                current_path = tf.path;
            }

            // Calculate offset within the file
            uint32_t frame_in_track = lba - tracks[track_idx].start_lba;
            uint64_t file_offset = tf.offset + static_cast<uint64_t>(frame_in_track) * tf.sector_size;

            current_file.seekg(static_cast<std::streamoff>(file_offset));
            if (!current_file.good()) return false;

            uint32_t data_size = toc.tracks[track_idx].datasize;
            if (data_size == 2048) {
                current_file.read(reinterpret_cast<char*>(buffer), 2048);
                return current_file.gcount() == 2048;
            }

            // Raw sector — extract cooked data
            uint8_t raw[2352];
            uint32_t to_read = std::min(data_size, uint32_t(2352));
            current_file.read(reinterpret_cast<char*>(raw), to_read);
            if (static_cast<uint32_t>(current_file.gcount()) != to_read) return false;

            if (data_size == 2352) {
                if (raw[15] == 0x01)
                    std::memcpy(buffer, &raw[16], 2048);
                else
                    std::memcpy(buffer, &raw[24], 2048);
            } else if (data_size == 2336) {
                std::memcpy(buffer, &raw[8], 2048);
            } else {
                std::memcpy(buffer, raw, std::min(uint32_t(2048), to_read));
            }
            return true;
        };

        auto det = detect_game_platform(sector_reader, ct, tracks, DetectFlags::All, detect_title);
        result.game_platform    = det.game_platform;
        result.title            = det.title;
        result.manufacturer_id  = det.manufacturer_id;
        result.platform_source  = det.platform_source;
        return result;
    }

    // .bin/.iso/.img — single file, detect sector size
    if (ext == ".iso" || ext == ".bin" || ext == ".img") {
        std::ifstream file(input_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            result.format = "raw";
            return result;
        }

        uint64_t file_size = static_cast<uint64_t>(file.tellg());
        file.seekg(0);

        if (file_size == 0) {
            result.format = "raw";
            return result;
        }

        uint32_t sector_size = detect_sector_size(file, file_size);

        // Determine if CD or DVD based on sector type and size
        bool has_sync = (sector_size == 2352 || sector_size == 2336);
        if (has_sync) {
            // Raw sectors → definitively CD (CD sync bytes present)
            result.format        = "cd";
            result.format_source = FormatSource::SyncBytes;

            // Build single-track info
            uint32_t num_frames = static_cast<uint32_t>(file_size / sector_size);
            TrackInfo ti = {};
            ti.track_number = 1;
            ti.type = (sector_size == 2352) ? TrackType::Mode2Raw : TrackType::Mode2;
            ti.data_size = sector_size;
            ti.frames = num_frames;
            ti.start_lba = 0;
            ti.is_audio = false;
            std::vector<TrackInfo> tracks = { ti };

            auto reader = make_file_reader(file, sector_size);
            auto det = detect_game_platform(reader, ContentType::CDROM, tracks, DetectFlags::All, detect_title);
            result.game_platform   = det.game_platform;
            result.title           = det.title;
            result.manufacturer_id = det.manufacturer_id;
            result.platform_source = det.platform_source;
        } else {
            // Cooked 2048-byte sectors — no sync bytes, so we can't tell from sector structure
            // alone whether this is a DVD ISO or a CD ripped without raw headers.
            // Run both detection paths and pick the more specific result; ties go to DVD.
            auto reader = make_file_reader(file, 2048);

            auto dvd_det = detect_game_platform(reader, ContentType::DVD, {}, DetectFlags::All, detect_title);

            uint32_t num_frames = static_cast<uint32_t>(file_size / 2048);
            TrackInfo ti = {};
            ti.track_number = 1;
            ti.type     = TrackType::Mode1;
            ti.data_size = 2048;
            ti.frames   = num_frames;
            ti.start_lba = 0;
            std::vector<TrackInfo> cd_tracks = { ti };
            auto cd_det = detect_game_platform(reader, ContentType::CDROM, cd_tracks, DetectFlags::All, detect_title);

            // A specific CD platform match overrides the generic DVD fallback.
            // If both (or neither) are specific, prefer DVD.
            bool dvd_specific = (dvd_det.game_platform != GamePlatform::DVDISO &&
                                 dvd_det.game_platform != GamePlatform::Unknown);
            bool cd_specific  = (cd_det.game_platform  != GamePlatform::GenericCD &&
                                 cd_det.game_platform  != GamePlatform::Unknown);

            if (cd_specific && dvd_specific) {
                // Both paths identified a specific platform — genuine ambiguity.
                result.format_conflict = true;
                result.format_conflict_detail =
                    std::string("CD path matched ") + game_platform_name(cd_det.game_platform) +
                    ", DVD path matched " + game_platform_name(dvd_det.game_platform);
            }

            if (cd_specific && !dvd_specific) {
                result.format          = "cd";
                result.format_source   = FormatSource::CdOverride;
                result.game_platform   = cd_det.game_platform;
                result.title           = cd_det.title;
                result.manufacturer_id = cd_det.manufacturer_id;
                result.platform_source = cd_det.platform_source;
            } else {
                result.format          = "dvd";
                result.format_source   = dvd_specific ? FormatSource::DvdMatch : FormatSource::DvdFallback;
                result.game_platform   = dvd_det.game_platform;
                result.title           = dvd_det.title;
                result.manufacturer_id = dvd_det.manufacturer_id;
                result.platform_source = dvd_det.platform_source;
            }
        }
        return result;
    }

    // Unknown extension → raw
    result.format = "raw";
    return result;
}

// ======================> Utility

const char* game_platform_name(GamePlatform sys)
{
    switch (sys) {
    case GamePlatform::PS1:       return "PlayStation";
    case GamePlatform::PS2:       return "PlayStation 2";
    case GamePlatform::PSP:       return "PSP";
    case GamePlatform::Saturn:    return "Sega Saturn";
    case GamePlatform::MegaCD:    return "Mega CD";
    case GamePlatform::PCEngine:  return "PC Engine CD";
    case GamePlatform::NeoGeoCD:  return "Neo Geo CD";
    case GamePlatform::ThreeDO:   return "3DO";
    case GamePlatform::Dreamcast: return "Dreamcast";
    case GamePlatform::DVDISO:    return "DVD";
    case GamePlatform::GenericCD: return "Generic CD";
    case GamePlatform::Unknown:   return "Unknown";
    }
    return "Unknown";
}

} // namespace chdlite
