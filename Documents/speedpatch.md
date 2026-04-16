# CHDlite Speed Patch Plan

Performance optimizations for CHDlite targeting **Windows / Linux x86-64** with **AVX2 baseline**.
Based on analysis of [chdman-simd](../References/chdman-simd/) patches (MAME 0.286, −71% total).

Current dev platform: macOS arm64. Target: modern desktop PCs (Intel Haswell 2013+ / AMD Excavator 2015+).

---

## Decisions

| Decision | Rationale |
|----------|-----------|
| **AVX2 baseline** (no scalar fallback) | All modern x86-64 desktops have AVX2. Only edge cases without: VMs with restricted CPUID, pre-2013 hardware. |
| **SHA-NI: runtime CPUID dispatch** | SHA-NI is NOT universal on AVX2 CPUs. Intel: Ice Lake+ (2019). AMD: Zen+ (2018). Need SSE2 fallback path. |
| **zlib-ng with ZLIB_COMPAT=ON** | Drop-in API-compatible. Runtime CPUID dispatch for SSE2/SSE4.2/PCLMULQDQ/AVX2/AVX512. |
| **LZMA ASM: conditional on UASM** | If assembler available → ASM decoder. Otherwise → C fallback. No hard dependency. |
| **Byte-swap: compile flag only** | `-mavx2` auto-vectorizes `std::swap(buf[i], buf[i+1])` to `vpshufb`. No manual intrinsics needed. |
| **Batch parallelism: 2–3 files max** | Each CHD compressor allocates ~256-hunk buffers. Limit concurrency to bound memory. |
| **Pipeline + batch = both layers** | Per-file pipeline (chdman-simd approach) × multi-file batch = multiplicative speedup. |

---

## chdman-simd Patch Applicability

| Patch | What it does | Win x86-64 | Linux x86-64 | macOS arm64 |
|-------|-------------|------------|--------------|-------------|
| `01_chdman_pipeline` | N_SLOTS=3 triple-buffer pipeline, SHA1 hot-cache | ✅ | ✅ | ✅ |
| `02_winfile_sequential` | `FILE_FLAG_SEQUENTIAL_SCAN` I/O hint | ✅ Win only | ❌ (use `posix_fadvise`) | ❌ (use `fcntl F_RDAHEAD`) |
| `03_hashing_simd` | CRC16 slice-by-16, SHA1 SSE2/SHA-NI dispatch | ✅ | ✅ | ⚠️ ARM SHA intrinsics |
| `04_chdcodec_flac_lzma` | FLAC 3→2 encode, LZMA persistent encoder | ✅ | ✅ | ✅ |
| `05_chd_warning` | `int` → `std::size_t` sign-compare fix | ✅ | ✅ | ✅ (already fixed) |

---

## Implementation Steps

### Phase 1 — Drop-in Library Swaps (HIGH impact, LOW effort)

#### Step 1: Replace zlib with zlib-ng

- **Impact**: inflate −76% CPU (measured), ~4× faster overall inflate
- **Effort**: LOW
- **Change**: `vcpkg.json` — `"zlib"` → `"zlib-ng[compat]"`
- **How**: ZLIB_COMPAT mode = same API. `find_package(ZLIB)` unchanged in CMakeLists.txt.
- **Runtime**: zlib-ng has built-in CPUID dispatch (SSE2, SSE4.2, PCLMULQDQ, AVX2, AVX512)
- **Affects**: All zlib-compressed CHD hunks (very common in CHD v4/v5)
- **Risk**: LOW — well-tested drop-in

#### Step 2: Ensure xxHash AVX2 dispatch

- **Impact**: XXH3 vectorized (already SIMD-capable, just needs flags)
- **Effort**: LOW (part of Step 9)
- **How**: CHDlite uses `#define XXH_INLINE_ALL` → compiled with whatever `-march` flags are active. Currently **no SIMD flags** → scalar codegen. Fix: add `-mavx2` to CMakeLists.txt.
- **Files**: `CMakeLists.txt`

