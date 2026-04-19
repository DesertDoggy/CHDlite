// license:GPLv3
// CHDlite - shared frontend execution helpers for CLI/wrapper

#ifndef CHDLITE_CHD_FRONTEND_HPP
#define CHDLITE_CHD_FRONTEND_HPP

#include "chd_types.hpp"
#include "chd_archiver.hpp"
#include "chd_extractor.hpp"
#include "detect_game_platform.hpp"

#include <string>
#include <functional>

namespace chdlite {

struct FrontendCreateOptions {
    std::string input_path;
    std::string output_path;
    std::string input_parent;
    std::string compression_arg;
    uint32_t hunk_size = 0;
    uint32_t unit_size = 0;
    int num_processors = 0;
    bool best = false;
    bool chdman_compat = false;
    bool detect_title = true;
    std::function<bool(uint64_t, uint64_t)> progress_callback;
    std::function<void(LogLevel, const std::string&)> log_callback;
};

struct FrontendCreateResult {
    bool success = false;
    std::string error_message;
    ArchiveResult archive;
    std::string resolved_output;
    std::string detected_media;
    GamePlatform detected_platform = GamePlatform::Unknown;
    Codec selected_codecs[4] = { Codec::None, Codec::None, Codec::None, Codec::None };
};

struct FrontendExtractOptions {
    std::string input_path;
    std::string output_path;
    std::string input_parent;
    std::string output_bin;
    bool force_overwrite = false;
    bool split_bin = true;
    uint64_t input_start_byte = 0;
    uint64_t input_start_hunk = 0;
    uint64_t input_bytes = 0;
    uint64_t input_hunks = 0;
    uint64_t input_start_frame = 0;
    uint64_t input_frames = 0;
    HashFlags hash = HashFlags(0);
    CueStyle cue_style = CueStyle::Unmatched;
    std::function<bool(uint64_t, uint64_t)> progress_callback;
    std::function<void(LogLevel, const std::string&)> log_callback;
};

struct FrontendExtractResult {
    bool success = false;
    std::string error_message;
    ExtractionResult extract;
};

bool apply_cli_compression_arg(const std::string& compression_arg,
                               ArchiveOptions& opts,
                               std::string& err);

bool compute_auto_compression_plan(const std::string& input_path,
                                   std::string& out_media_format,
                                   GamePlatform& out_platform,
                                   Codec (&out_codecs)[4]);

FrontendCreateResult run_frontend_create(const FrontendCreateOptions& options);
FrontendExtractResult run_frontend_extract(const FrontendExtractOptions& options);

} // namespace chdlite

#endif // CHDLITE_CHD_FRONTEND_HPP
