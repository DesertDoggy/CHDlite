// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_archiver.hpp - Archive (create CHD) public API

#ifndef CHDLITE_CHD_ARCHIVER_HPP
#define CHDLITE_CHD_ARCHIVER_HPP

#include "chd_types.hpp"
#include <memory>
#include <string>

namespace chdlite {

class ChdArchiver
{
public:
    ChdArchiver();
    ~ChdArchiver();

    ChdArchiver(const ChdArchiver&) = delete;
    ChdArchiver& operator=(const ChdArchiver&) = delete;
    ChdArchiver(ChdArchiver&&) noexcept;
    ChdArchiver& operator=(ChdArchiver&&) noexcept;

    /// Archive a raw binary file (HD/DVD/raw) to CHD.
    ArchiveResult archive_raw(const std::string& input_path,
                              const std::string& output_path,
                              const ArchiveOptions& options = {});

    /// Archive a CD image (CUE/GDI/TOC/ISO/NRG) to CHD.
    ArchiveResult archive_cd(const std::string& input_path,
                             const std::string& output_path,
                             const ArchiveOptions& options = {});

    /// Archive a DVD ISO image to CHD.
    ArchiveResult archive_dvd(const std::string& input_path,
                              const std::string& output_path,
                              const ArchiveOptions& options = {});

    /// Auto-detect input format and archive to CHD.
    /// Routes to archive_raw, archive_cd, or archive_dvd based on file extension.
    ArchiveResult archive(const std::string& input_path,
                          const std::string& output_path,
                          const ArchiveOptions& options = {});

    /// Strict input validation (default: true).
    /// When enabled, a 2048-byte ISO that produces conflicting CD and DVD platform
    /// detections throws ChdFormatException instead of proceeding with a best-guess format.
    /// Disable to allow forced conversion of irregular or irregular dumps.
    void set_strict(bool v) noexcept { m_strict = v; }
    bool strict() const noexcept { return m_strict; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_strict = true;
};

} // namespace chdlite

#endif // CHDLITE_CHD_ARCHIVER_HPP
