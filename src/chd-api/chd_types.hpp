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

// ======================> Log level (shared across library and CLI)

enum class LogLevel
{
    Debug    = 0,
    Info     = 1,
    Warning  = 2,
    Error    = 3,
    Critical = 4,
    None     = 5,
};

// Callback signature for library-level log messages.
// Modules call this (if set) instead of writing to stderr/files directly.
using LogCallback = std::function<void(LogLevel, const std::string&)>;

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
    SHA256,
    XXH3_128
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
    Hashes,  // human-readable text (.hashes)
    SFV,     // Simple File Verification (filename CRC32)
    JSON     // JSON object
};

// ======================> CUE sheet naming/format style

enum class CueStyle : int
{
    Chdman         = 0,   // (Track N) suffix always, no CATALOG — matches chdman output
    Redump         = 1,   // no (Track 1) for single-track, no CATALOG
    RedumpCatalog  = 2,   // Redump naming + CATALOG 0000000000000 header
    // 3..254 reserved for future styles
    Unmatched      = 255, // sentinel: no style matched the database hash
};

/// Result of match_cue(): identifies which CUE style matches a database hash.
struct CueMatchResult
{
    CueStyle        style = CueStyle::Unmatched;
    std::string     cue_data;       // converted CUE text (empty if unmatched)
    HashAlgorithm   hash_type = HashAlgorithm::SHA1;
    std::string     cue_hash;       // hex hash of the matched CUE (empty if unmatched)
};

// ======================> CD system detection for smart codec defaults

enum class GamePlatform
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

// How the disc format (cd/dvd/gd/raw) was determined during detect_input()
enum class FormatSource
{
    Unknown,
    Extension,      // file extension (.cue/.gdi/.toc/.nrg) → format determined without reading
    SyncBytes,      // CD sync bytes (0x00 FF×10 0x00) present → raw CD sectors (2352/2336)
    DvdMatch,       // 2048-byte sectors; DVD-specific platform detected (PS2, PSP, DVD-Video…)
    DvdFallback,    // 2048-byte sectors; no specific platform matched → DVD by default
    CdOverride,     // 2048-byte sectors; CD-specific platform detected, overrode DVD default
};

// How the game platform was identified during detect_game_platform()
enum class PlatformSource
{
    Unknown,
    Sector0,    // sector-0 magic bytes (3DO, MegaCD, Saturn, Dreamcast)
    Iso9660,    // ISO 9660 volume descriptor / directory entries
    Heuristic,  // multi-sector heuristic (PC Engine)
    Default,    // fell through to GenericCD or DVDISO
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
    std::string   hex_string;    // hex-encoded digest (empty if error)
    std::vector<uint8_t> raw;    // raw digest bytes (empty if error)
    std::string   error;         // non-empty if this algorithm failed; other algorithms may still be valid
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

struct VerifyResult
{
    bool        success = false;
    bool        raw_sha1_match = false;
    bool        overall_sha1_match = false;
    std::string computed_raw_sha1;
    std::string computed_overall_sha1;
    std::string header_raw_sha1;
    std::string header_overall_sha1;
    std::string error_message;
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
    GamePlatform    detected_game_platform;
    std::string detected_title;
    std::string detected_manufacturer_id;
    Codec       codec_used;
    FormatSource    detected_format_source   = FormatSource::Unknown;
    PlatformSource  detected_platform_source = PlatformSource::Unknown;
};

// ======================> Options

struct ExtractOptions
{
    std::string output_dir;
    std::string output_filename;   // override auto-generated name (chdman: --output)
    bool        force_bin_cue = false;  // force BIN/CUE instead of GDI
    bool        force_raw = false;      // extract raw hunks (no format interpretation)

    // CD-specific (chdman extractcd equivalents)
    CueStyle    cue_style = CueStyle::Chdman;  // CUE naming convention
    std::string output_bin;             // override bin filename template (chdman: --outputbin)
    bool        split_bin = true;       // split into per-track bin files (chdman: --splitbin, default true)

    // Raw/DVD/HD partial extraction (chdman extractraw/extracthd/extractdvd)
    uint64_t    input_start_byte = 0;   // starting byte offset (chdman: --inputstartbyte)
    uint64_t    input_start_hunk = 0;   // starting hunk offset (chdman: --inputstarthunk)
    uint64_t    input_bytes = 0;        // number of bytes to extract, 0 = all (chdman: --inputbytes)
    uint64_t    input_hunks = 0;        // number of hunks to extract, 0 = all (chdman: --inputhunks)

    // LaserDisc (chdman extractld)
    uint64_t    input_start_frame = 0;  // starting frame (chdman: --inputstartframe)
    uint64_t    input_frames = 0;       // number of frames, 0 = all (chdman: --inputframes)

    // General
    std::string parent_chd_path;        // parent CHD for delta (chdman: --inputparent)
    bool        force_overwrite = false; // overwrite existing output (chdman: --force)

    // Hash: compute hashes of extracted content alongside extraction
    HashFlags   hash = HashFlags(0);    // 0 = no hashing, or combine SHA1|MD5|CRC32 etc.

    // Progress callback: (bytes_processed, total_bytes) → return false to cancel
    std::function<bool(uint64_t, uint64_t)> progress_callback;

