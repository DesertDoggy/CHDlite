// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_reader.cpp - ChdReader implementation

#include "chd_reader.hpp"
#include "chd_processor.hpp"

#include "chd.h"
#include "cdrom.h"
#include "chdcodec.h"
#include "hashing.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace chdlite {

// ======================> Path helpers

static std::string path_stem(const std::string& path)
{
    auto sep = path.find_last_of("/\\");
    auto name = (sep == std::string::npos) ? path : path.substr(sep + 1);
    auto dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

// ======================> Codec mapping helpers

static Codec mame_codec_to_chdlite(chd_codec_type ct)
{
    if (ct == CHD_CODEC_NONE)    return Codec::None;
    if (ct == CHD_CODEC_ZLIB)    return Codec::Zlib;
    if (ct == CHD_CODEC_ZSTD)    return Codec::Zstd;
    if (ct == CHD_CODEC_LZMA)    return Codec::LZMA;
    if (ct == CHD_CODEC_HUFFMAN) return Codec::Huffman;
    if (ct == CHD_CODEC_FLAC)    return Codec::FLAC;
    if (ct == CHD_CODEC_CD_ZLIB) return Codec::CD_Zlib;
    if (ct == CHD_CODEC_CD_ZSTD) return Codec::CD_Zstd;
    if (ct == CHD_CODEC_CD_LZMA) return Codec::CD_LZMA;
    if (ct == CHD_CODEC_CD_FLAC) return Codec::CD_FLAC;
    if (ct == CHD_CODEC_AVHUFF)  return Codec::AVHUFF;
    return Codec::None;
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

static SubcodeType mame_subtype_to_chdlite(uint32_t subtype)
{
    switch (subtype) {
    case cdrom_file::CD_SUB_NORMAL: return SubcodeType::Normal;
    case cdrom_file::CD_SUB_RAW:    return SubcodeType::Raw;
    case cdrom_file::CD_SUB_NONE:   return SubcodeType::None;
    default:                        return SubcodeType::None;
    }
}

// ======================> Implementation detail

struct ChdReader::Impl
{
    chd_file chd;
    std::string filepath;

    ContentType detect_type() const
    {
        if (!chd.opened())
            return ContentType::Unknown;

        if (!chd.check_is_gd())   return ContentType::GDROM;
        if (!chd.check_is_cd())   return ContentType::CDROM;
        if (!chd.check_is_dvd())  return ContentType::DVD;
        if (!chd.check_is_av())   return ContentType::LaserDisc;
        if (!chd.check_is_hd())   return ContentType::HardDisk;
        return ContentType::Raw;
    }

    void populate_tracks(ChdHeader& hdr) const
    {
        if (hdr.content_type != ContentType::CDROM && hdr.content_type != ContentType::GDROM)
            return;

        cdrom_file::toc toc = {};
        auto err = cdrom_file::parse_metadata(const_cast<chd_file*>(&chd), toc);
        if (err)
            return;

        hdr.num_tracks = toc.numtrks;
        hdr.is_gdrom = (toc.flags & (cdrom_file::CD_FLAG_GDROM | cdrom_file::CD_FLAG_GDROMLE)) != 0;
        hdr.tracks.resize(toc.numtrks);

        for (uint32_t i = 0; i < toc.numtrks; i++)
        {
            auto& src = toc.tracks[i];
            auto& dst = hdr.tracks[i];
            dst.track_number = i + 1;
            dst.type = mame_trktype_to_chdlite(src.trktype);
            dst.subcode = mame_subtype_to_chdlite(src.subtype);
            dst.data_size = src.datasize;
            dst.sub_size = src.subsize;
            dst.frames = src.frames;
            dst.pregap = src.pregap;
            dst.postgap = src.postgap;
            dst.session = src.session;
            dst.is_audio = (src.trktype == cdrom_file::CD_TRACK_AUDIO);
        }
    }
};

// ======================> ChdReader

ChdReader::ChdReader() : m_impl(std::make_unique<Impl>()) {}
ChdReader::~ChdReader() = default;
ChdReader::ChdReader(ChdReader&&) noexcept = default;
ChdReader& ChdReader::operator=(ChdReader&&) noexcept = default;

void ChdReader::open(const std::string& path)
{
    if (m_impl->chd.opened())
        m_impl->chd.close();

    auto err = m_impl->chd.open(path, false);
    if (err)
        throw ChdException("Failed to open CHD file: " + path + " (" + err.message() + ")");

    m_impl->filepath = path;
}

void ChdReader::close()
{
    m_impl->chd.close();
    m_impl->filepath.clear();
}

bool ChdReader::is_open() const
{
    return m_impl->chd.opened();
}

ChdHeader ChdReader::read_header() const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");

    auto& chd = m_impl->chd;
    ChdHeader hdr = {};

    hdr.version = chd.version();
    hdr.logical_bytes = chd.logical_bytes();
    hdr.hunk_bytes = chd.hunk_bytes();
    hdr.hunk_count = chd.hunk_count();
    hdr.unit_bytes = chd.unit_bytes();
    hdr.unit_count = chd.unit_count();
    hdr.compressed = chd.compressed();
    hdr.has_parent = (chd.parent() != nullptr) || chd.parent_missing();

    for (int i = 0; i < 4; i++)
        hdr.codecs[i] = mame_codec_to_chdlite(chd.compression(i));

    // Embedded hashes
    auto sha1 = chd.sha1();
    if (sha1 != util::sha1_t::null)
        hdr.sha1 = sha1.as_string();

    auto raw = chd.raw_sha1();
    if (raw != util::sha1_t::null)
        hdr.raw_sha1 = raw.as_string();

    auto parent = chd.parent_sha1();
    if (parent != util::sha1_t::null)
        hdr.parent_sha1 = parent.as_string();

    // Detect content type
    hdr.content_type = m_impl->detect_type();

    // Populate CD tracks if applicable
    m_impl->populate_tracks(hdr);

    return hdr;
}

ContentType ChdReader::detect_content_type() const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");
    return m_impl->detect_type();
}

