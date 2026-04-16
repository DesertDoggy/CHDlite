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
- [ ] **Define codec strategy for auto-compression**
  - Current options:
    - **LZMA**: Best compression, slower
    - **FLAC**: Audio-specific, good for CD audio
    - **zstd**: Fast, decent compression
    - **Auto-select**: Not yet defined - needs testing/survey
  - Future work:
    - Benchmark codec performance on real CD images
    - Survey user preferences (speed vs compression)
    - Consider default: zstd for speed? LZMA for size?
    - Allow user override via flag `--codec`

# When using chdman commands need -i -o
-> make auto if not specified. (except -i ofcourse)