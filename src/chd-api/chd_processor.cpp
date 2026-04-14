// license:BSD-3-Clause
// CHDlite - Cross-platform CHD library
// chd_processor.cpp - Unified content iteration engine

#include "chd_processor.hpp"

#include "chd.h"
#include "cdrom.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace chdlite {

// ======================> Helpers (content type detection)

static ContentType detect_content_type(chd_file& chd)
{
    if (!chd.check_is_gd())   return ContentType::GDROM;
    if (!chd.check_is_cd())   return ContentType::CDROM;
    if (!chd.check_is_dvd())  return ContentType::DVD;
    if (!chd.check_is_av())   return ContentType::LaserDisc;
    if (!chd.check_is_hd())   return ContentType::HardDisk;
    return ContentType::Raw;
}

static TrackType mame_trktype_to_chdlite(uint32_t trktype)
{
    switch (trktype) {
    case cdrom_file::CD_TRACK_MODE1:          return TrackType::Mode1;
    case cdrom_file::CD_TRACK_MODE1_RAW:      return TrackType::Mode1Raw;
    case cdrom_file::CD_TRACK_MODE2:          return TrackType::Mode2;
    case cdrom_file::CD_TRACK_MODE2_FORM1:    return TrackType::Mode2Form1;
    case cdrom_file::CD_TRACK_MODE2_FORM2:    return TrackType::Mode2Form2;
    case cdrom_file::CD_TRACK_MODE2_FORM_MIX: return TrackType::Mode2FormMix;
    case cdrom_file::CD_TRACK_MODE2_RAW:      return TrackType::Mode2Raw;
    case cdrom_file::CD_TRACK_AUDIO:          return TrackType::Audio;
    default:                                  return TrackType::Mode1;
    }
}

// ======================> CD/GD-ROM iteration

static ChdProcessor::Result process_cd(chd_file& chd,
                                       std::vector<ProcessorSink*>& sinks,
                                       std::function<bool(uint64_t, uint64_t)>& progress)
{
    ChdProcessor::Result result;

    cdrom_file cd(&chd);
    const cdrom_file::toc& toc = cd.get_toc();
    bool is_gdrom = cd.is_gdrom();
    bool gdi_mode = is_gdrom;

    result.content_type = is_gdrom ? ContentType::GDROM : ContentType::CDROM;

    // Compute total frames for progress
    uint64_t total_frames = 0;
    for (uint32_t t = 0; t < toc.numtrks; t++)
        total_frames += toc.tracks[t].frames + toc.tracks[t].extraframes;

    for (auto* sink : sinks)
        sink->on_begin(result.content_type, toc.numtrks);

    uint64_t frames_done = 0;
    std::vector<uint8_t> sector_buf(cdrom_file::MAX_SECTOR_DATA);

    for (uint32_t t = 0; t < toc.numtrks; t++)
    {
        auto& track = toc.tracks[t];
        TrackType ttype = mame_trktype_to_chdlite(track.trktype);
        bool is_audio = (track.trktype == cdrom_file::CD_TRACK_AUDIO);

        for (auto* sink : sinks)
            sink->on_track_begin(t + 1, ttype, is_audio, track.datasize, track.frames,
                                 track.pregap, track.pgdatasize, track.postgap);

        // chdman's formula for actual output frames
        uint32_t actualframes = track.frames - track.padframes + track.splitframes;

        for (uint32_t f = 0; f < actualframes; f++)
        {
            // Handle splitframes: first splitframes of a track come from
            // the end of the previous track (GD-ROM)
            int trk;
            int frameofs;
            if (t > 0 && f < track.splitframes)
            {
                trk = t - 1;
                frameofs = toc.tracks[trk].frames - track.splitframes + f;
            }
            else
            {
                trk = t;
                frameofs = f - track.splitframes;
            }

            if (!cd.read_data(cd.get_track_start_phys(trk) + frameofs,
                              sector_buf.data(),
                              toc.tracks[trk].trktype, true))
            {
                std::memset(sector_buf.data(), 0, toc.tracks[trk].datasize);
            }

            // Byte-swap audio for CUE/GDI output (MAME stores big-endian)
            // For GDI mode with CHD version <= 4, audio is already little-endian
            bool swap_audio = (toc.tracks[trk].trktype == cdrom_file::CD_TRACK_AUDIO) &&
                              ((gdi_mode && chd.version() > 4) || !gdi_mode);
            if (swap_audio)
            {
                uint32_t dsz = toc.tracks[trk].datasize;
                for (uint32_t i = 0; i < dsz - 1; i += 2)
                    std::swap(sector_buf[i], sector_buf[i + 1]);
            }

            uint32_t data_len = toc.tracks[trk].datasize;
            for (auto* sink : sinks)
                sink->on_data(sector_buf.data(), data_len);

            result.total_bytes += data_len;
            frames_done++;

            if (progress && (frames_done % 1000 == 0))
            {
                if (!progress(frames_done, total_frames))
                {
                    result.error_message = "Processing cancelled";
                    return result;
                }
            }
        }

        for (auto* sink : sinks)
            sink->on_track_end();
    }

    for (auto* sink : sinks)
        sink->on_complete();

    result.success = true;
    return result;
}