    // Log callback — called with Error messages on failure.
    LogCallback log_callback;
};

// ======================> CUE fix modes (bitmask for future expansion)

enum class FixCue : uint32_t
{
    None     = 0,
    Single   = 1 << 0,   // Fix single-track CUE: if referenced .bin doesn't exist, find the actual .bin
    // Future: Multi, Pregap, etc.
};

inline FixCue operator|(FixCue a, FixCue b) { return FixCue(uint32_t(a) | uint32_t(b)); }
inline FixCue operator&(FixCue a, FixCue b) { return FixCue(uint32_t(a) & uint32_t(b)); }
inline bool has_fix_cue(FixCue flags, FixCue f) { return (uint32_t(flags) & uint32_t(f)) != 0; }

struct ArchiveOptions
{
    // Compression: up to 4 codecs tried per hunk (best ratio wins).
    // codec is a convenience shorthand — sets compression[0] and fills rest with None.
    // If both are left at defaults, archive() picks smart system-aware codecs.
    Codec       codec = Codec::None;        // None = auto-detect
    Codec       compression[4] = { Codec::None, Codec::None, Codec::None, Codec::None };
    bool        uncompressed = false;       // true = store raw (no compression)
    bool        best = false;               // true = maximum ratio (zstd,lzma,zlib / cdzs,cdlz,cdzl,cdfl)

    uint32_t    hunk_bytes = 0;             // 0 = auto (CD: 2448*8, DVD: 2048, HD: 4096 etc.)
    uint32_t    unit_bytes = 0;             // 0 = auto
    int         num_processors = 0;         // 0 = auto (all cores)
    int         max_concurrent_files = 0;   // 0 = auto; batch: max files processed in parallel

    // For CD images: override input format parsing
    std::string input_format;               // "cue", "gdi", "iso", "nrg", "toc"

    // Parent CHD for delta compression
    std::string parent_chd_path;

    // CUE file fixes
    FixCue      fix_cue = FixCue::None;

    // Title/ID detection and rename
    bool        detect_title = false;       // extract game title from disc filesystem
    bool        rename_to_title = false;    // rename output file to detected title (implies detect_title)
    bool        rename_to_gameid = false;   // rename output file to game ID (implies detect_title)

    // Progress callback
    std::function<bool(uint64_t, uint64_t)> progress_callback;

    // Log callback — called with Debug/Info/Warning/Error/Critical messages from the library.
    // If null, library is silent (the CLI installs its own handler).
    LogCallback log_callback;

    // Helper: true if user specified no codecs at all (use smart defaults)
    bool has_custom_compression() const {
        if (uncompressed) return true;
        if (codec != Codec::None) return true;
        for (auto c : compression)
            if (c != Codec::None) return true;
        return false;
    }
};

// ======================> Exception hierarchy
//
//  ChdException               — base; catch-all for any CHD operation failure
//  ├─ ChdInputException       — bad/missing/unreadable input file or track data
//  ├─ ChdParentException      — parent CHD cannot be opened
//  ├─ ChdOutputException      — output CHD cannot be created (permissions, disk full, etc.)
//  ├─ ChdMetadataException    — CHD created but metadata write failed (partially written)
//  ├─ ChdCompressionException — MAME codec failure mid-compression (Critical severity)
//  ├─ ChdHashException        — fatal I/O or state failure during hashing (individual algorithm
//  │                            errors are recorded in HashResult::error without throwing)
//  └─ ChdCancelledException   — progress callback returned false (Info severity)

class ChdException : public std::exception
{
public:
    explicit ChdException(std::string message, LogLevel severity = LogLevel::Error)
        : m_message(std::move(message)), m_severity(severity) {}
    const char* what() const noexcept override { return m_message.c_str(); }
    LogLevel severity() const noexcept { return m_severity; }
private:
    std::string m_message;
    LogLevel    m_severity;
};

class ChdInputException : public ChdException
{
public:
    explicit ChdInputException(std::string message)
        : ChdException(std::move(message), LogLevel::Error) {}
};

class ChdParentException : public ChdException
{
public:
    explicit ChdParentException(std::string message)
        : ChdException(std::move(message), LogLevel::Error) {}
};

class ChdOutputException : public ChdException
{
public:
    explicit ChdOutputException(std::string message)
        : ChdException(std::move(message), LogLevel::Error) {}
};

class ChdMetadataException : public ChdException
{
public:
    explicit ChdMetadataException(std::string message)
        : ChdException(std::move(message), LogLevel::Error) {}
};

// Thrown when the input format is ambiguous or conflicting (strict mode only).
// e.g. a 2048-byte ISO where both CD-specific and DVD-specific platforms were detected.
class ChdFormatException : public ChdException
{
public:
    explicit ChdFormatException(std::string message)
        : ChdException(std::move(message), LogLevel::Warning) {}
};

class ChdCompressionException : public ChdException
{
public:
    explicit ChdCompressionException(std::string message)
        : ChdException(std::move(message), LogLevel::Critical) {}
};

// Thrown when a hash operation cannot proceed at all (CHD not open, I/O failure).
// Per-algorithm failures (individual hash library errors) are recorded in
// HashResult::error and do NOT throw — other algorithms can still complete.
class ChdHashException : public ChdException
{
public:
    explicit ChdHashException(std::string message)
        : ChdException(std::move(message), LogLevel::Error) {}
};

class ChdCancelledException : public ChdException
{
public:
    explicit ChdCancelledException(std::string message = "Operation cancelled by user")
        : ChdException(std::move(message), LogLevel::Info) {}
};

} // namespace chdlite

#endif // CHDLITE_CHD_TYPES_HPP
