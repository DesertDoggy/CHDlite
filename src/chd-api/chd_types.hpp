// license:GPLv3
// CHDlite - Cross-platform CHD library
// chd_types.hpp - Enums, structs, and type definitions

#ifndef CHDLITE_CHD_TYPES_HPP
#define CHDLITE_CHD_TYPES_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chdlite {

// ======================> Content type detected from CHD metadata

enum class ContentType
{
    Unknown,
    HardDisk,
    CDROM,
    GDROM,       // Dreamcast GD-ROM
    DVD,
    LaserDisc,
    Raw
};

// ======================> Compression codec identifiers

enum class Codec
{
    None,
    Zlib,
    ZlibPlus,     // legacy V3/V4
    Zstd,
    LZMA,
    FLAC,
    Huffman,
    AVHUFF,       // A/V laserdisc
    // CD-specific compound codecs
    CD_Zlib,      // CD data+subcode ZLIB
    CD_Zstd,      // CD data+subcode ZSTD
    CD_LZMA,      // CD data+subcode LZMA
    CD_FLAC       // CD data+subcode FLAC
};

// ======================> Hash algorithm selection

enum class HashAlgorithm
{
    SHA1,
    MD5,
    CRC32,
    SHA256
};

// ======================> Hash algorithm flags (bitmask for selecting which to compute)

enum class HashFlags : uint32_t
{
    SHA1   = 1 << 0,
    MD5    = 1 << 1,
    CRC32  = 1 << 2,
    SHA256 = 1 << 3,
    XXH3_128 = 1 << 4,
    All    = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4)
};

inline HashFlags operator|(HashFlags a, HashFlags b) { return HashFlags(uint32_t(a) | uint32_t(b)); }
inline HashFlags operator&(HashFlags a, HashFlags b) { return HashFlags(uint32_t(a) & uint32_t(b)); }
inline bool has_flag(HashFlags flags, HashFlags f) { return (uint32_t(flags) & uint32_t(f)) != 0; }

// ======================> Hash output format

enum class HashOutputFormat
{
    Log,     // human-readable text
    SFV,     // Simple File Verification (filename CRC32)
    JSON     // JSON object
};

// ======================> CD system detection for smart codec defaults

enum class CdSystem
{
    Unknown,
    PS1,
    PS2,
    PSP,
    Saturn,
    MegaCD,
    PCEngine,
    NeoGeoCD,
    ThreeDO,
    Dreamcast,   // GD-ROM
    DVDISO,
    GenericCD
};

// ======================> Track types (mirrors MAME cdrom_file constants)

enum class TrackType
{
    Mode1 = 0,        // 2048 bytes/sector
    Mode1Raw,         // 2352 bytes/sector
    Mode2,            // 2336 bytes/sector
    Mode2Form1,       // 2048 bytes/sector
    Mode2Form2,       // 2324 bytes/sector
    Mode2FormMix,     // 2336 bytes/sector
    Mode2Raw,         // 2352 bytes/sector
    Audio             // 2352 bytes/sector (588 samples)
};

enum class SubcodeType
{
    Normal = 0,   // cooked 96 bytes/sector
    Raw,          // raw uninterleaved 96 bytes/sector
    None          // no subcode
};

// ======================> Structs

struct HashResult
{
    HashAlgorithm algorithm;
    std::string   hex_string;    // hex-encoded digest
    std::vector<uint8_t> raw;    // raw digest bytes
};

struct TrackHashResult
{
    uint32_t    track_number;    // 1-based (0 = whole-file for DVD/raw)
    TrackType   type;
    bool        is_audio;
    uint64_t    data_bytes;      // total bytes hashed for this track
    HashResult  sha1;
    HashResult  md5;
    HashResult  crc32;
    HashResult  sha256;
    HashResult  xxh3_128;
};

struct ContentHashResult
{
    bool        success;
    std::string error_message;
    ContentType content_type;
    std::string chd_sha1;        // embedded CHD SHA-1 (overall)
    std::string chd_raw_sha1;    // embedded raw SHA-1
    std::vector<TrackHashResult> tracks;  // per-track (or single entry for DVD/raw)

