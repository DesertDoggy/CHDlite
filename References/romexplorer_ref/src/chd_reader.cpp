#include "chd_reader.h"
#include "logger.h"

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <algorithm>

#include <magic_enum/magic_enum.hpp>
#include <zlib.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

namespace romexplorer {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string bytes_to_hex(const uint8_t* bytes, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    return ss.str();
}

// Parse track metadata from an already-open CHD file.
// Tries CDROM_TRACK_METADATA2_TAG, then CDROM_TRACK_METADATA_TAG,
// then GDROM_TRACK_METADATA_TAG (Dreamcast).
static std::vector<ChdTrackInfo> parse_track_metadata(chd_file* chd) {
    std::vector<ChdTrackInfo> tracks;
    char metadata[512];
    uint32_t result_len, result_tag;
    uint8_t result_flags;

    // Try V2 format first (most common, has pregap info)
    for (uint32_t idx = 0; ; ++idx) {
        chd_error err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, idx,
                                          metadata, sizeof(metadata),
                                          &result_len, &result_tag, &result_flags);
        if (err != CHDERR_NONE) break;

        ChdTrackInfo t;
        char type_str[32] = {}, sub_str[32] = {}, pgtype[32] = {}, pgsub[32] = {};
        sscanf(metadata, CDROM_TRACK_METADATA2_FORMAT,
               &t.track_num, type_str, sub_str, &t.frames,
               &t.pregap, pgtype, pgsub, &t.postgap);
        t.type    = type_str;
        t.subtype = sub_str;
        t.pgtype  = pgtype;
        tracks.push_back(t);
    }

    if (!tracks.empty()) return tracks;

    // Fallback: V1 format (no pregap field)
    for (uint32_t idx = 0; ; ++idx) {
        chd_error err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, idx,
                                          metadata, sizeof(metadata),
                                          &result_len, &result_tag, &result_flags);
        if (err != CHDERR_NONE) break;

        ChdTrackInfo t;
        char type_str[32] = {}, sub_str[32] = {};
        sscanf(metadata, CDROM_TRACK_METADATA_FORMAT,
               &t.track_num, type_str, sub_str, &t.frames);
        t.type    = type_str;
        t.subtype = sub_str;
        tracks.push_back(t);
    }

    if (!tracks.empty()) return tracks;

    // Fallback: GD-ROM format (Dreamcast)
    for (uint32_t idx = 0; ; ++idx) {
        chd_error err = chd_get_metadata(chd, GDROM_TRACK_METADATA_TAG, idx,
                                          metadata, sizeof(metadata),
                                          &result_len, &result_tag, &result_flags);
        if (err != CHDERR_NONE) break;

        ChdTrackInfo t;
        char type_str[32] = {}, sub_str[32] = {}, pgtype[32] = {}, pgsub[32] = {};
        sscanf(metadata, GDROM_TRACK_METADATA_FORMAT,
               &t.track_num, type_str, sub_str, &t.frames,
               &t.pad, &t.pregap, pgtype, pgsub, &t.postgap);
        t.type    = type_str;
        t.subtype = sub_str;
        t.pgtype  = pgtype;
        tracks.push_back(t);
    }

    return tracks;
}

// ---------------------------------------------------------------------------
// read_chd_header
// ---------------------------------------------------------------------------

