// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_extractor.hpp - Extract CHD to disc/image formats

#ifndef CHDLITE_CHD_EXTRACTOR_HPP
#define CHDLITE_CHD_EXTRACTOR_HPP

#include "chd_types.hpp"

#include <memory>
#include <string>

namespace chdlite {

class ChdExtractor
{
public:
    ChdExtractor();
    ~ChdExtractor();

    ChdExtractor(ChdExtractor&&) noexcept;
    ChdExtractor& operator=(ChdExtractor&&) noexcept;

    /// Extract CHD to appropriate format based on content type.
    /// Output format is auto-detected from the output path extension:
    ///   .cue → CUE/BIN, .gdi → GDI, .iso → raw ISO, else → raw binary
    ExtractionResult extract(const std::string& chd_path,
                             const ExtractOptions& options = {});

    /// Extract with explicit parent CHD for delta-compressed files.
    ExtractionResult extract(const std::string& chd_path,
                             const std::string& parent_chd_path,
                             const ExtractOptions& options = {});

    /// Strict output validation (default: true).
    /// When enabled, failure to write the .cue/.gdi metadata file throws ChdMetadataException.
    /// When disabled, extraction succeeds with only the BIN data and a warning is logged.
    void set_strict(bool v) noexcept { m_strict = v; }
    bool strict() const noexcept { return m_strict; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_strict = true;

    ExtractionResult extract_impl(const std::string& chd_path,
                                  const std::string& parent_chd_path,
                                  const ExtractOptions& options);
};

} // namespace chdlite

#endif // CHDLITE_CHD_EXTRACTOR_HPP