---

### Phase 2 — Code-Level Algorithm Fixes (MEDIUM impact, MEDIUM effort)

#### Step 3: LZMA encoder persistent instance

- **Impact**: Eliminates 1000+ alloc/free cycles per CHD creation
- **Effort**: MEDIUM
- **Current**: `LzmaEnc_Create()` + `LzmaEnc_Destroy()` called per `compress()` — per hunk
- **Fix**: Keep `CLzmaEncHandle m_encoder` as class member. Create in constructor, destroy in destructor.  `LzmaEnc_SetProps()` resets state without realloc.
- **Files**: `src/mame/util/chdcodec.cpp` — `chd_lzma_compressor` class
- **Ref**: chdman-simd patch `04_chdcodec_flac_lzma`

#### Step 4: FLAC 3→2 encode elimination

- **Impact**: +2.5% create/copy speed
- **Effort**: LOW–MEDIUM
- **Current**: Encodes LE, then BE, then re-encodes winner into final buffer (3 total)
- **Fix**: Encode LE into dest, BE into tmpbuf, `memcpy` winner → always 2 encodes
- **Files**: `src/mame/util/chdcodec.cpp` — `chd_flac_compressor::compress()`
- **Ref**: chdman-simd patch `04_chdcodec_flac_lzma`

#### Step 5: CRC16 slice-by-16

- **Impact**: −57% CPU on CRC16 (mandatory per-hunk in verify)
- **Effort**: MEDIUM
- **Current**: Byte-by-byte table lookup in `crc16_creator::append()`
- **Fix**: 16-byte-at-a-time slice-by-16 algorithm (larger tables ~34 KB, no SIMD)
- **Files**: `src/mame/util/hashing.cpp`
- **Ref**: chdman-simd patch `03_hashing_simd`

#### Step 6: Sequential read hints (cross-platform)

- **Impact**: ~3% elapsed time (I/O prefetch)
- **Effort**: LOW
- **Per-OS**:
  - Windows: `FILE_FLAG_SEQUENTIAL_SCAN` in `CreateFile`
  - Linux: `posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL)`
  - macOS: `fcntl(fd, F_RDAHEAD, 1)`
- **Files**: `src/mame/util/chd.cpp` or CHDlite's file open wrapper

---

### Phase 3 — SIMD Hashing (HIGH impact, HIGHER effort)

#### Step 7: SHA1 SIMD dispatch

- **Impact**: SHA1 drops from hotspot to 7.3% CPU (SSE2), ~2% (SHA-NI)
- **Effort**: HIGH
- **Runtime dispatch**:
  - Rounds: scalar → SSE2 → SHA-NI (Ice Lake+/Zen+)
  - Byte-swap: scalar → SSSE3 → AVX2
  - ARM64: NEON SHA1 intrinsics (Apple Silicon has hardware SHA)
- **Compile**: SHA-NI file compiled with `-msha -msse4.1`, dispatched by CPUID at runtime
- **Files**: `src/mame/util/hashing.cpp`
- **Ref**: chdman-simd patch `03_hashing_simd`

#### Step 8: SHA256 SIMD (future / lower priority)

- **Impact**: Lower priority — SHA256 is optional in CHD format
- **SHA-NI includes SHA256 instructions** (`_mm_sha256rnds2_epu32`, etc.)
- **ARM Cryptographic Extensions** include SHA256
- **Files**: `src/mame/util/hashing.cpp`

---

### Phase 4 — Build System (CRITICAL prerequisite for SIMD)

#### Step 9: CMakeLists.txt SIMD compile flags

