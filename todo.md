# CHDlite TODO

## Read Functionality
- [x] **Extend read to support non-CHD files**
  - Currently: CHD files only
  - Add support for: ISO images, CD images, etc.
  - Use auto `platform` detect to distinguish file types
  - Auto-detect should work on raw/non-CHD files too

## Terminology Change: "system" → "platform"
- [x] **Rename for consistency with main app**
  - Update codebase: `detect_system` → `detect_game_platform` (code), displayed as "Platform:"
  - Update API: functions/variables using "system" → "game_platform"
  - Reasoning: "platform" is clearer (refers to game console)
  - "system" is ambiguous (could mean OS/computer)
  - "game_platform" used in code to avoid confusion with OS platform
  - This matches main application terminology

## macOS
- [ ] **Drag and Drop** - Needs simple GUI (defer, time-consuming)
  - Build a minimal native macOS window/widget app to accept dropped files
  - Reuse same detection/hash/read logic from CLI
  - Share UI patterns with Windows widget once designed

## Windows — Drag and Drop (next priority)
- [ ] **`read` drag and drop**
  - Use `pause` to keep terminal open after output
  - Output: terminal only (no log file by default)
  - Behaviour: drop CHD/CUE/ISO → print Platform/Title/Manufacturer ID → pause

- [ ] **`hash` drag and drop**
  - Use `pause` to keep terminal open after output
  - Default: also write `.hashes` log file alongside input file
  - Behaviour: drop file(s) → compute hash(es) → print + save `.hashes` → pause

## CD Archiving Codec Selection
- [x] **Define codec strategy for auto-compression** — research complete, defaults confirmed by benchmark

  ### Research findings

  #### How the 4-slot system works
  - CHD V5 stores up to 4 codec types in the header. Each hunk is independently compressed
    with whichever of the 4 codecs produces the smallest result. This is a competition, not
    a pipeline — the encoder tries all slots and writes the winner + a 1-byte tag.
  - DVD/raw CHDs use generic codecs (LZMA, ZLIB, HUFF, FLAC).
  - CD/GD CHDs use CD-specific compound codecs (cdlz, cdzl, cdzs, cdfl).

  #### What the CD-specific codecs do internally (chdcodec.cpp)
  All CD codecs share the same pre-processing pipeline:
  1. Strip ECC/sync data from data sectors (saves ~280 bytes/sector, regenerated on decompress)
  2. Split each hunk into: `[sector_data_all | subcode_data_all]` (de-interleaved)
  3. Compress the two parts independently with two sub-codecs:
     - `cdlz` = sector data: LZMA  + subcode: ZLIB
     - `cdzl` = sector data: ZLIB  + subcode: ZLIB
     - `cdzs` = sector data: ZSTD  + subcode: ZSTD
     - `cdfl` = sector data: **FLAC** (treating raw PCM as audio) + subcode: ZLIB
  - FLAC is NOT limited to audio tracks. `cdfl` runs on every sector in the hunk regardless
    of track type. For audio tracks (raw PCM 16-bit stereo @ 44.1 kHz) it wins decisively.
    For data sectors it usually loses to LZMA/ZSTD (random-looking compressed data).

  #### chdman default codec set for CD/GD
  ```
  s_default_cd_compression = { CHD_CODEC_CD_LZMA, CHD_CODEC_CD_ZLIB, CHD_CODEC_CD_FLAC }
  ```
  Only 3 slots used (4th = 0/none). No `cdzs` in the chdman default.

  #### What our CHD samples actually use (from `chdlite read`)
  | File | Platform | Content | Compression |
  |------|----------|---------|-------------|
  | PS1 | CD-ROM 1 track data | `cdlz, cdzl, cdfl` |
  | Saturn | CD-ROM 2 tracks (1 audio) | `cdlz, cdzl, cdfl` |
  | | PC Engine | CD-ROM 22 tracks (20 audio) | `cdzs, cdfl` |
  | Dreamcast | GD-ROM 3 tracks (1 audio) | `cdzs, cdzl, cdfl` |
  | PS2 | DVD | `zlib` |
  | PSP | DVD | `zstd` |

  #### Benchmark results — confirmed (see Documents/Codec_Benchmark_Results.md)
  M4 MacBook Air, 4 threads, real disc images:
  - `cdzs,cdfl` achieves same ratio as `cdlz,cdzl,cdfl` on most games (0–3% worse),
    decompresses **2–3× faster** (48–79 MB/s vs 23–29 MB/s)
  - More slots = slower compression, no meaningful ratio gain (PCEngine: all 4 variants → ratio 0.503)
  - DVD: `zstd` = 138 MB/s decomp (PSP), `zlib` = 103 MB/s (PS2 safe choice, negligible ratio penalty)
  - `cdfl` alone is fast to decomp only because data sectors end up near-uncompressed (bad ratio)

  #### Decompression speed (affects emulators reading CHDs directly + `hash` speed)
  Fastest → slowest decompression:
  - **ZSTD** — fastest decompressor by design (asymmetric: slow compress, fast decompress)
  - **ZLIB** — fast, universally supported
  - **FLAC** — fast for true audio PCM; slower for data patterns
  - **LZMA** — slowest decompressor (symmetric: slow both ways, best ratio)

  #### Emulator compatibility
  - **PS2 (DVD)**: Android PS2 emulators only support `zlib` — use `zlib` for PS2
  - **PSP (DVD)**: PPSSPP modern builds support zstd
  - **CD/GD platforms**: most emulators use MAME libchd which supports all CD codecs

  #### CHDlite creation default strategy
  | Content | Default codec set | Rationale |
  |---------|-------------------|-----------|
  | CD-ROM / GD-ROM | `cdzs, cdfl` | ZSTD fastest for data hunks; FLAC wins audio; skip LZMA |
  | DVD — PS2 | `zlib` | Android PS2 emulator compatibility |
  | DVD — PSP / generic | `zstd` | Fast decomp (~139 MB/s), good ratio |
  | `--best` preset | `cdlz, cdzl, cdfl` (CD) / `lzma, zlib` (DVD) | Max ratio, slow decomp |

  Note: chdman default `cdlz, cdzl, cdfl` prioritises compression ratio over decomp speed.
  CHDlite differs intentionally — same ratio for most content, 3× faster reads.

  ### Tasks
  - [ ] Set CHDlite default CD codec to `cdzs, cdfl`
  - [ ] Set CHDlite default DVD codec to `zlib` for PS2, `zstd` for others (platform-aware)
  - [ ] Expose `--compression` override (already parsed, verify it flows through to archiver)
  - [ ] Add `--best` preset alias → `cdlz, cdzl, cdfl` (CD) / `lzma, zlib` (DVD)
  - [ ] Support `-c none` producing a truly uncompressed CHD (chdman-compatible `CHD_CODEC_NONE`)
        Currently `Codec::None` is the auto-detect sentinel — need a distinct `Codec::Uncompressed`
        (maps to CHD v5 codec tag `0x00000000`) so `-c none` stores raw hunks.

# When using chdman commands need -i -o, and depending on command some other options also.
-> make auto if not specified. (except -i ofcourse)