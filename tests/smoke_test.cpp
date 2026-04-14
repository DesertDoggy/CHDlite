// license:GPLv3
// CHDlite - Smoke test
// Reads a real CHD file and prints header info to verify the API works end-to-end.

#include "chd_api.hpp"
#include <cstdio>
#include <cstdlib>

static const char* content_type_str(chdlite::ContentType ct)
{
    switch (ct) {
    case chdlite::ContentType::HardDisk:  return "HardDisk";
    case chdlite::ContentType::CDROM:     return "CD-ROM";
    case chdlite::ContentType::GDROM:     return "GD-ROM";
    case chdlite::ContentType::DVD:       return "DVD";
    case chdlite::ContentType::LaserDisc: return "LaserDisc";
    case chdlite::ContentType::Raw:       return "Raw";
    default:                              return "Unknown";
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <chd_file>\n", argv[0]);
        return 1;
    }

    const char* path = argv[1];

    // Test is_chd_file
    bool is_chd = chdlite::is_chd_file(path);
    std::printf("is_chd_file: %s\n", is_chd ? "true" : "false");
    if (!is_chd) {
        std::fprintf(stderr, "Not a CHD file.\n");
        return 1;
    }

    // Open and read header
    chdlite::ChdReader reader;
    try {
        reader.open(path);
    } catch (const chdlite::ChdException& e) {
        std::fprintf(stderr, "Failed to open: %s\n", e.what());
        return 1;
    }

    auto hdr = reader.read_header();

    std::printf("CHD Version:    %u\n", hdr.version);
    std::printf("Content Type:   %s\n", content_type_str(hdr.content_type));
    std::printf("Logical Bytes:  %llu\n", (unsigned long long)hdr.logical_bytes);
    std::printf("Hunk Bytes:     %u\n", hdr.hunk_bytes);
    std::printf("Hunk Count:     %u\n", hdr.hunk_count);
    std::printf("Unit Bytes:     %u\n", hdr.unit_bytes);
    std::printf("Compressed:     %s\n", hdr.compressed ? "yes" : "no");
    std::printf("Has Parent:     %s\n", hdr.has_parent ? "yes" : "no");
    std::printf("SHA-1:          %s\n", hdr.sha1.c_str());
    std::printf("Raw SHA-1:      %s\n", hdr.raw_sha1.c_str());

    if (hdr.content_type == chdlite::ContentType::CDROM || hdr.content_type == chdlite::ContentType::GDROM) {
        std::printf("Tracks:         %u\n", hdr.num_tracks);
        std::printf("Is GD-ROM:      %s\n", hdr.is_gdrom ? "yes" : "no");
        for (const auto& t : hdr.tracks) {
            std::printf("  Track %02u: frames=%u data_size=%u sub_size=%u audio=%s\n",
                t.track_number, t.frames, t.data_size, t.sub_size,
                t.is_audio ? "yes" : "no");
        }
    }

    std::printf("\nSmoke test PASSED.\n");
    return 0;
}