ChdHeaderInfo read_chd_header(const std::string& file_path) {
    LOG_DEBUG_STREAM("[CHD] read_chd_header() called: " << file_path);
    
    chd_header raw_hdr;
    chd_error err = chd_read_header(file_path.c_str(), &raw_hdr);
    if (err != CHDERR_NONE) {
        LOG_DEBUG_STREAM("[CHD] Failed to read CHD header: " << chd_error_string(err));
        throw std::runtime_error("Failed to read CHD header: " + std::string(chd_error_string(err)));
    }

    LOG_DEBUG_STREAM("[CHD] CHD version: " << raw_hdr.version);
    LOG_DEBUG_STREAM("[CHD] Logical size: " << raw_hdr.logicalbytes << " bytes");
    LOG_DEBUG_STREAM("[CHD] Hunk size: " << raw_hdr.hunkbytes << " bytes (" << raw_hdr.totalhunks << " hunks)");

    ChdHeaderInfo info;
    info.version       = raw_hdr.version;
    info.logical_bytes = raw_hdr.logicalbytes;
    info.hunk_bytes    = raw_hdr.hunkbytes;
    info.total_hunks   = raw_hdr.totalhunks;

    // SHA1 fields are present from V3+
    if (raw_hdr.version >= 3) {
        // sha1 = combined raw+meta SHA1 (what MAME_REDUMP DATs compare against)
        bool has_sha1 = false;
        for (int i = 0; i < CHD_SHA1_BYTES; ++i)
            if (raw_hdr.sha1[i]) { has_sha1 = true; break; }
        if (has_sha1)
            info.sha1_hex = bytes_to_hex(raw_hdr.sha1, CHD_SHA1_BYTES);

        // rawsha1 = data-only SHA1 (V4/V5)
        if (raw_hdr.version >= 4) {
            bool has_rawsha1 = false;
            for (int i = 0; i < CHD_SHA1_BYTES; ++i)
                if (raw_hdr.rawsha1[i]) { has_rawsha1 = true; break; }
            if (has_rawsha1)
                info.rawsha1_hex = bytes_to_hex(raw_hdr.rawsha1, CHD_SHA1_BYTES);
        }
    }

    // MD5 is present in V1/V2/V3
    if (raw_hdr.version <= 3) {
        bool has_md5 = false;
        for (int i = 0; i < CHD_MD5_BYTES; ++i)
            if (raw_hdr.md5[i]) { has_md5 = true; break; }
        if (has_md5)
            info.md5_hex = bytes_to_hex(raw_hdr.md5, CHD_MD5_BYTES);
    }

    // Compression type
    // V5: compression[0] holds the primary codec tag (4-byte tag packed into uint32_t).
    //     CD-specific codecs (CHD_CODEC_CD_*) are mapped to the underlying algorithm.
    // V1-V4: single uint32_t compression field; non-zero = zlib (only codec available then).
    if (raw_hdr.version >= 5) {
        switch (raw_hdr.compression[0]) {
            case 0:                  info.compression_type = ArchiveSubtype::CHD_UNCOMPRESSED; break;
            case CHD_CODEC_ZLIB:
            case CHD_CODEC_CD_ZLIB:  info.compression_type = ArchiveSubtype::CHD_ZLIB;         break;
            case CHD_CODEC_ZSTD:
            case CHD_CODEC_CD_ZSTD:  info.compression_type = ArchiveSubtype::CHD_ZSTD;         break;
            case CHD_CODEC_LZMA:
            case CHD_CODEC_CD_LZMA:  info.compression_type = ArchiveSubtype::CHD_LZMA;         break;
            case CHD_CODEC_FLAC:
            case CHD_CODEC_CD_FLAC:  info.compression_type = ArchiveSubtype::CHD_FLAC;         break;
            default:                 info.compression_type = ArchiveSubtype::OTHER;             break;
        }
    } else {
        // V1-V4 used a single compression field; non-zero = zlib, 0 = uncompressed
        info.compression_type = (raw_hdr.compression[0] != 0)
            ? ArchiveSubtype::CHD_ZLIB
            : ArchiveSubtype::CHD_UNCOMPRESSED;
    }

    LOG_DEBUG("[CHD] read_chd_header() completed successfully");
    return info;
}

// ---------------------------------------------------------------------------
// get_chd_tracks
// ---------------------------------------------------------------------------

std::vector<ChdTrackInfo> get_chd_tracks(const std::string& file_path) {
    chd_file* chd = nullptr;
    chd_error err = chd_open(file_path.c_str(), CHD_OPEN_READ, nullptr, &chd);
    if (err != CHDERR_NONE)
        throw std::runtime_error("chd_open failed: " + std::string(chd_error_string(err)));

    auto tracks = parse_track_metadata(chd);
    chd_close(chd);
    return tracks;
}

// ---------------------------------------------------------------------------
// hash_chd_tracks
// ---------------------------------------------------------------------------