std::vector<uint8_t> ChdReader::read_hunk(uint32_t hunk_num) const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");

    std::vector<uint8_t> buffer(m_impl->chd.hunk_bytes());
    auto err = m_impl->chd.read_hunk(hunk_num, buffer.data());
    if (err)
        throw ChdException("Failed to read hunk " + std::to_string(hunk_num) + ": " + err.message());
    return buffer;
}

std::vector<uint8_t> ChdReader::read_bytes(uint64_t offset, uint32_t length) const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");

    std::vector<uint8_t> buffer(length);
    auto err = m_impl->chd.read_bytes(offset, buffer.data(), length);
    if (err)
        throw ChdException("Failed to read bytes at offset " + std::to_string(offset) + ": " + err.message());
    return buffer;
}

std::vector<TrackInfo> ChdReader::get_tracks() const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");

    auto type = m_impl->detect_type();
    if (type != ContentType::CDROM && type != ContentType::GDROM)
        throw ChdException("Not a CD/GD-ROM CHD");

    ChdHeader hdr = {};
    hdr.content_type = type;
    m_impl->populate_tracks(hdr);
    return hdr.tracks;
}

bool ChdReader::read_sector(uint32_t lba, void* buffer, TrackType type) const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");

    auto content = m_impl->detect_type();
    if (content != ContentType::CDROM && content != ContentType::GDROM)
        throw ChdException("Not a CD/GD-ROM CHD");

    cdrom_file cd(&m_impl->chd);
    return cd.read_data(lba, buffer, static_cast<uint32_t>(type));
}

// ======================> Hash sink for ChdProcessor

class HashSink : public ProcessorSink
{
public:
    explicit HashSink(HashFlags flags = HashFlags::All)
        : m_flags(flags), m_xxh3_state(nullptr)
    {
        if (has_flag(m_flags, HashFlags::XXH3_128))
            m_xxh3_state = XXH3_createState();
    }

    ~HashSink() override
    {
        if (m_xxh3_state)
            XXH3_freeState(m_xxh3_state);
    }

