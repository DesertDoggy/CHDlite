// Quick debug tool to inspect disc sectors for detection debugging
#include "chd_reader.hpp"
#include "detect_system.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void hex_dump(const uint8_t* data, uint32_t offset, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 16) {
        std::printf("  %04X: ", offset + i);
        for (uint32_t j = 0; j < 16 && i + j < len; j++)
            std::printf("%02X ", data[i + j]);
        for (uint32_t j = i + 16 > len ? 16 - (len - i) : 0; j > 0; j--)
            std::printf("   ");
        std::printf(" |");
        for (uint32_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = data[i + j];
            std::printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
        }
        std::printf("|\n");
    }
}

int main(int argc, char* argv[])
{
    std::string rom_dir = "test_root/Roms/DiscRomsChd";
    if (argc >= 2) rom_dir = argv[1];

    for (const auto& entry : fs::recursive_directory_iterator(rom_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".chd")
            continue;

        std::string path = entry.path().string();
        std::string name = entry.path().filename().string();
        std::string parent = entry.path().parent_path().filename().string();

        chdlite::ChdReader reader;
        try { reader.open(path); } catch (...) { continue; }

        auto hdr = reader.read_header();
        std::printf("========== %s / %s ==========\n", parent.c_str(), name.c_str());
        std::printf("Content: %d, Tracks: %zu\n",
            (int)hdr.content_type, hdr.tracks.size());

        // Build a sector reader from CHD
        auto content = reader.detect_content_type();
        auto sector_read = [&](uint32_t lba, uint8_t* buf) -> bool {
            try {
                if (content == chdlite::ContentType::CDROM || content == chdlite::ContentType::GDROM) {
                    return reader.read_sector(lba, buf, chdlite::TrackType::Mode1);
                } else {
                    auto data = reader.read_bytes(static_cast<uint64_t>(lba) * 2048, 2048);
                    if (data.size() == 2048) { std::memcpy(buf, data.data(), 2048); return true; }
                    return false;
                }
            } catch (...) { return false; }
        };

        // === PC Engine debug ===
        if (parent == "PCEngine" && hdr.tracks.size() >= 2) {
            std::printf("\n--- PC Engine Debug ---\n");
            for (size_t i = 0; i < hdr.tracks.size(); i++) {
                auto& t = hdr.tracks[i];
                std::printf("  Track %u: type=%d audio=%d start_lba=%u frames=%u data_size=%u\n",
                    t.track_number, (int)t.type, t.is_audio, t.start_lba, t.frames, t.data_size);
            }

            // Try reading sector at data track start + 1 (IPL block)
            for (size_t i = 0; i < hdr.tracks.size(); i++) {
                if (!hdr.tracks[i].is_audio) {
                    uint32_t data_lba = hdr.tracks[i].start_lba;
                    std::printf("  Data track %zu at LBA %u\n", i, data_lba);

                    // Try sector data_lba (sector 0 of data track)
                    uint8_t sec[2048];
                    std::printf("  Sector @ LBA %u (data track start):\n", data_lba);
                    if (sector_read(data_lba, sec))
                        hex_dump(sec, 0, 64);
                    else
                        std::printf("  FAILED to read LBA %u\n", data_lba);

                    // Try sector data_lba + 1 (IPL block)
                    std::printf("  Sector @ LBA %u (data track +1, IPL):\n", data_lba + 1);
                    if (sector_read(data_lba + 1, sec))
                        hex_dump(sec, 0, 64);
                    else
                        std::printf("  FAILED to read LBA %u\n", data_lba + 1);

                    // Also try absolute LBA 0 and 1
                    std::printf("  Sector @ LBA 0 (absolute):\n");
                    if (sector_read(0, sec))
                        hex_dump(sec, 0, 64);
                    else
                        std::printf("  FAILED to read LBA 0\n");

                    std::printf("  Sector @ LBA 1 (absolute):\n");
                    if (sector_read(1, sec))
                        hex_dump(sec, 0, 64);
                    else
                        std::printf("  FAILED to read LBA 1\n");

                    break;
                }
            }
        }

        // === Saturn debug: check if there's ISO 9660 PVD ===
        if (parent == "Saturn") {
            std::printf("\n--- Saturn Debug ---\n");
            uint8_t sec[2048];
            // Sector 0 header
            if (sector_read(0, sec)) {
                std::printf("  Sector 0:\n");
                hex_dump(sec, 0, 64);
            }
            // Check for PVD at LBA 16
            if (sector_read(16, sec)) {
                std::printf("  LBA 16 (PVD check): type=%02X magic=%.5s\n",
                    sec[0], &sec[1]);
                if (sec[0] == 0x01 && std::memcmp(&sec[1], "CD001", 5) == 0) {
                    char sysid[33] = {}, volid[33] = {};
                    std::memcpy(sysid, &sec[8], 32); sysid[32] = 0;
                    std::memcpy(volid, &sec[40], 32); volid[32] = 0;
                    std::printf("  PVD found! system_id='%s' volume_id='%s'\n", sysid, volid);
                } else {
                    std::printf("  No PVD at LBA 16\n");
                }
            }
        }

        // === PS1 debug: check volume ID and SYSTEM.CNF ===
        if (parent == "PS1" || parent == "PS2") {
            std::printf("\n--- %s Debug ---\n", parent.c_str());
            uint8_t sec[2048];
            if (sector_read(16, sec) && sec[0] == 0x01 && std::memcmp(&sec[1], "CD001", 5) == 0) {
                char sysid[33] = {}, volid[33] = {};
                std::memcpy(sysid, &sec[8], 32); sysid[32] = 0;
                std::memcpy(volid, &sec[40], 32); volid[32] = 0;
                std::printf("  PVD system_id='%s'\n  PVD volume_id='%s'\n", sysid, volid);

                // Also dump application_id (offset 574, 128 bytes)
                char appid[129] = {};
                std::memcpy(appid, &sec[574], 128); appid[128] = 0;
                std::printf("  PVD app_id='%.40s...'\n", appid);

                // Publisher (offset 318, 128 bytes)
                char pubid[129] = {};
                std::memcpy(pubid, &sec[318], 128); pubid[128] = 0;
                std::printf("  PVD publisher='%.40s...'\n", pubid);
            }
        }

        reader.close();
        std::printf("\n");
    }
    return 0;
}