    // Generated CUE/GDI sheet (CD/GD-ROM only, constructed — not stored in CHD)
    std::string     sheet_content;   // CUE or GDI text (empty for non-CD)
    TrackHashResult sheet_hash;      // hashes of the sheet content
};

struct TrackInfo
{
    uint32_t    track_number;
    TrackType   type;
    SubcodeType subcode;
    uint32_t    data_size;       // bytes per sector
    uint32_t    sub_size;        // subcode bytes per sector
    uint32_t    frames;          // total frames in track
    uint32_t    pregap;          // pregap frames
    uint32_t    postgap;         // postgap frames
    uint32_t    session;         // session number
    uint32_t    start_lba;       // logical frame offset (LBA where track data begins)
    bool        is_audio;
};

struct ChdHeader
{
    uint32_t    version;
    uint64_t    logical_bytes;
    uint32_t    hunk_bytes;
    uint32_t    hunk_count;
    uint32_t    unit_bytes;
    uint64_t    unit_count;
    bool        compressed;
    bool        has_parent;

    // Content type detected from metadata
    ContentType content_type;

    // Compression codecs (V5: 4 slots)
    Codec       codecs[4];

    // Embedded hashes from header
    std::string sha1;            // combined raw+meta SHA-1 (hex)
    std::string raw_sha1;        // raw data SHA-1 (hex)
    std::string parent_sha1;     // parent SHA-1 (hex, empty if no parent)

    // CD-specific
    uint32_t    num_tracks;
    bool        is_gdrom;
    std::vector<TrackInfo> tracks;
};

struct ExtractionResult
{
    bool        success;
    std::string error_message;
    std::string output_path;     // primary output file
    std::vector<std::string> output_files;  // all generated files
    uint64_t    bytes_written;
    ContentType detected_type;
};

struct ArchiveResult
{
    bool        success;
    std::string error_message;
    std::string output_path;
    uint64_t    input_bytes;
    uint64_t    output_bytes;
    double      compression_ratio;
    CdSystem    detected_system;
    std::string detected_title;    // game title (populated when detect_title is true)
    std::string detected_gameid;   // product/serial number (populated when detect_title is true)
    Codec       codec_used;
};

// ======================> Options

struct ExtractOptions
{
    std::string output_dir;
    std::string output_filename;   // override auto-generated name
    bool        force_bin_cue = false;  // force BIN/CUE instead of GDI
    bool        force_raw = false;      // extract raw hunks (no format interpretation)

    // Progress callback: (bytes_processed, total_bytes) → return false to cancel
    std::function<bool(uint64_t, uint64_t)> progress_callback;
};

struct ArchiveOptions
{
    Codec       codec = Codec::None;        // None = auto-detect
    uint32_t    hunk_bytes = 0;             // 0 = auto (CD: 2448*8, HD: 4096 etc.)
    uint32_t    unit_bytes = 0;             // 0 = auto
    int         num_processors = 0;         // 0 = auto (all cores)

    // For CD images: override input format parsing
    std::string input_format;               // "cue", "gdi", "iso", "nrg", "toc"

    // Parent CHD for delta compression
    std::string parent_chd_path;

    // Title/ID detection and rename
    bool        detect_title = false;       // extract game title from disc filesystem
    bool        rename_to_title = false;    // rename output file to detected title (implies detect_title)
    bool        rename_to_gameid = false;   // rename output file to game ID (implies detect_title)

    // Progress callback
    std::function<bool(uint64_t, uint64_t)> progress_callback;
};

// ======================> Exception

class ChdException : public std::exception
{
public:
    explicit ChdException(std::string message) : m_message(std::move(message)) {}
    const char* what() const noexcept override { return m_message.c_str(); }
private:
    std::string m_message;
};

} // namespace chdlite

#endif // CHDLITE_CHD_TYPES_HPP