    void on_track_begin(uint32_t track_num, TrackType track_type,
                        bool is_audio, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t) override
    {
        if (has_flag(m_flags, HashFlags::SHA1))   m_sha1.reset();
        if (has_flag(m_flags, HashFlags::MD5))    m_md5.reset();
        if (has_flag(m_flags, HashFlags::CRC32))  m_crc.reset();
        if (has_flag(m_flags, HashFlags::SHA256)) m_sha256.reset();
        if (has_flag(m_flags, HashFlags::XXH3_128) && m_xxh3_state)
            XXH3_128bits_reset(m_xxh3_state);
        m_bytes = 0;
        m_tracks.push_back({});
        auto& t = m_tracks.back();
        t.track_number = track_num;
        t.type = track_type;
        t.is_audio = is_audio;
    }

    void on_data(const void* data, uint32_t len) override
    {
        if (has_flag(m_flags, HashFlags::SHA1))   m_sha1.append(data, len);
        if (has_flag(m_flags, HashFlags::MD5))    m_md5.append(data, len);
        if (has_flag(m_flags, HashFlags::CRC32))  m_crc.append(data, len);
        if (has_flag(m_flags, HashFlags::SHA256)) m_sha256.append(data, len);
        if (has_flag(m_flags, HashFlags::XXH3_128) && m_xxh3_state)
            XXH3_128bits_update(m_xxh3_state, data, len);
        m_bytes += len;
    }

    void on_track_end() override
    {
        auto& t = m_tracks.back();
        t.data_bytes = m_bytes;

        if (has_flag(m_flags, HashFlags::SHA1)) {
            auto s = m_sha1.finish();
            t.sha1 = { HashAlgorithm::SHA1, s.as_string(), {s.m_raw, s.m_raw + 20} };
        }
        if (has_flag(m_flags, HashFlags::MD5)) {
            auto m = m_md5.finish();
            t.md5 = { HashAlgorithm::MD5, m.as_string(), {m.m_raw, m.m_raw + 16} };
        }
        if (has_flag(m_flags, HashFlags::CRC32)) {
            auto c = m_crc.finish();
            uint32_t val = c.m_raw;
            char buf[9];
            std::snprintf(buf, sizeof(buf), "%08x", val);
            t.crc32 = { HashAlgorithm::CRC32, buf,
                         { uint8_t(val >> 24), uint8_t(val >> 16), uint8_t(val >> 8), uint8_t(val) } };
        }
        if (has_flag(m_flags, HashFlags::SHA256)) {
            auto h = m_sha256.finish();
            t.sha256 = { HashAlgorithm::SHA256, h.as_string(), {h.m_raw, h.m_raw + 32} };
        }
        if (has_flag(m_flags, HashFlags::XXH3_128) && m_xxh3_state) {
            XXH128_hash_t h = XXH3_128bits_digest(m_xxh3_state);
            char buf[33];
            std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                          (unsigned long long)h.high64, (unsigned long long)h.low64);
            // Store 16 raw bytes: high64 big-endian then low64 big-endian
            std::vector<uint8_t> raw(16);
            for (int i = 7; i >= 0; --i) { raw[7 - i] = uint8_t(h.high64 >> (i * 8)); }
            for (int i = 7; i >= 0; --i) { raw[15 - i] = uint8_t(h.low64 >> (i * 8)); }
            t.xxh3_128 = { HashAlgorithm::SHA1 /*placeholder*/, buf, std::move(raw) };
        }
    }

    std::vector<TrackHashResult>& tracks() { return m_tracks; }

private:
    HashFlags            m_flags;
    util::sha1_creator   m_sha1;
    util::md5_creator    m_md5;
    util::crc32_creator  m_crc;
    util::sha256_creator m_sha256;
    XXH3_state_t*        m_xxh3_state;
    uint64_t             m_bytes = 0;
    std::vector<TrackHashResult> m_tracks;
};

// ======================> Sheet sink for CUE/GDI hash

static std::string msf_from_frames(uint32_t frames)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                  frames / (75 * 60), (frames / 75) % 60, frames % 75);
    return buf;
}

