# CHDlite TODO

## Read Functionality
- [ ] **Extend read to support non-CHD files**
  - Currently: CHD files only
  - Add support for: ISO images, CD images, etc.
  - Use auto `platform` detect to distinguish file types
  - Auto-detect should work on raw/non-CHD files too

## Terminology Change: "system" → "platform"
- [ ] **Rename for consistency with main app**
  - Update codebase: `detect_system` → `detect_platform`
  - Update API: functions/variables using "system" → "platform"
  - Reasoning: "platform" is clearer (refers to game console)
  - "system" is ambiguous (could mean OS/computer)
  - This matches main application terminology

## macOS
- [ ] **Drag and Drop not working** - Needs simple GUI
  - CLI mode only currently
  - Implement basic drag/drop file handling to accept CHD files
  - Consider lightweight UI framework or native macOS integration

## Windows
- [ ] **Drag and Drop IS working** - BUT terminal closes automatically
  - Results/hash output becomes invisible without result/log output
  - Need to:
    - Persist terminal window after execution
    - Save output to log file
    - Or display results in GUI overlay/output window
    - Or use `pause` command to keep window open until user input

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
