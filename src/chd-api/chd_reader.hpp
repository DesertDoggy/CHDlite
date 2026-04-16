// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_reader.hpp - Read and hash CHD files

#ifndef CHDLITE_CHD_READER_HPP
#define CHDLITE_CHD_READER_HPP

#include "chd_types.hpp"
#include "detect_game_platform.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chdlite {

class ChdReader
{
public:
    ChdReader();
    ~ChdReader();

    ChdReader(ChdReader&&) noexcept;
    ChdReader& operator=(ChdReader&&) noexcept;

    // Open/close
    void open(const std::string& path);
    void close();
    bool is_open() const;

    // Header info
    ChdHeader read_header() const;

    // Content type detection
    ContentType detect_content_type() const;

    // Raw data access
    std::vector<uint8_t> read_hunk(uint32_t hunk_num) const;
    std::vector<uint8_t> read_bytes(uint64_t offset, uint32_t length) const;

    // CD track access (only valid for CD/GD content)
    std::vector<TrackInfo> get_tracks() const;
    bool read_sector(uint32_t lba, void* buffer, TrackType type) const;

    // Platform/system detection (CD/GD/DVD content)
    DetectionResult detect_game_platform(DetectFlags flags = DetectFlags::All, bool detect_title = false) const
    {
        return chdlite::detect_game_platform(*this, flags, detect_title);
    }

    // Hash operations - compute from actual extracted content data
    // For CD/GD-ROM: hashes each track separately (matching chdman extractcd output)
    // For DVD/HD/raw: hashes the full extracted binary
    // flags selects which algorithms to compute (default: all)
    ContentHashResult hash_content(HashFlags flags = HashFlags::All) const;

    // Format hash result as text in the given output format
    static std::string format_hash(const ContentHashResult& result,
                                   HashOutputFormat format,
                                   const std::string& stem = "");

    // Fast hash from CHD header (no recalculation)
    std::string get_embedded_sha1() const;
    std::string get_embedded_raw_sha1() const;
    std::string get_embedded_parent_sha1() const;

    // Metadata access
    std::string read_metadata(uint32_t tag, uint32_t index = 0) const;

    // Static utility
    static bool is_chd_file(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chdlite

#endif // CHDLITE_CHD_READER_HPP