static const char* cue_track_type_string(TrackType trktype, uint32_t datasize)
{
    switch (trktype)
    {
    case TrackType::Mode1:
        return (datasize == 2048) ? "MODE1/2048" : "MODE1/2352";
    case TrackType::Mode1Raw:
        return "MODE1/2352";
    case TrackType::Mode2:
    case TrackType::Mode2Form1:
    case TrackType::Mode2Form2:
    case TrackType::Mode2FormMix:
        return (datasize == 2048) ? "MODE2/2048" : "MODE2/2352";
    case TrackType::Mode2Raw:
        return "MODE2/2352";
    case TrackType::Audio:
        return "AUDIO";
    default:
        return "MODE1/2352";
    }
}

/// Builds CUE or GDI sheet text in memory (no file I/O) and hashes it.
class SheetSink : public ProcessorSink
{
public:
    SheetSink(const std::string& stem, HashFlags flags = HashFlags::All)
        : m_stem(stem), m_flags(flags) {}

    void on_begin(ContentType type, uint32_t num_tracks) override
    {
        m_gdi = (type == ContentType::GDROM);
        m_num_tracks = num_tracks;
        if (m_gdi)
            m_sheet << num_tracks << "\n";
    }

    void on_track_begin(uint32_t track_num, TrackType track_type,
                        bool is_audio, uint32_t data_size,
                        uint32_t frames,
                        uint32_t pregap,
                        uint32_t pgdatasize,
                        uint32_t postgap) override
    {
        m_cur_track = track_num;
        m_cur_type = track_type;
        m_cur_audio = is_audio;
        m_cur_datasize = data_size;
        m_cur_toc_frames = frames;
        m_cur_pregap = pregap;
        m_cur_pgdatasize = pgdatasize;
        m_cur_postgap = postgap;

        // Build bin filename using same convention as extractor
        if (m_num_tracks == 1 && !m_gdi)
            m_cur_bin_name = m_stem + ".bin";
        else if (m_gdi)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02u", track_num);
            m_cur_bin_name = m_stem + buf + (is_audio ? ".raw" : ".bin");
        }
        else
        {
            char buf[8];
            if (m_num_tracks >= 10)
                std::snprintf(buf, sizeof(buf), "%02u", track_num);
            else
                std::snprintf(buf, sizeof(buf), "%u", track_num);
            m_cur_bin_name = m_stem + " (Track " + buf + ").bin";
        }
    }

    void on_data(const void*, uint32_t) override
    {
    }

    void on_track_end() override
    {
        if (m_gdi)
        {
            int gdi_type = m_cur_audio ? 0 : 4;
            m_sheet << m_cur_track << " "
                    << m_logframeofs << " "
                    << gdi_type << " "
                    << m_cur_datasize << " "
                    << "\"" << m_cur_bin_name << "\" "
                    << 0 << "\n";
            m_logframeofs += m_cur_toc_frames;
        }
        else
        {
            m_sheet << "FILE \"" << m_cur_bin_name << "\" BINARY\n";
            char tnum[8];
            std::snprintf(tnum, sizeof(tnum), "%02u", m_cur_track);
            m_sheet << "  TRACK " << tnum << " "
                    << cue_track_type_string(m_cur_type, m_cur_datasize) << "\n";
            // Pregap handling (split-bin: each file starts at offset 0)
            if (m_cur_pregap > 0 && m_cur_pgdatasize == 0)
            {
                m_sheet << "    PREGAP " << msf_from_frames(m_cur_pregap) << "\n";
                m_sheet << "    INDEX 01 00:00:00\n";
            }
            else if (m_cur_pregap > 0 && m_cur_pgdatasize > 0)
            {
                m_sheet << "    INDEX 00 00:00:00\n";
                m_sheet << "    INDEX 01 " << msf_from_frames(m_cur_pregap) << "\n";
            }
            else
            {
                m_sheet << "    INDEX 01 00:00:00\n";
            }
            if (m_cur_postgap > 0)
                m_sheet << "    POSTGAP " << msf_from_frames(m_cur_postgap) << "\n";
        }
    }

    void on_complete() override
    {
        m_content = m_sheet.str();

        // Hash the sheet content
        const void* data = m_content.data();
        uint32_t len = static_cast<uint32_t>(m_content.size());

        if (has_flag(m_flags, HashFlags::SHA1)) {
            util::sha1_creator sha1; sha1.append(data, len);
            auto s = sha1.finish();
            m_hash.sha1 = { HashAlgorithm::SHA1, s.as_string(), {s.m_raw, s.m_raw + 20} };
        }
        if (has_flag(m_flags, HashFlags::MD5)) {
            util::md5_creator md5; md5.append(data, len);
            auto m = md5.finish();
            m_hash.md5 = { HashAlgorithm::MD5, m.as_string(), {m.m_raw, m.m_raw + 16} };
        }
        if (has_flag(m_flags, HashFlags::CRC32)) {
            util::crc32_creator crc; crc.append(data, len);
            auto c = crc.finish();
            uint32_t val = c.m_raw;
            char buf[9];
            std::snprintf(buf, sizeof(buf), "%08x", val);
            m_hash.crc32 = { HashAlgorithm::CRC32, buf,
                             { uint8_t(val >> 24), uint8_t(val >> 16), uint8_t(val >> 8), uint8_t(val) } };
        }
        if (has_flag(m_flags, HashFlags::SHA256)) {
            util::sha256_creator sha256; sha256.append(data, len);
            auto h = sha256.finish();
            m_hash.sha256 = { HashAlgorithm::SHA256, h.as_string(), {h.m_raw, h.m_raw + 32} };
        }
        if (has_flag(m_flags, HashFlags::XXH3_128)) {
            XXH128_hash_t h = XXH3_128bits(data, len);
            char buf[33];
            std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                          (unsigned long long)h.high64, (unsigned long long)h.low64);
            std::vector<uint8_t> raw(16);
            for (int i = 7; i >= 0; --i) { raw[7 - i] = uint8_t(h.high64 >> (i * 8)); }
            for (int i = 7; i >= 0; --i) { raw[15 - i] = uint8_t(h.low64 >> (i * 8)); }
            m_hash.xxh3_128 = { HashAlgorithm::SHA1 /*placeholder*/, buf, std::move(raw) };
        }

        m_hash.track_number = 0;
        m_hash.type = TrackType::Mode1;
        m_hash.is_audio = false;
        m_hash.data_bytes = len;
    }

    bool is_cd() const { return m_num_tracks > 0; }
    const std::string& content() const { return m_content; }
    const TrackHashResult& hash() const { return m_hash; }

