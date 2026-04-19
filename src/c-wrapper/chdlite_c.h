// license:GPLv3
// CHDlite - Cross-platform CHD library
// chdlite_c.h - C wrapper API for FFI (Flutter, Python, etc.)

#ifndef CHDLITE_C_H
#define CHDLITE_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
  #ifdef CHDLITE_FFI_BUILD
    #define CHDLITE_API __declspec(dllexport)
  #else
    #define CHDLITE_API __declspec(dllimport)
  #endif
#else
  #define CHDLITE_API __attribute__((visibility("default")))
#endif

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

/// Progress callback. Return false to cancel the operation.
/// @param current  Bytes processed so far.
/// @param total    Total bytes (0 if unknown).
typedef int (*chdlite_progress_fn)(uint64_t current, uint64_t total);

/// Log callback.
/// @param level  0=Debug 1=Info 2=Warning 3=Error 4=Critical.
/// @param msg    Log message (valid only for the duration of the call).
typedef void (*chdlite_log_fn)(int level, const char* msg);

/// Set the progress callback for subsequent operations.
CHDLITE_API void chdlite_set_progress_callback(chdlite_progress_fn cb);

/// Set the log callback for subsequent operations.
CHDLITE_API void chdlite_set_log_callback(chdlite_log_fn cb);

/// Cancel the currently running operation (thread-safe).
CHDLITE_API void chdlite_cancel(void);

// ---------------------------------------------------------------------------
// Operations — all return a JSON string (caller frees with chdlite_free)
// ---------------------------------------------------------------------------

/// Read CHD header info. Returns JSON with header fields.
/// On error, JSON contains {"success":false,"error":"..."}.
CHDLITE_API char* chdlite_read(const char* chd_path);

/// Hash CHD content. Returns JSON with per-track hashes.
/// @param algorithms  Comma-separated: "sha1,md5,crc32,sha256,xxh3" (NULL = sha1).
CHDLITE_API char* chdlite_hash(const char* chd_path, const char* algorithms);

/// Extract CHD to CUE/BIN or raw image.
/// @param output_dir  Output directory (NULL = same dir as input).
/// @param split_bin   1 = per-track BIN files, 0 = single BIN.
CHDLITE_API char* chdlite_extract(const char* chd_path,
                                  const char* output_dir,
                                  int split_bin);

/// Compress input (CUE/BIN/ISO/GDI) to CHD.
/// @param output_path  Output CHD path (NULL = input stem + .chd).
/// @param codec        Codec name: "zstd","zlib","lzma","flac" etc. (NULL = auto).
/// @param hunk_size    Hunk size in bytes (0 = auto).
/// @param unit_size    Unit size in bytes (0 = auto).
/// @param threads      Number of threads (0 = auto).
CHDLITE_API char* chdlite_compress(const char* input_path,
                                   const char* output_path,
                                   const char* codec,
                                   int hunk_size,
                                   int unit_size,
                                   int threads);

/// Free a string returned by any chdlite_* function.
CHDLITE_API void chdlite_free(char* ptr);

/// Return library version string (e.g. "0.2.1"). Do NOT free.
CHDLITE_API const char* chdlite_version(void);

#ifdef __cplusplus
}
#endif

#endif // CHDLITE_C_H