std::vector<HashesResults> hash_chd_tracks(
    const std::string& file_path,
    const std::vector<HashAlgorithm>& hash_types)
{
    chd_file* chd = nullptr;
    chd_error err = chd_open(file_path.c_str(), CHD_OPEN_READ, nullptr, &chd);
    if (err != CHDERR_NONE)
        throw std::runtime_error("chd_open failed: " + std::string(chd_error_string(err)));

    const chd_header* hdr = chd_get_header(chd);
    uint32_t hunk_bytes = hdr->hunkbytes;

    // CD frame size in the hunk buffer:
    //   2352 bytes raw sector data + 96 bytes subcode = 2448 bytes
    // (WANT_RAW_DATA_SECTOR=1, WANT_SUBCODE=1 are the libchdr defaults)
    constexpr uint32_t SECTOR_DATA  = 2352;
    constexpr uint32_t SUBCODE_DATA = 96;
    constexpr uint32_t FRAME_SIZE   = SECTOR_DATA + SUBCODE_DATA; // 2448

    if (hunk_bytes < FRAME_SIZE) {
        chd_close(chd);
        throw std::runtime_error("CHD hunk size too small for CD data (not a disc image?)");
    }
    uint32_t sectors_per_hunk = hunk_bytes / FRAME_SIZE;

    auto tracks = parse_track_metadata(chd);
    if (tracks.empty()) {
        chd_close(chd);
        throw std::runtime_error("No CD track metadata found in CHD");
    }

    // Pre-compute algorithm flags
    std::vector<std::string_view> algo_names;
    for (const auto& algo : hash_types)
        algo_names.emplace_back(magic_enum::enum_name(algo));

    bool has_crc32  = std::find(algo_names.begin(), algo_names.end(), "CRC32")  != algo_names.end();
    bool has_md5    = std::find(algo_names.begin(), algo_names.end(), "MD5")    != algo_names.end();
    bool has_sha1   = std::find(algo_names.begin(), algo_names.end(), "SHA1")   != algo_names.end();
    bool has_sha256 = std::find(algo_names.begin(), algo_names.end(), "SHA256") != algo_names.end();

    // Calculate the CHD frame offset at which each track starts.
    // CHD stores frames + extraframes per track (no pregap, no pad in offsets).
    // extraframes = padding to 4-frame (TRACK_PADDING) boundary.
    std::vector<uint32_t> track_chd_offset(tracks.size());
    uint32_t cumulative = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        track_chd_offset[i] = cumulative;
        uint32_t extraframes = ((tracks[i].frames + 3) / 4) * 4 - tracks[i].frames;
        cumulative += tracks[i].frames + extraframes;
    }

    std::vector<uint8_t> hunk_buf(hunk_bytes);
    std::vector<HashesResults> results;

    // Temporary buffer for byte-swapping audio sectors
    std::vector<uint8_t> swap_buf(SECTOR_DATA);

    for (size_t ti = 0; ti < tracks.size(); ++ti) {
        const auto& track = tracks[ti];
        bool is_audio = (track.type == "AUDIO");

        // Frames to hash = track's data frames minus GD-ROM pad frames
        // For CD: pad=0, so this equals frames
        // For GD-ROM: pad represents gap between density areas (not data)
        uint32_t start_frame = track_chd_offset[ti];
        uint32_t num_frames  = track.frames - track.pad;
        uint32_t end_frame   = start_frame + num_frames; // exclusive

        uint32_t first_hunk = start_frame / sectors_per_hunk;
        uint32_t last_hunk  = (end_frame > 0) ? (end_frame - 1) / sectors_per_hunk : first_hunk;

        // Initialize hash contexts (only for requested algorithms)
        unsigned long crc_val = crc32(0L, Z_NULL, 0);

        EVP_MD_CTX* md5_ctx = has_md5 ? EVP_MD_CTX_new() : nullptr;
        if (md5_ctx) EVP_DigestInit_ex(md5_ctx, EVP_md5(), nullptr);

        EVP_MD_CTX* sha1_ctx = has_sha1 ? EVP_MD_CTX_new() : nullptr;
        if (sha1_ctx) EVP_DigestInit_ex(sha1_ctx, EVP_sha1(), nullptr);

        EVP_MD_CTX* sha256_ctx = has_sha256 ? EVP_MD_CTX_new() : nullptr;
        if (sha256_ctx) EVP_DigestInit_ex(sha256_ctx, EVP_sha256(), nullptr);

        // Read hunks and hash per-sector (skip subcode bytes)
        for (uint32_t h = first_hunk; h <= last_hunk; ++h) {
            err = chd_read(chd, h, hunk_buf.data());
            if (err != CHDERR_NONE) {
                if (md5_ctx)    EVP_MD_CTX_free(md5_ctx);
                if (sha1_ctx)   EVP_MD_CTX_free(sha1_ctx);
                if (sha256_ctx) EVP_MD_CTX_free(sha256_ctx);
                chd_close(chd);
                throw std::runtime_error("chd_read failed at hunk " + std::to_string(h)
                                         + ": " + std::string(chd_error_string(err)));
            }

            for (uint32_t s = 0; s < sectors_per_hunk; ++s) {
                uint32_t global_frame = h * sectors_per_hunk + s;
                if (global_frame < start_frame) continue;
                if (global_frame >= end_frame)  break;

                const uint8_t* sector = hunk_buf.data() + s * FRAME_SIZE;
                const uint8_t* hash_data = sector;

                // Audio tracks: byte-swap pairs to match chdman extraction output
                // CHD stores audio in big-endian (CD native), BIN files use little-endian
                if (is_audio) {
                    std::memcpy(swap_buf.data(), sector, SECTOR_DATA);
                    for (uint32_t j = 0; j < SECTOR_DATA; j += 2)
                        std::swap(swap_buf[j], swap_buf[j + 1]);
                    hash_data = swap_buf.data();
                }

                if (has_crc32)
                    crc_val = crc32(crc_val, hash_data, SECTOR_DATA);
                if (md5_ctx)
                    EVP_DigestUpdate(md5_ctx, hash_data, SECTOR_DATA);
                if (sha1_ctx)
                    EVP_DigestUpdate(sha1_ctx, hash_data, SECTOR_DATA);
                if (sha256_ctx)
                    EVP_DigestUpdate(sha256_ctx, hash_data, SECTOR_DATA);
            }
        }

        // Finalize
        HashesResults r;
        std::time_t finish_time = std::time(nullptr);

        if (has_crc32) {
            std::stringstream ss;
            ss << std::hex << std::setw(8) << std::setfill('0') << crc_val;
            r.crc32_hex  = ss.str();
            r.crc32_date = finish_time;
        }
        if (md5_ctx) {
            unsigned char digest[MD5_DIGEST_LENGTH];
            unsigned int len = 0;
            EVP_DigestFinal_ex(md5_ctx, digest, &len);
            r.md5_hex  = bytes_to_hex(digest, len);
            r.md5_date = finish_time;
            EVP_MD_CTX_free(md5_ctx);
        }
        if (sha1_ctx) {
            unsigned char digest[SHA_DIGEST_LENGTH];
            unsigned int len = 0;
            EVP_DigestFinal_ex(sha1_ctx, digest, &len);
            r.sha1_hex  = bytes_to_hex(digest, len);
            r.sha1_date = finish_time;
            EVP_MD_CTX_free(sha1_ctx);
        }
        if (sha256_ctx) {
            unsigned char digest[SHA256_DIGEST_LENGTH];
            unsigned int len = 0;
            EVP_DigestFinal_ex(sha256_ctx, digest, &len);
            r.sha256_hex  = bytes_to_hex(digest, len);
            r.sha256_date = finish_time;
            EVP_MD_CTX_free(sha256_ctx);
        }

        results.push_back(r);
    }

    chd_close(chd);
    return results;
}

