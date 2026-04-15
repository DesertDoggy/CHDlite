// license:GPLv3
// CHDlite - Cross-platform CHD library
// detect_system.cpp - Platform auto-detection implementation

#include "detect_system.hpp"
#include "chd_reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
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

// Read a 2048-byte cooked sector; returns false on any error
static bool read_sector_safe(const ChdReader& reader, uint32_t lba, uint8_t* buffer)
{
    try {
        return reader.read_sector(lba, buffer, TrackType::Mode1);
    } catch (...) {
        return false;
    }
}

// Read the Primary Volume Descriptor at LBA 16 (or base_lba + 16)
static Iso9660Pvd read_pvd(const ChdReader& reader, uint32_t base_lba = 0)
{
    Iso9660Pvd pvd;
    uint8_t sector[2048];

    if (!read_sector_safe(reader, base_lba + 16, sector))
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
static std::vector<Iso9660DirEntry> read_directory(const ChdReader& reader,
                                                    uint32_t extent_lba,
                                                    uint32_t length)
{
    std::vector<Iso9660DirEntry> entries;
    uint32_t sectors = (length + 2047) / 2048;

    for (uint32_t s = 0; s < sectors && s < 32; s++) {
        uint8_t sector[2048];
        if (!read_sector_safe(reader, extent_lba + s, sector))
            break;

        uint32_t offset = 0;
        while (offset + 33 < 2048) {
            uint8_t rec_len = sector[offset];
            if (rec_len < 33) break;                // end of entries or corrupt
            if (offset + rec_len > 2048) break;

            const uint8_t* rec = &sector[offset];
            uint8_t name_len = rec[32];
            uint8_t flags    = rec[25];

            // Skip "." (name_len=1, name=0x00) and ".." (name_len=1, name=0x01)
            if (name_len > 1) {
                Iso9660DirEntry entry;
                entry.extent_lba  = read_le32(&rec[2]);
                entry.data_length = read_le32(&rec[10]);
                entry.is_directory = (flags & 0x02) != 0;

                std::string raw_name(reinterpret_cast<const char*>(&rec[33]), name_len);

                // Strip ";1" version suffix
                auto semi = raw_name.find(';');
                if (semi != std::string::npos)
                    raw_name.erase(semi);

                // Strip trailing dot (ISO 9660 quirk)
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
static bool iso_find_in_dir(const ChdReader& reader,
                            uint32_t dir_extent, uint32_t dir_length,
                            const std::string& name, Iso9660DirEntry& result)
{
    auto entries = read_directory(reader, dir_extent, dir_length);
    for (const auto& e : entries) {
        if (name_match(e.name, name)) {
            result = e;
            return true;
        }
    }
    return false;
}

// Check if a path exists in the ISO 9660 filesystem (e.g. "PSP_GAME/PARAM.SFO")
static bool iso_file_exists(const ChdReader& reader, const Iso9660Pvd& pvd,
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
        if (!iso_find_in_dir(reader, dir_extent, dir_length, component, entry))
            return false;

        if (remaining.empty())
            return true;   // found the final component

        if (!entry.is_directory)
            return false;

        dir_extent = entry.extent_lba;
        dir_length = entry.data_length;
    }

    return false;
}

// Read file content from ISO 9660 by path
static std::string iso_read_file(const ChdReader& reader, const Iso9660Pvd& pvd,
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
        if (!iso_find_in_dir(reader, dir_extent, dir_length, component, entry))
            return {};

        if (remaining.empty()) {
            // Final component — read file content
            if (entry.is_directory) return {};
            uint32_t to_read = std::min(entry.data_length, max_bytes);
            std::string content;
            content.reserve(to_read);

            uint32_t rem = to_read;
            uint32_t lba = entry.extent_lba;
            while (rem > 0) {
                uint8_t sector[2048];
                if (!read_sector_safe(reader, lba, sector))
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

static CdSystem check_3do(const uint8_t* sector0)
{
    // Opera filesystem header: 01 5A 5A 5A 5A 5A 01
    if (sector0[0] == 0x01 &&
        sector0[1] == 0x5A && sector0[2] == 0x5A && sector0[3] == 0x5A &&
        sector0[4] == 0x5A && sector0[5] == 0x5A &&
        sector0[6] == 0x01)
        return CdSystem::ThreeDO;
    return CdSystem::Unknown;
}

static CdSystem check_megacd(const uint8_t* sector0)
{
    if (std::memcmp(sector0, "SEGADISCSYSTEM", 14) == 0 ||
        std::memcmp(sector0, "SEGABOOTDISC",   12) == 0 ||
        std::memcmp(sector0, "SEGADISC",        8) == 0 ||
        std::memcmp(sector0, "SEGADATADISC",   12) == 0)
        return CdSystem::MegaCD;
    return CdSystem::Unknown;
}

static CdSystem check_saturn(const uint8_t* sector0)
{
    if (std::memcmp(sector0, "SEGA SEGASATURN ", 16) == 0)
        return CdSystem::Saturn;
    return CdSystem::Unknown;
}

static CdSystem check_dreamcast(const uint8_t* sector0)
{
    if (std::memcmp(sector0, "SEGA SEGAKATANA ", 16) == 0)
        return CdSystem::Dreamcast;
    return CdSystem::Unknown;
}

// ======================> ISO 9660 filesystem-based checks

static CdSystem check_ps1_ps2(const ChdReader& reader, const Iso9660Pvd& pvd)
{
    std::string cnf = iso_read_file(reader, pvd, "SYSTEM.CNF");
    if (cnf.empty()) return CdSystem::Unknown;

    // Check BOOT2 (PS2) before BOOT (PS1) to avoid false match
    if (cnf.find("BOOT2") != std::string::npos)
        return CdSystem::PS2;
    if (cnf.find("BOOT") != std::string::npos)
        return CdSystem::PS1;

    return CdSystem::Unknown;
}

static CdSystem check_psp(const ChdReader& reader, const Iso9660Pvd& pvd)
{
    if (iso_file_exists(reader, pvd, "PSP_GAME/PARAM.SFO"))
        return CdSystem::PSP;
    return CdSystem::Unknown;
}

static CdSystem check_neogeocd(const ChdReader& reader, const Iso9660Pvd& pvd)
{
    if (iso_file_exists(reader, pvd, "IPL.TXT"))
        return CdSystem::NeoGeoCD;
    return CdSystem::Unknown;
}

static CdSystem check_dvd_video(const ChdReader& reader, const Iso9660Pvd& pvd)
{
    if (iso_file_exists(reader, pvd, "VIDEO_TS/VIDEO_TS.IFO"))
        return CdSystem::DVDISO;
    return CdSystem::Unknown;
}

// ======================> PC Engine CD heuristic

static CdSystem check_pcengine(const ChdReader& reader, const std::vector<TrackInfo>& tracks)
{
    // PC Engine CD: Track 1 = Audio, Track 2 = Data
    if (tracks.size() < 2)
        return CdSystem::Unknown;
    if (!tracks[0].is_audio)
        return CdSystem::Unknown;
    if (tracks[1].is_audio)
        return CdSystem::Unknown;

    // Read IPL data block at sector 1 of the data track
    // IPL Information Data Block is at byte offset 0x800 from track start = sector 1
    uint32_t data_lba = tracks[1].start_lba;
    uint8_t sector[2048];

    if (!read_sector_safe(reader, data_lba + 1, sector))
        return CdSystem::Unknown;

    // IPL fields (offsets within sector):
    //   0x00-0x02: IPLBLK  (program sector address, 3 bytes)
    //   0x03:      IPLBLN  (sector count, 1-255)
    //   0x04-0x05: IPLSTA  (load address, LE, typically 0x4000)
    //   0x06-0x07: IPLJMP  (execute address, LE, typically 0x4000)
    //   0x08-0x0C: IPLMPR2-6 (bank mappings, 5 bytes)

    uint8_t  iplbln = sector[0x03];
    uint16_t iplsta = read_le16(&sector[0x04]);
    uint16_t ipljmp = read_le16(&sector[0x06]);

    // IPLBLN must be 1-255
    if (iplbln == 0)
        return CdSystem::Unknown;

    // IPLSTA typically 0x4000; valid range ~0x2000-0xFFFF
    if (iplsta < 0x2000)
        return CdSystem::Unknown;

    // IPLJMP must be non-zero
    if (ipljmp == 0)
        return CdSystem::Unknown;

    // Bank mappings (IPLMPR2-6) should be reasonable values (< 0x80)
    for (int i = 0; i < 5; i++) {
        if (sector[0x08 + i] > 0x7F)
            return CdSystem::Unknown;
    }

    return CdSystem::PCEngine;
}

} // anonymous namespace

// ======================> Public API

CdSystem detect_system(const ChdReader& reader)
{
    ContentType content = reader.detect_content_type();

    // ===== GD-ROM path: always Dreamcast =====
    if (content == ContentType::GDROM) {
        // Verify with header check on last data track
        try {
            auto tracks = reader.get_tracks();
            for (int i = static_cast<int>(tracks.size()) - 1; i >= 0; i--) {
                if (!tracks[i].is_audio) {
                    uint8_t sector[2048];
                    if (read_sector_safe(reader, tracks[i].start_lba, sector)) {
                        if (check_dreamcast(sector) != CdSystem::Unknown)
                            return CdSystem::Dreamcast;
                    }
                    break;
                }
            }
        } catch (...) {}
        // GD-ROM is Dreamcast by definition
        return CdSystem::Dreamcast;
    }

    // ===== DVD path =====
    if (content == ContentType::DVD) {
        Iso9660Pvd pvd = read_pvd(reader);
        if (pvd.valid) {
            CdSystem result;

            result = check_psp(reader, pvd);
            if (result != CdSystem::Unknown) return result;

            result = check_ps1_ps2(reader, pvd);
            if (result != CdSystem::Unknown) return result;

            result = check_dvd_video(reader, pvd);
            if (result != CdSystem::Unknown) return result;
        }
        return CdSystem::DVDISO;
    }

    // ===== CD-ROM path =====
    if (content == ContentType::CDROM) {
        std::vector<TrackInfo> tracks;
        try {
            tracks = reader.get_tracks();
        } catch (...) {
            return CdSystem::GenericCD;
        }

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
            uint8_t sector0[2048];
            if (read_sector_safe(reader, data_lba, sector0)) {
                CdSystem result;

                result = check_3do(sector0);
                if (result != CdSystem::Unknown) return result;

                result = check_megacd(sector0);
                if (result != CdSystem::Unknown) return result;

                result = check_saturn(sector0);
                if (result != CdSystem::Unknown) return result;

                result = check_dreamcast(sector0);
                if (result != CdSystem::Unknown) return result;
            }

            // --- Loop 2: ISO 9660 filesystem checks ---
            // Try PVD at LBA 16 first (standard for data-first discs)
            Iso9660Pvd pvd = read_pvd(reader);
            // Fallback: PVD relative to data track start (for audio-first discs)
            if (!pvd.valid && data_lba > 0)
                pvd = read_pvd(reader, data_lba);

            if (pvd.valid) {
                CdSystem result;

                result = check_ps1_ps2(reader, pvd);
                if (result != CdSystem::Unknown) return result;

                result = check_neogeocd(reader, pvd);
                if (result != CdSystem::Unknown) return result;

                result = check_psp(reader, pvd);
                if (result != CdSystem::Unknown) return result;

                result = check_dvd_video(reader, pvd);
                if (result != CdSystem::Unknown) return result;
            }

            // --- Loop 3: Heuristic checks ---
            {
                CdSystem result = check_pcengine(reader, tracks);
                if (result != CdSystem::Unknown) return result;
            }
        }

        return CdSystem::GenericCD;
    }

    return CdSystem::Unknown;
}

const char* system_name(CdSystem sys)
{
    switch (sys) {
    case CdSystem::PS1:       return "PlayStation";
    case CdSystem::PS2:       return "PlayStation 2";
    case CdSystem::PSP:       return "PSP";
    case CdSystem::Saturn:    return "Sega Saturn";
    case CdSystem::MegaCD:    return "Mega CD";
    case CdSystem::PCEngine:  return "PC Engine CD";
    case CdSystem::NeoGeoCD:  return "Neo Geo CD";
    case CdSystem::ThreeDO:   return "3DO";
    case CdSystem::Dreamcast: return "Dreamcast";
    case CdSystem::DVDISO:    return "DVD";
    case CdSystem::GenericCD: return "Generic CD";
    case CdSystem::Unknown:   return "Unknown";
    }
    return "Unknown";
}

} // namespace chdlite
