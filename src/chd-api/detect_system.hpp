// license:GPLv3
// CHDlite - Cross-platform CHD library
// detect_system.hpp - Platform auto-detection for disc images

#ifndef CHDLITE_DETECT_SYSTEM_HPP
#define CHDLITE_DETECT_SYSTEM_HPP

#include "chd_types.hpp"

namespace chdlite {

class ChdReader;

// Detect the disc platform/system by examining sector content.
// Requires the CHD to be open and contain CD/GD/DVD content.
// Returns CdSystem::Unknown if detection fails or content is not optical media.
CdSystem detect_system(const ChdReader& reader);

// Convert CdSystem enum to a human-readable string
const char* system_name(CdSystem sys);

} // namespace chdlite

#endif // CHDLITE_DETECT_SYSTEM_HPP