// ---------------------------------------------------------------------------
// CUE reconstruction helpers
// ---------------------------------------------------------------------------

// Maps CHD track type strings to CUE track type strings
static std::string chd_type_to_cue_type(const std::string& chd_type) {
    if (chd_type == "MODE2_RAW") return "MODE2/2352";
    if (chd_type == "MODE1_RAW") return "MODE1/2352";
    if (chd_type == "AUDIO")     return "AUDIO";
    if (chd_type == "MODE2")     return "MODE2/2048";
    if (chd_type == "MODE1")     return "MODE1/2048";
    // Already in CUE format (e.g. "MODE2/2352")
    if (chd_type.find('/') != std::string::npos) return chd_type;
    return chd_type;
}

static std::string frames_to_msf(uint32_t frame) {
    uint32_t mm = frame / (60 * 75);
    uint32_t ss = (frame / 75) % 60;
    uint32_t ff = frame % 75;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", mm, ss, ff);
    return buf;
}

// ---------------------------------------------------------------------------
// reconstruct_cue_content  (split-bin: one FILE per track)
// ---------------------------------------------------------------------------

std::string reconstruct_cue_content(const std::string& file_path, const std::string& base_name) {
    auto tracks = get_chd_tracks(file_path);

    // Determine track-number width for zero-padding (match max track number width)
    int width = 1;
    if (!tracks.empty()) {
        uint32_t max_num = tracks.back().track_num;
        width = static_cast<int>(std::to_string(max_num).length());
    }

    std::string cue;

    for (const auto& t : tracks) {
        // Track filename: "base_name (Track N).bin"
        char track_suffix[64];
        snprintf(track_suffix, sizeof(track_suffix), " (Track %0*u).bin", width, t.track_num);
        std::string track_bin = base_name + track_suffix;

        cue += "FILE \"" + track_bin + "\" BINARY\r\n";

        char track_num[4];
        snprintf(track_num, sizeof(track_num), "%02u", t.track_num);

        cue += "  TRACK ";
        cue += track_num;
        cue += " ";
        cue += chd_type_to_cue_type(t.type);
        cue += "\r\n";

        bool has_pregap_data = (t.pregap > 0 && !t.pgtype.empty() && t.pgtype[0] == 'V');

        if (t.pregap > 0 && has_pregap_data) {
            // Pregap data IS in the BIN file: INDEX 00 at start, INDEX 01 at pregap offset
            cue += "    INDEX 00 " + frames_to_msf(0) + "\r\n";
            cue += "    INDEX 01 " + frames_to_msf(t.pregap) + "\r\n";
        } else if (t.pregap > 0) {
            // Virtual pregap (not in BIN): PREGAP command + INDEX 01 at start
            cue += "    PREGAP " + frames_to_msf(t.pregap) + "\r\n";
            cue += "    INDEX 01 " + frames_to_msf(0) + "\r\n";
        } else {
            cue += "    INDEX 01 " + frames_to_msf(0) + "\r\n";
        }
    }

    return cue;
}

// ---------------------------------------------------------------------------
// hash_chd_cue
// ---------------------------------------------------------------------------

