# CHDlite TODO

# Bin input
- Both cli, gui

# GUI app install mode
- Install mode

## SIMD Optimizations & Performance Patches
- [ ] **SIMD Optimization 8**: SHA256 SIMD dispatch (future, lower priority)
- [/] **SIMD Optimization 12**: LZMA ASM decoder (LzmaDecOpt.asm, requires UASM) Win OK.


  ### Uncompressed CHD (Low priority)
  - [ ] ~~Support `-c none`~~ — deliberately unsupported (speedpatch issues with uncompressed CHD)

## chdman Command Defaults n(low priority)
- [ ] **`createraw` `-us` warning** — warn + default to 512 when omitted (safe for speedpatch)
- [ ] **`createhd`** — keep as alias for createraw (no CHS/template/blank disk support)

### Deferred / Out of Scope
- [ ] **`listtemplates`** — 13 pre-defined HD geometries for MAME arcade hardware (niche, skip for now)
- [ ] **`createld` / `extractld`** — LaserDisc (AVI), extremely niche
- [ ] **`addmeta` / `delmeta`** — metadata write ops (special-purpose)