private:
    std::string m_stem;
    HashFlags m_flags;
    bool m_gdi = false;
    uint32_t m_num_tracks = 0;

    uint32_t m_cur_track = 0;
    TrackType m_cur_type = TrackType::Mode1;
    bool m_cur_audio = false;
    uint32_t m_cur_datasize = 0;
    uint32_t m_cur_toc_frames = 0;
    uint32_t m_cur_pregap = 0;
    uint32_t m_cur_pgdatasize = 0;
    uint32_t m_cur_postgap = 0;
    uint32_t m_logframeofs = 0;
    std::string m_cur_bin_name;

    std::ostringstream m_sheet;
    std::string m_content;
    TrackHashResult m_hash;
};

ContentHashResult ChdReader::hash_content(HashFlags flags) const
{
    ContentHashResult result = {};
    result.success = false;

    if (!m_impl->chd.opened())
    {
        result.error_message = "CHD file not open";
        return result;
    }

    auto& chd = m_impl->chd;
    result.content_type = m_impl->detect_type();

    // Embedded hashes
    auto sha1 = chd.sha1();
    if (sha1 != util::sha1_t::null)
        result.chd_sha1 = sha1.as_string();
    auto raw = chd.raw_sha1();
    if (raw != util::sha1_t::null)
        result.chd_raw_sha1 = raw.as_string();

    // Use the unified processor to iterate content
    HashSink hash_sink(flags);
    std::string stem = path_stem(m_impl->filepath);
    SheetSink sheet_sink(stem, flags);
    std::vector<ProcessorSink*> sinks = { &hash_sink, &sheet_sink };
    auto proc = ChdProcessor::process(m_impl->filepath, sinks);

    if (!proc.success)
    {
        result.error_message = proc.error_message;
        return result;
    }

    result.tracks = std::move(hash_sink.tracks());

    // Populate sheet hash for CD/GD-ROM content
    if (sheet_sink.is_cd())
    {
        result.sheet_content = sheet_sink.content();
        result.sheet_hash = sheet_sink.hash();
    }

    result.success = true;
    return result;
}

