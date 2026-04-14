// license:BSD-3-Clause
// CHDlite - Cross-platform CHD library
// chd_processor.hpp - Unified content iteration engine with pluggable sinks
//
// The processor iterates over CHD content exactly once and feeds data to
// one or more sinks. This avoids duplicating the CD track iteration logic
// (physframeofs, splitframes, byte-swap) across hash and extract code paths.
//
// Sinks can be compiled independently — a hash-only binary need not link
// file-writing code, and vice versa.

#ifndef CHDLITE_CHD_PROCESSOR_HPP
#define CHDLITE_CHD_PROCESSOR_HPP

#include "chd_types.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chdlite {

// ======================> Sink interface

/// Attached to a ChdProcessor to receive extracted content data.
/// Implement only the callbacks you need.
class ProcessorSink
{
public:
    virtual ~ProcessorSink() = default;

    /// Called once before iteration begins.
    /// @param type     Detected content type.
    /// @param num_tracks  Number of tracks (1 for raw/DVD).
    virtual void on_begin(ContentType type, uint32_t num_tracks) {}

    /// Called at the start of each track (CD/GD-ROM) or once for raw content.
    /// For raw content: track_num=0, track_type=Mode1, data_size=hunk unit size.
    /// @param track_num   1-based track number (0 for raw/DVD whole-file).
    /// @param track_type  Track type enum.
    /// @param is_audio    True if audio track.
    /// @param data_size   Bytes per sector/chunk for this track.
    /// @param frames      TOC logical frame count for this track.
    virtual void on_track_begin(uint32_t track_num, TrackType track_type,
                                bool is_audio, uint32_t data_size,
                                uint32_t frames = 0) {}

    /// Called for each data chunk. For CD: one sector at a time.
    /// For raw: large buffer chunks. Data is already byte-swapped if needed.
    /// @param data  Pointer to extracted data.
    /// @param len   Number of bytes.
    virtual void on_data(const void* data, uint32_t len) = 0;

    /// Called at the end of each track.
    virtual void on_track_end() {}

    /// Called once after all iteration is complete.
    virtual void on_complete() {}
};

// ======================> Processor

/// Iterates CHD content and feeds data to attached sinks.
/// The iteration logic (CD track handling, splitframes, byte-swap,
/// raw hunk reading) lives here — nowhere else.
class ChdProcessor
{
public:
    /// Process a CHD file, feeding data to sinks.
    /// @param chd_path      Path to the CHD file.
    /// @param sinks         One or more sinks to receive data.
    /// @param progress      Optional progress callback: (done, total) → false to cancel.
    /// @return true on success.
    struct Result
    {
        bool        success = false;
        std::string error_message;
        ContentType content_type = ContentType::Unknown;
        uint64_t    total_bytes = 0;
    };

    static Result process(const std::string& chd_path,
                          std::vector<ProcessorSink*> sinks,
                          std::function<bool(uint64_t, uint64_t)> progress = nullptr);
};

} // namespace chdlite

#endif // CHDLITE_CHD_PROCESSOR_HPP