// ======================> Raw/DVD/HD iteration

static ChdProcessor::Result process_raw(chd_file& chd,
                                        ContentType type,
                                        std::vector<ProcessorSink*>& sinks,
                                        std::function<bool(uint64_t, uint64_t)>& progress)
{
    ChdProcessor::Result result;
    result.content_type = type;

    const uint64_t total_bytes = chd.logical_bytes();
    const uint32_t hunk_bytes = chd.hunk_bytes();
    const uint32_t hunk_count = chd.hunk_count();

    constexpr uint32_t BUFFER_SIZE = 4 * 1024 * 1024;
    const uint32_t hunks_per_read = std::max(1u, BUFFER_SIZE / hunk_bytes);
    std::vector<uint8_t> buffer(hunks_per_read * hunk_bytes);

    for (auto* sink : sinks)
    {
        sink->on_begin(type, 1);
        sink->on_track_begin(0, TrackType::Mode1, false, hunk_bytes);
    }

    uint64_t bytes_done = 0;
    uint32_t hunk_num = 0;

    while (hunk_num < hunk_count)
    {
        uint32_t hunks_this = std::min(hunks_per_read, hunk_count - hunk_num);
        uint64_t bytes_to_read = std::min(
            uint64_t(hunks_this) * hunk_bytes,
            total_bytes - bytes_done);

        auto err = chd.read_bytes(bytes_done, buffer.data(), static_cast<uint32_t>(bytes_to_read));
        if (err)
        {
            result.error_message = "Read error at offset " + std::to_string(bytes_done);
            return result;
        }

        for (auto* sink : sinks)
            sink->on_data(buffer.data(), static_cast<uint32_t>(bytes_to_read));

        bytes_done += bytes_to_read;
        result.total_bytes = bytes_done;
        hunk_num += hunks_this;

        if (progress)
        {
            if (!progress(bytes_done, total_bytes))
            {
                result.error_message = "Processing cancelled";
                return result;
            }
        }
    }

    for (auto* sink : sinks)
    {
        sink->on_track_end();
        sink->on_complete();
    }

    result.success = true;
    return result;
}

// ======================> Public entry point

ChdProcessor::Result ChdProcessor::process(
    const std::string& chd_path,
    std::vector<ProcessorSink*> sinks,
    std::function<bool(uint64_t, uint64_t)> progress)
{
    Result result;

    chd_file chd;
    auto err = chd.open(chd_path, false);
    if (err)
    {
        result.error_message = "Failed to open CHD: " + err.message();
        return result;
    }

    ContentType type = detect_content_type(chd);
    result.content_type = type;

    if (type == ContentType::CDROM || type == ContentType::GDROM)
        return process_cd(chd, sinks, progress);
    else
        return process_raw(chd, type, sinks, progress);
}

} // namespace chdlite