- **Impact**: Prerequisite for Steps 2, 7, 8, 13 (xxHash, SHA, byte-swap)
- **Effort**: LOW–MEDIUM
- **Add**:
  ```cmake
  # x86-64 with AVX2 baseline
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_options(-mavx2 -msse4.2 -mssse3)
    # Or: -march=x86-64-v3 for AVX2 baseline level
  endif()
  # ARM64: NEON + crypto implicit on aarch64 (no flag needed)
  ```
- **SHA-NI dispatch**: Compile SHA-NI sources with `-msha -msse4.1` separately, CPUID dispatch at runtime
- **Files**: `CMakeLists.txt`

---

### Phase 5 — Pipeline & Parallelism (HIGH impact, HIGHER effort)

#### Step 10: Per-file pipeline optimization (chdman-simd approach)

CHDlite's `chd_file_compressor` already has 3 stages (Read → Compress → Write) with multi-threaded compression (`osd_work_queue`, up to 16 workers). However:

- Hash (CRC16 + SHA1) is computed in **worker threads** with cold L1/L2 cache
- Write stage is hunk-by-hunk sequential
- No explicit triple-buffer slot rotation

##### Step 10a — Deferred hash computation (quick win)

- **Impact**: ~5–10% per-file speedup
- **Effort**: LOW (~50 LOC)
- **Change**: Move CRC16+SHA1 from `async_compress_hunk()` to main-thread consume loop. Data is still hot in L1/L2 after decompression completes.
- **Files**: `src/mame/util/chd.cpp` — `compress_continue()`, `async_compress_hunk()`

##### Step 10b — N_SLOTS=3 triple-buffer pipeline

- **Impact**: ~15–25% per-file speedup
- **Effort**: MEDIUM (~200 LOC)
- **Change**: Explicit 3-slot rotation — seed 2 slots, consume slot N while slots N+1 and N+2 decompress/compress in background. Mirrors chdman-simd `01_chdman_pipeline` patch.
- **Current buffer**: implicit circular 256-hunk buffer
- **New**: `batch_set[3]`, each with 1024 hunks, 64 work items (16 hunks/item)
- **Global hunk cursor** for SHA1 ordering (no special ordering enforcement needed)
- **Files**: `src/mame/util/chd.cpp`
- **Ref**: chdman-simd patch `01_chdman_pipeline`

Combined 10a + 10b: **20–35% single-file speedup**.

##### Cache analysis (L1 is not a concern)

| Datum | Size | Fits L1 (32 KB x86 / 128 KB M4P)? |
|-------|------|-------------------------------------|
| CD hunk | 19,584 B | ✅ Yes (61% / 15% of L1) |
| DVD hunk | 4,096 B | ✅ Yes (13% / 3%) |
| SHA1 block | 64 B | ✅ = 1 cache line |
| CRC16 slice-by-16 tables | ~34 KB | ⚠️ Spills x86 L1 → L2 (256 KB). Fits M4 L1. |
| SHA1 state | 20 B | ✅ Trivial |

**Why deferred hash (10a) works**: Worker threads decompress a hunk on core A → data lands in core A's L1. Current code hashes on the same worker (good). But with pipeline overlap, main thread on core B hashes slot N while workers on cores A,C decompress slots N+1,N+2. The hunk data travels A's L1 → shared L2/L3 → B's L1. Since hunk ≤ 19.6 KB, it fits entirely in L2 (256 KB+) and prefetches into B's L1 during the sequential `sha1_creator::append()` pass. **Net: L2 hit, not RAM. Acceptable.**

N_SLOTS=3 working set: `3 × 19.6 KB = 59 KB` (CD) — fits in L2 with room to spare. No concern even for DVD (`3 × 4 KB = 12 KB`).

#### Step 11: Multi-file batch parallelism