std::string ChdReader::get_embedded_sha1() const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");
    auto h = m_impl->chd.sha1();
    return (h != util::sha1_t::null) ? h.as_string() : std::string();
}

std::string ChdReader::get_embedded_raw_sha1() const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");
    auto h = m_impl->chd.raw_sha1();
    return (h != util::sha1_t::null) ? h.as_string() : std::string();
}

std::string ChdReader::get_embedded_parent_sha1() const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");
    auto h = m_impl->chd.parent_sha1();
    return (h != util::sha1_t::null) ? h.as_string() : std::string();
}

std::string ChdReader::read_metadata(uint32_t tag, uint32_t index) const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");
    std::string output;
    auto err = m_impl->chd.read_metadata(tag, index, output);
    if (err)
        return std::string();
    return output;
}

bool ChdReader::is_chd_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    char magic[8] = {};
    f.read(magic, 8);
    return f.good() && std::memcmp(magic, "MComprHD", 8) == 0;
}

// ======================> format_hash

static std::string content_type_string(ContentType ct)
{
    switch (ct)
    {
    case ContentType::CDROM:    return "CD-ROM";
    case ContentType::GDROM:    return "GD-ROM";
    case ContentType::DVD:      return "DVD";
    case ContentType::HardDisk: return "HardDisk";
    case ContentType::LaserDisc:return "LaserDisc";
    case ContentType::Raw:      return "Raw";
    default:                    return "Unknown";
    }
}

static std::string track_filename(const std::string& stem, const ContentHashResult& result,
                                  const TrackHashResult& thr)
{
    bool is_cd = (result.content_type == ContentType::CDROM ||
                  result.content_type == ContentType::GDROM);
    if (!is_cd)
        return stem + ((result.content_type == ContentType::DVD) ? ".iso" : ".bin");

    bool gdi = (result.content_type == ContentType::GDROM);
    if (gdi) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02u", thr.track_number);
        return stem + buf + (thr.is_audio ? ".raw" : ".bin");
    }
    if (result.tracks.size() == 1)
        return stem + ".bin";

    char buf[8];
    if (result.tracks.size() >= 10)
        std::snprintf(buf, sizeof(buf), "%02u", thr.track_number);
    else
        std::snprintf(buf, sizeof(buf), "%u", thr.track_number);
    return stem + " (Track " + buf + ").bin";
}

static std::string sheet_filename(const std::string& stem, const ContentHashResult& result)
{
    return stem + ((result.content_type == ContentType::GDROM) ? ".gdi" : ".cue");
}

// JSON helper — escape a string for JSON
static std::string json_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    out += '"';
    return out;
}

