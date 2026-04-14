// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_api.hpp - Aggregate public API header

#ifndef CHDLITE_CHD_API_HPP
#define CHDLITE_CHD_API_HPP

#include "chd_types.hpp"
#include "chd_reader.hpp"
#include "chd_processor.hpp"
#include "chd_extractor.hpp"
#include "chd_archiver.hpp"

namespace chdlite {

/// Library version info
inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 1;
inline constexpr int VERSION_PATCH = 0;

/// Quick check if a file is a valid CHD without opening a full reader.
inline bool is_chd_file(const std::string& path)
{
    return ChdReader::is_chd_file(path);
}

} // namespace chdlite

#endif // CHDLITE_CHD_API_HPP
