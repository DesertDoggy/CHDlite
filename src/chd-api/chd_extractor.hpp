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

/// Convert CUE sheet text to a different naming/format style.
/// Handles single-track filename differences and CATALOG header.
/// Input may use any line ending; output always uses CRLF.
std::string convert_cue_style(const std::string& cue_text, CueStyle to);

/// File-based CUE style conversion.
/// Reads input CUE, converts to the given style, writes to output path.
/// If output_path is empty, overwrites the input file.
void convert_cue_file(const std::string& input_path,
                      const std::string& output_path,
                      CueStyle to);

/// Try all CUE styles and return the one whose hash matches the database hash.
/// Converts cue_data to each style, hashes with the given algorithm, and
/// compares to db_hash (case-insensitive hex).  Returns CueStyle::Unmatched
/// if none match.
CueMatchResult match_cue(const std::string& cue_data,
                         HashAlgorithm hash_type,
                         const std::string& db_hash);

} // namespace chdlite

#endif // CHDLITE_CHD_EXTRACTOR_HPP
