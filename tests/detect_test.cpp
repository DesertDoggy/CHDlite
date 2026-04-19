// CHDlite - detect_system test
// Opens each CHD, runs detect_system with all loops and backup-only, prints results.

#include "chd_reader.hpp"
#include "detect_game_platform.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    std::string rom_dir = "test_root/Roms/DiscRomsChd";
    std::string filter;
    if (argc >= 2) rom_dir = argv[1];
    if (argc >= 3) filter = argv[2];

    // Find all .chd files recursively
    std::vector<std::string> chd_files;
    for (const auto& entry : fs::recursive_directory_iterator(rom_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".chd") {
            if (!filter.empty() && entry.path().string().find(filter) == std::string::npos)
                continue;
            chd_files.push_back(entry.path().string());
        }
    }
    std::sort(chd_files.begin(), chd_files.end());
    std::printf("Found %zu CHD files.\n\n", chd_files.size());

    int pass = 0, fail = 0;

    for (const auto& path : chd_files) {
        std::string name = fs::path(path).filename().string();
        std::string parent = fs::path(path).parent_path().filename().string();

        chdlite::ChdReader reader;
        try {
            reader.open(path);
        } catch (const chdlite::ChdException& e) {
            std::printf("%-12s %-50s  OPEN FAILED: %s\n", parent.c_str(), name.c_str(), e.what());
            fail++;
            continue;
        }

        auto hdr = reader.read_header();
        const char* type_str = "???";
        switch (hdr.content_type) {
            case chdlite::ContentType::CDROM:     type_str = "CD-ROM"; break;
            case chdlite::ContentType::GDROM:     type_str = "GD-ROM"; break;
            case chdlite::ContentType::DVD:       type_str = "DVD";    break;
            case chdlite::ContentType::HardDisk:  type_str = "HD";     break;
            case chdlite::ContentType::LaserDisc: type_str = "LD";     break;
            case chdlite::ContentType::Raw:       type_str = "Raw";    break;
            default: break;
        }

        if (hdr.content_type != chdlite::ContentType::CDROM &&
            hdr.content_type != chdlite::ContentType::GDROM &&
            hdr.content_type != chdlite::ContentType::DVD) {
            std::printf("%-12s %-50s  %-6s  (skipped - not optical)\n",
                parent.c_str(), name.c_str(), type_str);
            reader.close();
            continue;
        }

        // Normal: all loops (with title)
        auto t0 = std::chrono::steady_clock::now();
        auto det_all = reader.detect_game_platform(chdlite::DetectFlags::All, true);
        auto t1 = std::chrono::steady_clock::now();
        double ms_all = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // Backup: skip sector 0 magic
        auto t2 = std::chrono::steady_clock::now();
        auto backup_flags = chdlite::DetectFlags::Iso9660 | chdlite::DetectFlags::Heuristic;
        auto det_backup = reader.detect_game_platform(backup_flags);
        auto t3 = std::chrono::steady_clock::now();
        double ms_backup = std::chrono::duration<double, std::milli>(t3 - t2).count();

        auto sys_all = det_all.game_platform;
        auto sys_backup = det_backup.game_platform;

        // Determine expected system from parent folder name
        std::string expected_str = parent;
        const char* note = "";

        // Compare all-loops result vs backup result
        if (sys_all != sys_backup) {
            // Sector-0-only platforms: backup legitimately returns GenericCD
            bool sector0_platform = (sys_all == chdlite::GamePlatform::ThreeDO ||
                                     sys_all == chdlite::GamePlatform::MegaCD ||
                                     sys_all == chdlite::GamePlatform::Saturn);
            if (sector0_platform && sys_backup == chdlite::GamePlatform::GenericCD)
                note = "  (backup->GenericCD expected)";
            else if (sys_all == chdlite::GamePlatform::Dreamcast)
                note = "  (GD-ROM always Dreamcast)";
            else
                note = "  ** UNEXPECTED DIFF **";
        }

        std::printf("%-12s %-50s  %-6s  all=%-16s (%.1fms)  backup=%-16s (%.1fms)%s\n",
            parent.c_str(), name.c_str(), type_str,
            chdlite::game_platform_name(sys_all), ms_all,
            chdlite::game_platform_name(sys_backup), ms_backup,
            note);

        if (!det_all.title.empty())
            std::printf("             Title: %s\n", det_all.title.c_str());
        if (!det_all.manufacturer_id.empty())
            std::printf("           Game ID: %s\n", det_all.manufacturer_id.c_str());

        pass++;
        reader.close();
    }

    std::printf("\n%d tested, %d failed to open.\n", pass, fail);
    return fail > 0 ? 1 : 0;
}