- **Impact**: Linear scaling with file count (2–4× on typical 4-core)
- **Effort**: MEDIUM
- **Current**: `cmd_auto_batch()` processes files sequentially in a for-loop
- **Fix**: Thread pool (`std::async` or similar), each thread calls `cmd_auto()` per file
- **Limit**: 2–3 concurrent files to bound memory (each compressor allocates ~256-hunk buffers)
- **Thread safety**: CLI logging needs synchronization (spdlog is already thread-safe)
- **Files**: `src/cli/chdlite_cli.cpp` — `cmd_auto_batch()`

**Step 10 × Step 11 combined**: per-file pipeline (20–35%) × multi-file batch (2–4×) = **multiplicative speedup**.

---

### Phase 6 — ASM & Auto-vectorization

#### Step 12: LZMA ASM decoder (LzmaDecOpt.asm)

- **Impact**: −29% LZMA decode CPU
- **Effort**: MEDIUM
- **Requires**: UASM assembler (x86-64 only)
- **Installation**:
  - Windows (MSYS2): `pacman -S mingw-w64-x86_64-uasm`
  - Linux: build UASM from source ([github.com/AsmEd/UASM](https://github.com/AsmEd/UASM))
- **Integration**:
  - Download `LzmaDecOpt.asm` + `7zAsm.asm` from LZMA SDK (public domain)
  - Add to `src/3rdparty/lzma/Asm/`
  - Compile with UASM, define `Z7_LZMA_DEC_OPT`
  - `LzmaDec.c` already has `#ifndef Z7_LZMA_DEC_OPT` guard (line 18) — designed for this
- **CMake**: Conditional — if UASM found, use ASM; else C fallback
- **Risk**: MEDIUM — x86-64 only, but graceful fallback
- **Files**: `src/3rdparty/lzma/`, `CMakeLists.txt`

#### Step 13: Audio byte-swap auto-vectorization

- **Impact**: Cumulative for multi-track audio CDs (thousands of frames)
- **Effort**: ZERO (compile flag only)
- **How**: With `-mavx2`, GCC/Clang auto-vectorize `std::swap(buf[i], buf[i+1])` to `vpshufb`
- **Verify**: `objdump -d` after build to confirm vectorization
- **Fallback**: If compiler doesn't auto-vectorize, manual `_mm256_shuffle_epi8` (~5 lines)
- **Files**: `src/chd-api/chd_processor.cpp` line 114

---

## Libraries — No Action Needed

| Library | Status | Why |
|---------|--------|-----|
| **zstd** | Already optimized | Built-in SIMD: Huffman ASM decoding, sequence matching. vcpkg build includes platform optimizations. |
| **xxHash** | Optimized IF compiled with AVX2 | XXH3 is SIMD-vectorized. Just needs `-mavx2` flag (Step 9). |
| **FLAC (libflac)** | Minor gains | Recent libflac has LPC SIMD intrinsics. vcpkg version should include them. |

---

## Files Affected

| File | Steps |
|------|-------|
| `vcpkg.json` | 1 (zlib-ng) |
| `CMakeLists.txt` | 1, 2, 9, 12 |
| `src/mame/util/hashing.cpp` | 5, 7, 8 |
| `src/mame/util/chdcodec.cpp` | 3, 4 |
| `src/mame/util/chd.cpp` | 6, 10a, 10b |
| `src/mame/osd/osdsync.cpp` | 14 (remove 4-core cap) |
| `src/chd-api/chd_processor.cpp` | 13 |
| `src/cli/chdlite_cli.cpp` | 11, 14 (wire -np), 15 (batch budget) |
| `src/3rdparty/lzma/` | 12 |

---

## Verification

1. **Tests**: `./build/cli_test` — all 108 tests pass (Block 4 pre-existing known)
2. **Smoke**: `./build/smoke_test` — CHD read/write correctness
3. **Hash bit-identical**: Compare SHA1/CRC output before and after SIMD changes
4. **Benchmark**: Time `chdlite hash -hash all` on a large CHD, before/after each step
5. **Cross-platform**: Verify builds on macOS arm64 (current), then test Win/Linux x86-64
6. **Pipeline SIMD banner** (future): Print active paths on startup, similar to chdman-simd:
   ```
   [SHA1=SSE2+AVX2  inflate=AVX2  FLAC=AVX2+FMA  LZMA=ASM]
   ```

---

## Priority Order (recommended implementation sequence)

| Order | Step | Impact | Effort | Prerequisite |
|-------|------|--------|--------|-------------|
| 1 | **Step 9**: CMake SIMD flags | Prerequisite | LOW | — |
| 2 | **Step 14**: Remove 4-core cap, wire `-np` | Unlocks full CPU | LOW | — |
| 3 | **Step 1**: zlib → zlib-ng | −76% inflate CPU | LOW | — |
| 4 | **Step 3**: LZMA persistent encoder | Eliminates 1000+ alloc/free | MEDIUM | — |
| 5 | **Step 4**: FLAC 3→2 encode | +2.5% create speed | LOW–MEDIUM | — |
| 6 | **Step 5**: CRC16 slice-by-16 | −57% CRC16 CPU | MEDIUM | — |
| 7 | **Step 6**: Sequential read hints | ~3% elapsed | LOW | — |
| 8 | **Step 10a**: Deferred hash | +5–10% per-file | LOW | — |
| 9 | **Step 10b**: N_SLOTS=3 pipeline | +15–25% per-file | MEDIUM | Step 10a |
| 10 | **Step 7**: SHA1 SIMD dispatch | −93% SHA1 CPU | HIGH | Step 9 |
| 11 | **Step 15**: Batch thread budget | 2–4× batch speed | MEDIUM | Step 14 |
| 12 | **Step 12**: LZMA ASM decoder | −29% LZMA decode | MEDIUM | UASM |
| 13 | **Step 13**: Byte-swap vectorize | Auto with `-mavx2` | ZERO | Step 9 |
| 14 | **Step 8**: SHA256 SIMD | Future | HIGH | Step 9 |
| — | **Step 2**: xxHash AVX2 | Auto with `-mavx2` | ZERO | Step 9 |

---

## Thread Allocation Model

### Current Problems

| Problem | Detail |
|---------|--------|
| **4-core cap** | `osd_get_num_processors(heavy_mt=false)` → `min(cores, 4)`. MAME legacy. 10-core M4 or 16-core i7 only uses 3 workers. |
| **`-np` not wired** | CLI parses `--numprocessors` into `Args.num_processors` but never sets `osd_num_processors`. Dead code. |
| **Batch is sequential** | `cmd_auto_batch()` runs files one-by-one. Remaining cores idle. |
| **No thread budget** | Single file grabs `numprocs-1` threads regardless of batch context. |

### Design: Core-Aware Thread Budget

```
N_SLOTS      = 3          (always — triple-buffer for I/O overlap, not core-dependent)
WORK_MAX     = 16         (hard cap per-file work queue, existing MAME limit)

total        = hardware_concurrency()       // logical cores (includes HT/SMT)
```

#### Single-file mode

```
workers      = min(total - 1, WORK_MAX)     // all cores to one file
n_slots      = 3
```

#### Batch mode

```
OPTIMAL_WORKERS = 4       // sweet spot — diminishing returns above this per-file
MIN_WORKERS     = 2       // minimum for effective compression parallelism
MAX_FILES       = 4       // memory cap (each file holds ~256 hunks × hunk_bytes × 2 buffers)

concurrent   = clamp(total / OPTIMAL_WORKERS, 1, min(file_count, MAX_FILES))
workers/file = total / concurrent
n_slots      = 3          // per file
```

Each concurrent file gets its own `osd_work_queue` with `workers/file` threads + 1 I/O thread + main consume loop.

### Examples by CPU

| CPU | Logical Cores | Single-file workers | Batch: files × workers |
|-----|---------------|--------------------|-----------------------|
| **Apple M4 Pro** (12P+4E) | 16 | 15 | 4 × 4 |
| **Apple M4** (4P+6E) | 10 | 9 | 2 × 5 |
| **Intel i3-12100** (4P, HT) | 8 | 7 | 2 × 4 |
| **Intel i5-13600K** (6P+8E, HT) | 20 | 16 (cap) | 4 × 5 |
| **Intel i7-14700K** (8P+12E, HT) | 28 | 16 (cap) | 4 × 7 |
| **AMD Ryzen 7 7800X3D** (8C, SMT) | 16 | 15 | 4 × 4 |
| **AMD Ryzen 9 7950X** (16C, SMT) | 32 | 16 (cap) | 4 × 8 |
| **Intel Pentium** (2C/4T) | 4 | 3 | 1 × 3 (too few for batch) |
| **Intel Celeron** (2C/2T) | 2 | 1 | 1 × 1 |

### Memory Budget (secondary constraint)

Memory per concurrent file ≈ `WORK_BUFFER_HUNKS × hunk_bytes × 2` (work + compressed buffers):

| Media | Hunk Size | Buffers per file | 4 concurrent |
|-------|-----------|-----------------|--------------|
| CD (2048+sub) | 2,448 B | ~1.2 MB | ~5 MB |
| DVD | 65,536 B | ~33 MB | ~132 MB |
| BD / large | 524,288 B | ~268 MB | ~1 GB |

For CD/DVD batch: memory is not a constraint. For BD-size: limit to 2 concurrent.

```
// Pseudo-code for memory-aware cap
mem_per_file  = WORK_BUFFER_HUNKS * hunk_bytes * 2 + codec_overhead
mem_available = system_memory * 0.5    // conservative: use half of RAM
max_by_memory = mem_available / mem_per_file
concurrent    = min(concurrent, max_by_memory)
```

### Implementation Steps (new)

#### Step 14: Remove 4-core cap & wire `-np`

- **Effort**: LOW (~20 LOC)
- **Changes**:
  1. `osd_get_num_processors()` — remove `min(threads, 4)` cap (or always pass `heavy_mt=true`)
  2. Wire `Args.num_processors` → `osd_num_processors` global before compression starts
  3. Add `-j` alias for `--numprocessors` (familiar to make/cmake users)
- **Files**: `src/mame/osd/osdsync.cpp`, `src/cli/chdlite_cli.cpp`

#### Step 15: Batch thread budget allocator

- **Effort**: MEDIUM
- **Changes**:
  1. Detect `hardware_concurrency()` once at batch start
  2. Calculate `concurrent_files` and `workers_per_file` per model above
  3. Create thread pool (or `std::async` with semaphore) with `concurrent_files` slots
  4. Each slot calls `cmd_auto()` with its `workers_per_file` count passed via `osd_num_processors`
  5. Memory check for large hunk sizes → reduce concurrency if needed
- **Files**: `src/cli/chdlite_cli.cpp` — `cmd_auto_batch()`
- **Depends on**: Step 14 (wired `-np`)

### Combined Speedup Model

```
Single file:
  Step 10a (deferred hash)     → +5–10%
  Step 10b (N_SLOTS=3)         → +15–25%
  Step 14  (remove 4-core cap) → unlocks full CPU for compression
  Total single-file: ~30–40% faster

Batch (e.g., 20 CD images on 16-core CPU):
  Everything above per file    → 30–40% per file
  Step 15 (4× concurrent)     → 4× throughput
  Total batch: ~5–6× faster

  Previously: 20 files × sequential × 3-worker cap = very slow
  After:      20 files / 4 concurrent × 4 workers × pipeline = very fast
```

---

## Reference

- `References/chdman-simd/` — patches, OPTIMISATIONS.md, build scripts
- `References/chdman-simd/OPTIMISATIONS.md` — VTune measurements, code diffs, rationale
- `References/chdman-simd/patches/` — 5 patch files against MAME 0.286
