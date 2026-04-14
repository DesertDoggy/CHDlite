// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_reader.cpp - ChdReader implementation

#include "chd_reader.hpp"

#include "chd.h"
#include "cdrom.h"
#include "chdcodec.h"
#include "hashing.h"

#include <cstring>
#include <fstream>

namespace chdlite {

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

HashResult ChdReader::hash_content(HashAlgorithm algorithm) const
{
    if (!m_impl->chd.opened())
        throw ChdException("CHD file not open");

    auto& chd = m_impl->chd;
    const uint32_t hunk_bytes = chd.hunk_bytes();
    const uint32_t hunk_count = chd.hunk_count();
    std::vector<uint8_t> buffer(hunk_bytes);
    HashResult result;
    result.algorithm = algorithm;

    switch (algorithm)
    {
    case HashAlgorithm::SHA1:
    {
        util::sha1_creator hasher;
        for (uint32_t h = 0; h < hunk_count; h++)
        {
            auto err = chd.read_hunk(h, buffer.data());
            if (err)
                throw ChdException("Read error at hunk " + std::to_string(h));

            // For the last hunk, only hash up to logical_bytes
            uint32_t bytes = hunk_bytes;
            if (h == hunk_count - 1)
            {
                uint64_t remaining = chd.logical_bytes() - (uint64_t(h) * hunk_bytes);
                if (remaining < hunk_bytes)
                    bytes = static_cast<uint32_t>(remaining);
            }
            hasher.append(buffer.data(), bytes);
        }
        auto digest = hasher.finish();
        result.hex_string = digest.as_string();
        result.raw.assign(digest.m_raw, digest.m_raw + 20);
        break;
    }
    case HashAlgorithm::MD5:
    {
        util::md5_creator hasher;
        for (uint32_t h = 0; h < hunk_count; h++)
        {
            auto err = chd.read_hunk(h, buffer.data());
            if (err)
                throw ChdException("Read error at hunk " + std::to_string(h));

            uint32_t bytes = hunk_bytes;
            if (h == hunk_count - 1)
            {
                uint64_t remaining = chd.logical_bytes() - (uint64_t(h) * hunk_bytes);
                if (remaining < hunk_bytes)
                    bytes = static_cast<uint32_t>(remaining);
            }
            hasher.append(buffer.data(), bytes);
        }
        auto digest = hasher.finish();
        result.hex_string = digest.as_string();
        result.raw.assign(digest.m_raw, digest.m_raw + 16);
        break;
    }
    case HashAlgorithm::CRC32:
    {
        util::crc32_creator hasher;
        for (uint32_t h = 0; h < hunk_count; h++)
        {
            auto err = chd.read_hunk(h, buffer.data());
            if (err)
                throw ChdException("Read error at hunk " + std::to_string(h));

            uint32_t bytes = hunk_bytes;
            if (h == hunk_count - 1)
            {
                uint64_t remaining = chd.logical_bytes() - (uint64_t(h) * hunk_bytes);
                if (remaining < hunk_bytes)
                    bytes = static_cast<uint32_t>(remaining);
            }
            hasher.append(buffer.data(), bytes);
        }
        auto digest = hasher.finish();
        result.hex_string = digest.as_string();
        uint32_t val = digest.m_raw;
        result.raw = { uint8_t(val >> 24), uint8_t(val >> 16), uint8_t(val >> 8), uint8_t(val) };
        break;
    }
    }

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

} // namespace chdlite