std::string ChdReader::format_hash(const ContentHashResult& result,
                                   HashOutputFormat format,
                                   const std::string& stem_override)
{
    std::string stem = stem_override.empty() ? "output" : stem_override;
    std::ostringstream out;

    switch (format)
    {
    case HashOutputFormat::Log:
    {
        out << "; CHDlite Hash - " << stem << "\n";
        out << "; Content Type: " << content_type_string(result.content_type) << "\n";
        out << "; Tracks: " << result.tracks.size() << "\n";
        for (const auto& t : result.tracks) {
            std::string fn = track_filename(stem, result, t);
            out << fn << "\n";
            if (!t.sha1.hex_string.empty())   out << "  SHA1:   " << t.sha1.hex_string << "\n";
            if (!t.md5.hex_string.empty())     out << "  MD5:    " << t.md5.hex_string << "\n";
            if (!t.crc32.hex_string.empty())   out << "  CRC32:  " << t.crc32.hex_string << "\n";
            if (!t.sha256.hex_string.empty())  out << "  SHA256: " << t.sha256.hex_string << "\n";
            if (!t.xxh3_128.hex_string.empty()) out << "  XXH3_128: " << t.xxh3_128.hex_string << "\n";
        }
        if (!result.sheet_content.empty()) {
            std::string fn = sheet_filename(stem, result);
            out << fn << "\n";
            const auto& sh = result.sheet_hash;
            if (!sh.sha1.hex_string.empty())   out << "  SHA1:   " << sh.sha1.hex_string << "\n";
            if (!sh.md5.hex_string.empty())     out << "  MD5:    " << sh.md5.hex_string << "\n";
            if (!sh.crc32.hex_string.empty())   out << "  CRC32:  " << sh.crc32.hex_string << "\n";
            if (!sh.sha256.hex_string.empty())  out << "  SHA256: " << sh.sha256.hex_string << "\n";
            if (!sh.xxh3_128.hex_string.empty()) out << "  XXH3_128: " << sh.xxh3_128.hex_string << "\n";
        }
        break;
    }
    case HashOutputFormat::SFV:
    {
        out << "; Generated by CHDlite\n";
        for (const auto& t : result.tracks) {
            std::string fn = track_filename(stem, result, t);
            if (!t.crc32.hex_string.empty())
                out << fn << " " << t.crc32.hex_string << "\n";
        }
        if (!result.sheet_content.empty()) {
            std::string fn = sheet_filename(stem, result);
            if (!result.sheet_hash.crc32.hex_string.empty())
                out << fn << " " << result.sheet_hash.crc32.hex_string << "\n";
        }
        break;
    }
    case HashOutputFormat::JSON:
    {
        out << "{\n";
        out << "  \"chd\": " << json_escape(stem + ".chd") << ",\n";
        out << "  \"content_type\": " << json_escape(content_type_string(result.content_type)) << ",\n";
        if (!result.chd_sha1.empty())
            out << "  \"chd_sha1\": " << json_escape(result.chd_sha1) << ",\n";
        if (!result.chd_raw_sha1.empty())
            out << "  \"chd_raw_sha1\": " << json_escape(result.chd_raw_sha1) << ",\n";
        out << "  \"tracks\": [\n";
        for (size_t i = 0; i < result.tracks.size(); ++i) {
            const auto& t = result.tracks[i];
            std::string fn = track_filename(stem, result, t);
            out << "    {\n";
            out << "      \"track\": " << t.track_number << ",\n";
            out << "      \"filename\": " << json_escape(fn) << ",\n";
            out << "      \"bytes\": " << t.data_bytes << ",\n";
            out << "      \"audio\": " << (t.is_audio ? "true" : "false");
            if (!t.sha1.hex_string.empty())   out << ",\n      \"sha1\": " << json_escape(t.sha1.hex_string);
            if (!t.md5.hex_string.empty())     out << ",\n      \"md5\": " << json_escape(t.md5.hex_string);
            if (!t.crc32.hex_string.empty())   out << ",\n      \"crc32\": " << json_escape(t.crc32.hex_string);
            if (!t.sha256.hex_string.empty())  out << ",\n      \"sha256\": " << json_escape(t.sha256.hex_string);
            if (!t.xxh3_128.hex_string.empty()) out << ",\n      \"xxh3_128\": " << json_escape(t.xxh3_128.hex_string);
            out << "\n    }" << (i + 1 < result.tracks.size() ? "," : "") << "\n";
        }
        out << "  ]";
        if (!result.sheet_content.empty()) {
            std::string fn = sheet_filename(stem, result);
            const auto& sh = result.sheet_hash;
            out << ",\n  \"sheet\": {\n";
            out << "    \"filename\": " << json_escape(fn) << ",\n";
            out << "    \"bytes\": " << result.sheet_content.size();
            if (!sh.sha1.hex_string.empty())   out << ",\n    \"sha1\": " << json_escape(sh.sha1.hex_string);
            if (!sh.md5.hex_string.empty())     out << ",\n    \"md5\": " << json_escape(sh.md5.hex_string);
            if (!sh.crc32.hex_string.empty())   out << ",\n    \"crc32\": " << json_escape(sh.crc32.hex_string);
            if (!sh.sha256.hex_string.empty())  out << ",\n    \"sha256\": " << json_escape(sh.sha256.hex_string);
            if (!sh.xxh3_128.hex_string.empty()) out << ",\n    \"xxh3_128\": " << json_escape(sh.xxh3_128.hex_string);
            out << "\n  }";
        }
        out << "\n}\n";
        break;
    }
    }

    return out.str();
}

} // namespace chdlite
