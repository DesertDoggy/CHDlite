# CHDlite TODO
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

## SIMD Optimizations & Performance Patches
- [x] **SIMD Optimization 1**: Replace zlib with zlib-ng
- [x] **SIMD Optimization 2**: xxHash AVX2 auto-dispatch
- [x] **SIMD Optimization 3**: LZMA encoder persistent instance
- [x] **SIMD Optimization 4**: FLAC 3→2 encode elimination
- [x] **SIMD Optimization 5**: CRC16 slice-by-16
- [x] **SIMD Optimization 6**: Sequential read hints (cross-platform)
- [x] **SIMD Optimization 7**: SHA1 SIMD dispatch (SSE2/SHA-NI)
- [ ] **SIMD Optimization 8**: SHA256 SIMD dispatch (future, lower priority)
- [x] **SIMD Optimization 9**: CMakeLists SIMD compile flags
- [x] **SIMD Optimization 10**: Per-file pipeline deferred hash
- [x] **SIMD Optimization 11**: N_SLOTS=3 triple-buffer pipeline
- [/] **SIMD Optimization 12**: LZMA ASM decoder (LzmaDecOpt.asm, requires UASM) Win OK.
- [x] **SIMD Optimization 13**: Audio byte-swap auto-vectorization
- [x] **SIMD Optimization 14**: Remove 4-core cap, wire `-np` flag
- [x] **SIMD Optimization 15**: Multi-file batch thread budget

  ### Uncompressed CHD (Low priority)
  - [ ] ~~Support `-c none`~~ — deliberately unsupported (speedpatch issues with uncompressed CHD)

## chdman Command Defaults n(low priority)
- [ ] **`createraw` `-us` warning** — warn + default to 512 when omitted (safe for speedpatch)
- [ ] **`createhd`** — keep as alias for createraw (no CHS/template/blank disk support)

### Deferred / Out of Scope
- [ ] **`listtemplates`** — 13 pre-defined HD geometries for MAME arcade hardware (niche, skip for now)
- [ ] **`createld` / `extractld`** — LaserDisc (AVI), extremely niche
- [ ] **`addmeta` / `delmeta`** — metadata write ops (special-purpose)