HashesResults hash_chd_cue(
    const std::string& file_path,
    const std::string& base_name,
    const std::vector<HashAlgorithm>& hash_types)
{
    std::string cue_content = reconstruct_cue_content(file_path, base_name);
    const auto* data = reinterpret_cast<const uint8_t*>(cue_content.data());
    size_t size = cue_content.size();

    std::vector<std::string_view> algo_names;
    for (const auto& algo : hash_types)
        algo_names.emplace_back(magic_enum::enum_name(algo));

    bool has_crc32  = std::find(algo_names.begin(), algo_names.end(), "CRC32")  != algo_names.end();
    bool has_md5    = std::find(algo_names.begin(), algo_names.end(), "MD5")    != algo_names.end();
    bool has_sha1   = std::find(algo_names.begin(), algo_names.end(), "SHA1")   != algo_names.end();
    bool has_sha256 = std::find(algo_names.begin(), algo_names.end(), "SHA256") != algo_names.end();

    HashesResults r;
    std::time_t finish_time = std::time(nullptr);

    if (has_crc32) {
        unsigned long val = crc32(0L, Z_NULL, 0);
        val = crc32(val, data, static_cast<uInt>(size));
        std::stringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << val;
        r.crc32_hex  = ss.str();
        r.crc32_date = finish_time;
    }

    auto evp_hash = [&](const EVP_MD* md, std::string& out_hex, std::time_t& out_date) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, md, nullptr);
        EVP_DigestUpdate(ctx, data, size);
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        EVP_DigestFinal_ex(ctx, digest, &len);
        EVP_MD_CTX_free(ctx);
        out_hex  = bytes_to_hex(digest, len);
        out_date = finish_time;
    };

    if (has_md5)    evp_hash(EVP_md5(),    r.md5_hex,    r.md5_date);
    if (has_sha1)   evp_hash(EVP_sha1(),   r.sha1_hex,   r.sha1_date);
    if (has_sha256) evp_hash(EVP_sha256(), r.sha256_hex, r.sha256_date);

    return r;
}

// ---------------------------------------------------------------------------
// GDI reconstruction helpers
// ⚠️  PROTOTYPE: Limited testing on Dreamcast samples. Compatibility may vary.
// ---------------------------------------------------------------------------

std::string reconstruct_gdi_content(const std::string& file_path, const std::string& base_name_param) {
    auto tracks = get_chd_tracks(file_path);
    if (tracks.empty())
        throw std::runtime_error("no tracks found in CHD: " + file_path);

    // Derive base_name from CHD filename if not provided
    std::string base_name = base_name_param.empty()
        ? std::filesystem::path(file_path).stem().string()
        : base_name_param;

    // LBA start for each track: cumulative frames (no pregap, no pad)
    //   lba[0] = 0
    //   lba[i] = lba[i-1] + tracks[i-1].frames
    std::vector<uint32_t> lba(tracks.size(), 0);
    for (size_t i = 1; i < tracks.size(); ++i)
        lba[i] = lba[i - 1] + tracks[i - 1].frames;

    std::string gdi;
    gdi += std::to_string(tracks.size()) + "\r\n";

    for (size_t i = 0; i < tracks.size(); ++i) {
        bool audio = (tracks[i].type == "AUDIO");
        int  type_flag = audio ? 0 : 4;
        const char* ext = audio ? ".raw" : ".bin";

        // chdman GDI naming: "basename%02d.ext" (no space before number)
        char fname[512];
        snprintf(fname, sizeof(fname), "%s%02u%s", base_name.c_str(), tracks[i].track_num, ext);

        gdi += std::to_string(tracks[i].track_num) + " ";
        gdi += std::to_string(lba[i]) + " ";
        gdi += std::to_string(type_flag) + " ";
        gdi += "2352 ";
        // Quote filenames (always, for consistency with chdman)
        gdi += "\"";
        gdi += fname;
        gdi += "\"";
        gdi += " 0\r\n";
    }

    return gdi;
}

// ---------------------------------------------------------------------------
// hash_chd_gdi
// ⚠️  PROTOTYPE: GDI support is under development.
// ---------------------------------------------------------------------------

HashesResults hash_chd_gdi(
    const std::string& file_path,
    const std::vector<HashAlgorithm>& hash_types)
{
    std::string gdi_content = reconstruct_gdi_content(file_path);
    const auto* data = reinterpret_cast<const uint8_t*>(gdi_content.data());
    size_t size = gdi_content.size();

    std::vector<std::string_view> algo_names;
    for (const auto& algo : hash_types)
        algo_names.emplace_back(magic_enum::enum_name(algo));

    bool has_crc32  = std::find(algo_names.begin(), algo_names.end(), "CRC32")  != algo_names.end();
    bool has_md5    = std::find(algo_names.begin(), algo_names.end(), "MD5")    != algo_names.end();
    bool has_sha1   = std::find(algo_names.begin(), algo_names.end(), "SHA1")   != algo_names.end();
    bool has_sha256 = std::find(algo_names.begin(), algo_names.end(), "SHA256") != algo_names.end();

    HashesResults r;
    std::time_t finish_time = std::time(nullptr);

    if (has_crc32) {
        unsigned long val = crc32(0L, Z_NULL, 0);
        val = crc32(val, data, static_cast<uInt>(size));
        std::stringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << val;
        r.crc32_hex  = ss.str();
        r.crc32_date = finish_time;
    }

    auto evp_hash = [&](const EVP_MD* md, std::string& out_hex, std::time_t& out_date) {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, md, nullptr);
        EVP_DigestUpdate(ctx, data, size);
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        EVP_DigestFinal_ex(ctx, digest, &len);
        EVP_MD_CTX_free(ctx);
        out_hex  = bytes_to_hex(digest, len);
        out_date = finish_time;
    };

    if (has_md5)    evp_hash(EVP_md5(),    r.md5_hex,    r.md5_date);
    if (has_sha1)   evp_hash(EVP_sha1(),   r.sha1_hex,   r.sha1_date);
    if (has_sha256) evp_hash(EVP_sha256(), r.sha256_hex, r.sha256_date);

    return r;
}

// ---------------------------------------------------------------------------
// extract_chd
// ---------------------------------------------------------------------------

std::expected<std::vector<std::string>, ScanError>
extract_chd(const std::string& chd_path, const std::string& output_dir, const std::string& base_name_param) {
    namespace fs = std::filesystem;
    LOG_DEBUG_STREAM("[CHD] extract_chd() called: chd=" << chd_path << " output=" << output_dir << " base_name=" << base_name_param);

    if (!fs::exists(chd_path)) {
        LOG_DEBUG("[CHD] CHD file not found");
        return std::unexpected(ScanError::FILE_NOT_FOUND);
    }

    chd_file* chd = nullptr;
    if (chd_open(chd_path.c_str(), CHD_OPEN_READ, nullptr, &chd) != CHDERR_NONE) {
        LOG_DEBUG("[CHD] Failed to open CHD file");
        return std::unexpected(ScanError::CORRUPT_ARCHIVE);
    }

    const chd_header* hdr       = chd_get_header(chd);
    const uint32_t hunk_bytes   = hdr->hunkbytes;
    const uint32_t total_hunks  = hdr->totalhunks;
    LOG_DEBUG_STREAM("[CHD] CHD opened: " << total_hunks << " hunks x " << hunk_bytes << " bytes");

    // Use provided base_name or derive from CHD filename
    std::string base_name = base_name_param.empty() 
        ? fs::path(chd_path).stem().string() 
        : base_name_param;
    fs::create_directories(output_dir);

    // parse_track_metadata uses the already-open handle (avoids re-opening).
    auto tracks = parse_track_metadata(chd);

    std::vector<uint8_t> hunk_buf(hunk_bytes);
    std::vector<std::string> result;

    // ---- Non-CD image: DVD ISO, hard-disk, or generic binary ---------------
    if (tracks.empty()) {
        // Probe for DVD metadata (PS2, Xbox, Xbox 360, Wii, etc.)
        bool is_dvd = false;
        {
            char dummy[16];
            uint32_t rlen, rtag; uint8_t rflags;
            is_dvd = (chd_get_metadata(chd, DVD_METADATA_TAG, 0,
                                       dummy, sizeof(dummy),
                                       &rlen, &rtag, &rflags) == CHDERR_NONE);
        }

        // Probe for hard-disk metadata (arcade drives, etc.)
        int hd_cyls = 0, hd_heads = 0, hd_secs = 0, hd_bps = 0;
        bool is_hdd = false;
        if (!is_dvd) {
            char meta[256];
            uint32_t rlen, rtag; uint8_t rflags;
            if (chd_get_metadata(chd, HARD_DISK_METADATA_TAG, 0,
                                 meta, sizeof(meta),
                                 &rlen, &rtag, &rflags) == CHDERR_NONE) {
                sscanf(meta, HARD_DISK_METADATA_FORMAT,
                       &hd_cyls, &hd_heads, &hd_secs, &hd_bps);
                is_hdd = (hd_bps > 0);
            }
        }

        // For DVD/ISO: sectors are exactly 2048 bytes (hunk is a multiple of 2048).
        // Write logical_bytes worth of data stripped to sector boundaries.
        std::string out_ext = is_dvd ? ".iso" : (is_hdd ? ".img" : ".bin");
        uint32_t sector_size = is_hdd && hd_bps > 0
                                ? static_cast<uint32_t>(hd_bps)
                                : (is_dvd ? 2048u : hunk_bytes);

        fs::path out_path = fs::path(output_dir) / (base_name + out_ext);
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            chd_close(chd);
            return std::unexpected(ScanError::READ_ERROR);
        }

        uint64_t logical_bytes = hdr->logicalbytes;
        uint64_t bytes_written = 0;

        for (uint32_t i = 0; i < total_hunks; ++i) {
            if (chd_read(chd, i, hunk_buf.data()) != CHDERR_NONE) {
                chd_close(chd);
                return std::unexpected(ScanError::READ_ERROR);
            }
            // Write only up to logical_bytes to avoid trailing hunk padding.
            uint64_t to_write = std::min<uint64_t>(hunk_bytes, logical_bytes - bytes_written);
            out.write(reinterpret_cast<const char*>(hunk_buf.data()),
                      static_cast<std::streamsize>(to_write));
            bytes_written += to_write;
            if (bytes_written >= logical_bytes) break;
        }
        out.close();
        chd_close(chd);
        result.push_back(out_path.string());
        return result;
    }

    // ---- CD / GDI image ---------------------------------------------------
    // Each CD frame in a hunk = 2352 bytes data + 96 bytes subcode = 2448 bytes.
    constexpr uint32_t SECTOR_DATA = 2352;
    constexpr uint32_t FRAME_SIZE  = 2448;

    if (hunk_bytes < FRAME_SIZE || hunk_bytes % FRAME_SIZE != 0) {
        chd_close(chd);
        return std::unexpected(ScanError::CORRUPT_ARCHIVE);
    }
    const uint32_t sectors_per_hunk = hunk_bytes / FRAME_SIZE;

    // GDI = Dreamcast: detected by GDROM_TRACK_METADATA_TAG presence
    bool is_gdi = false;
    {
        char meta_probe[16];
        uint32_t rlen, rtag; uint8_t rflags;
        is_gdi = (chd_get_metadata(chd, GDROM_TRACK_METADATA_TAG, 0,
                                   meta_probe, sizeof(meta_probe),
                                   &rlen, &rtag, &rflags) == CHDERR_NONE);
    }

    // Calculate the CHD frame offset where each track begins.
    // CHD stores frames + extraframes per track (no pregap, no pad in offsets).
    std::vector<uint32_t> track_chd_offset(tracks.size());
    uint32_t cumulative = 0;
    for (size_t i = 0; i < tracks.size(); ++i) {
        track_chd_offset[i] = cumulative;
        uint32_t extraframes = ((tracks[i].frames + 3) / 4) * 4 - tracks[i].frames;
        cumulative += tracks[i].frames + extraframes;
    }

    // Helper: write selected frames [start, end) from the CHD to an open stream,
    // stripping the 96-byte subcode tail from each sector.
    // Audio tracks are byte-swapped (CHD big-endian → BIN little-endian).
    std::vector<uint8_t> swap_buf(SECTOR_DATA);
    auto write_frames = [&](std::ofstream& out,
                            uint32_t start_frame, uint32_t end_frame,
                            bool audio = false) -> bool {
        if (start_frame >= end_frame) return true;
        uint32_t first_hunk = start_frame / sectors_per_hunk;
        uint32_t last_hunk  = (end_frame - 1) / sectors_per_hunk;
        for (uint32_t h = first_hunk; h <= last_hunk; ++h) {
            if (chd_read(chd, h, hunk_buf.data()) != CHDERR_NONE) return false;
            for (uint32_t s = 0; s < sectors_per_hunk; ++s) {
                uint32_t gf = h * sectors_per_hunk + s;
                if (gf < start_frame) continue;
                if (gf >= end_frame)  break;
                const uint8_t* sector = hunk_buf.data() + s * FRAME_SIZE;
                if (audio) {
                    std::memcpy(swap_buf.data(), sector, SECTOR_DATA);
                    for (uint32_t j = 0; j < SECTOR_DATA; j += 2)
                        std::swap(swap_buf[j], swap_buf[j + 1]);
                    out.write(reinterpret_cast<const char*>(swap_buf.data()), SECTOR_DATA);
                } else {
                    out.write(reinterpret_cast<const char*>(sector), SECTOR_DATA);
                }
            }
        }
        return true;
    };

    if (is_gdi) {
        // GDI: one file per track, chdman naming: "base_name%02u.ext"
        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            const auto& t = tracks[ti];
            bool audio = (t.type == "AUDIO");
            char fname[512];
            snprintf(fname, sizeof(fname), "%s%02u%s",
                     base_name.c_str(), t.track_num, audio ? ".raw" : ".bin");
            fs::path track_path = fs::path(output_dir) / fname;

            std::ofstream out(track_path, std::ios::binary);
            if (!out) { chd_close(chd); return std::unexpected(ScanError::READ_ERROR); }

            uint32_t data_start = track_chd_offset[ti];
            uint32_t actual_frames = t.frames - t.pad; // GD-ROM: pad frames are gap, not data
            uint32_t data_end   = data_start + actual_frames;
            if (!write_frames(out, data_start, data_end, audio)) {
                chd_close(chd);
                return std::unexpected(ScanError::READ_ERROR);
            }
            out.close();
            result.push_back(track_path.string());
        }

        // GDI descriptor
        fs::path gdi_path = fs::path(output_dir) / (base_name + ".gdi");
        std::ofstream gdi_out(gdi_path);
        gdi_out << reconstruct_gdi_content(chd_path, base_name);
        gdi_out.close();
        result.push_back(gdi_path.string());

    } else {
        // CUE/BIN: split-bin format (one BIN per track, matching chdman extractcd -sb).
        // Determine track-number width for zero-padding
        int width = 1;
        if (!tracks.empty()) {
            uint32_t max_num = tracks.back().track_num;
            width = static_cast<int>(std::to_string(max_num).length());
        }

        for (size_t ti = 0; ti < tracks.size(); ++ti) {
            const auto& t = tracks[ti];
            bool audio = (t.type == "AUDIO");
            char track_suffix[64];
            snprintf(track_suffix, sizeof(track_suffix), " (Track %0*u).bin", width, t.track_num);
            std::string bin_name = base_name + track_suffix;
            fs::path bin_path = fs::path(output_dir) / bin_name;

            std::ofstream out(bin_path, std::ios::binary);
            if (!out) { chd_close(chd); return std::unexpected(ScanError::READ_ERROR); }

            uint32_t data_start = track_chd_offset[ti];
            uint32_t data_end   = data_start + t.frames;
            if (!write_frames(out, data_start, data_end, audio)) {
                chd_close(chd);
                return std::unexpected(ScanError::READ_ERROR);
            }
            out.close();
            result.push_back(bin_path.string());
        }

        // CUE descriptor
        fs::path cue_path = fs::path(output_dir) / (base_name + ".cue");
        std::ofstream cue_out(cue_path);
        cue_out << reconstruct_cue_content(chd_path, base_name);
        cue_out.close();
        result.push_back(cue_path.string());
    }

    chd_close(chd);
    LOG_DEBUG_STREAM("[CHD] extract_chd() completed: extracted " << result.size() << " files");
    return result;
}

// ---------------------------------------------------------------------------
// detect_chd_content_type
// Detects CHD content type by probing metadata tags (matches chdman behavior)
// ---------------------------------------------------------------------------

ChdContentType detect_chd_content_type(const std::string& chd_path) {
    try {
        chd_file* chd = nullptr;
        chd_error err = chd_open(chd_path.c_str(), CHD_OPEN_READ, nullptr, &chd);
        if (err != CHDERR_NONE) return ChdContentType::UNKNOWN;

        char metadata[256];
        uint32_t rlen, rtag; uint8_t rflags;

        // GD-ROM (Dreamcast): has GDROM_TRACK_METADATA_TAG
        if (chd_get_metadata(chd, GDROM_TRACK_METADATA_TAG, 0,
                             metadata, sizeof(metadata),
                             &rlen, &rtag, &rflags) == CHDERR_NONE) {
            chd_close(chd);
            return ChdContentType::GDROM;
        }

        // DVD (PS2, PSP, Xbox, Wii, etc.): has DVD_METADATA_TAG
        if (chd_get_metadata(chd, DVD_METADATA_TAG, 0,
                             metadata, sizeof(metadata),
                             &rlen, &rtag, &rflags) == CHDERR_NONE) {
            chd_close(chd);
            return ChdContentType::ISO;
        }

        // Hard disk (arcade drives, etc.): has HARD_DISK_METADATA_TAG
        if (chd_get_metadata(chd, HARD_DISK_METADATA_TAG, 0,
                             metadata, sizeof(metadata),
                             &rlen, &rtag, &rflags) == CHDERR_NONE) {
            chd_close(chd);
            return ChdContentType::HDD;
        }

        // CD tracks (V2 or V1): count tracks to distinguish single vs multi
        int track_count = 0;
        bool has_cd = false;
        for (uint32_t idx = 0; ; ++idx) {
            if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, idx,
                                 metadata, sizeof(metadata),
                                 &rlen, &rtag, &rflags) != CHDERR_NONE) break;
            track_count++;
            has_cd = true;
        }
        if (!has_cd) {
            for (uint32_t idx = 0; ; ++idx) {
                if (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, idx,
                                     metadata, sizeof(metadata),
                                     &rlen, &rtag, &rflags) != CHDERR_NONE) break;
                track_count++;
                has_cd = true;
            }
        }
        if (has_cd) {
            chd_close(chd);
            return (track_count > 1) ? ChdContentType::CD_CUE_MULTI
                                     : ChdContentType::CD_CUE_BIN;
        }

        // LaserDisc: has AV_METADATA_TAG
        if (chd_get_metadata(chd, AV_METADATA_TAG, 0,
                             metadata, sizeof(metadata),
                             &rlen, &rtag, &rflags) == CHDERR_NONE) {
            chd_close(chd);
            return ChdContentType::LASERDISC;
        }

        chd_close(chd);
        return ChdContentType::UNKNOWN;

    } catch (const std::exception& e) {
        LOG_DEBUG_STREAM("[CHD] detect_chd_content_type error: " << e.what());
        return ChdContentType::UNKNOWN;
    }
}

} // namespace romexplorer
