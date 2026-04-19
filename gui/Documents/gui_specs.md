# CHDlite Flutter GUI Specifications

## Overview
Flutter-based cross-platform GUI for CHDlite on macOS, Windows, and Linux. Simple drag-and-drop interface with 4 operation buttons (Read, Hash, Extract/Lite, Compress/Comp), live progress streaming, and comprehensive settings panel.

## Architecture

### Integration Model
- **Static Library**: CHDlite compiled as static lib (.a/.lib/.a)
- **C Wrapper Layer**: Exposes CHDlite C++ API as C functions for FFI
- **Dart FFI**: Direct function calls from Flutter to C wrapper
- **Background Processing**: Dart isolates for non-blocking operations
- **Progress Streaming**: C callbacks marshaled to Dart via SendPort

### Technology Stack
- **Flutter 3.x+** — Cross-platform UI framework
- **Dart FFI** — Foreign function interface for C interop
- **TOML** — Settings format (`toml_dart` package)
- **CMake** — Build system for native libraries (macos, windows, linux)

## GUI Design & Layout

### Home Screen
```
┌─────────────────────────────────────────────┐
│  CHDlite                     [⚙ Settings] [✕] │
├─────────────────────────────────────────────┤
│                                               │
│   ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐   │
│   │ Read │  │ Comp │  │ Lite │  │ Hash │   │
│   │      │  │      │  │      │  │      │   │
│   │(icon)│  │(icon)│  │(icon)│  │(icon)│   │
│   └──────┘  └──────┘  └──────┘  └──────┘   │
│                                               │
│   (drag → scale 125-150%)                   │
│   (drop → execute operation)                │
│                                               │
├─────────────────────────────────────────────┤
│ ┌─────────────────────────────────────────┐ │
│ │ Output results scroll here...           │ │
│ │ Errors in red, progress updates live    │ │
│ │ [Cancel] [Open in Editor]               │ │
│ └─────────────────────────────────────────┘ │
└─────────────────────────────────────────────┘
```

### Button Images (Left to Right)
1. **Read** — `gui/assets/CHD read Icon1.png`
2. **Comp** (Compress) — `gui/assets/CHDcomp Icon1.png`
3. **Lite** (Extract) — `gui/assets/Groove-Title.png`
4. **Hash** — `gui/assets/CHD hash Icon1.png`

### Interaction Behavior
- **Hover/Drag**: Icon scales to 125-150% (visual feedback)
- **Drop**: Execute operation with defaults + settings overrides
- **Multi-file Drop**: Process all files with same operation
- **Cancel Button**: Active during operation, stops in-flight task
- **Settings Button**: Opens settings screen
- **Close Button**: Exit application

### Output Display Widget
- Scrollable text area for live streaming output
- Progress bar showing percentage complete
- Error messages highlighted in red/yellow
- Warning messages in orange
- Success messages in green
- "Cancel" button (active during operation)
- "Open in Editor" button (for hash/.hashes files)

### Settings Screen
- Tab/drawer layout for all CLI options
- Sections:
  - **Output**: Extract output dir, Compress output dir, Hash output dir (default: Desktop)
  - **Compression**: Codec selection, hunk size, unit size, thread count
  - **Hash**: Algorithm checkboxes (SHA1, MD5, CRC32, SHA256, XXH3), output format (text/JSON)
  - **Extract**: Split bin files (toggle)
  - **UI**: Theme (light/dark), window size preferences
  - **Advanced**: Portable vs Install mode, log level, result format (text/JSON)

## Cross-Platform Behavior

### Output File Locations

#### Extract/Compress (Default: Same as Input)
- Override with settings: `output.extract_output_dir`, `output.compress_output_dir`
- Example: `input.cue` → `output.chd` in same directory

#### Hash Output (Default: Platform-Specific)
- **Windows**: `<exe_dir>/logs/` (portable behavior)
- **macOS**: `~/Desktop/` (preferred), fallback `~/.cache/chdlite/`
- **Linux**: `~/Desktop/` (if exists), fallback `~/.cache/chdlite/`
- Override with settings: `output.hash_output_dir`
- Format: `{input_filename}.hashes` (plain text)

#### Logs
- All logs in: `<logs_dir>/chdlite.log`
- Result log (appended): `<logs_dir>/result.log` (plain text, operation summary)
- Logs dir on first run:
  - Portable mode: `{exe_dir}/logs/`
  - Install mode: Platform-standard
    - Windows: `%APPDATA%/chdlite/logs/`
    - macOS: `~/Library/Application Support/chdlite/logs/`
    - Linux: `~/.cache/chdlite/logs/`

### Settings Storage

#### Portable Mode (Single Executable)
- `{exe_dir}/chdlite.toml` — Single config file next to executable
- Use case: USB drive, no system paths, no registry on Windows
- Cleanup: Delete exe + config file

#### Install Mode (System-Standard Locations)
- **Windows**: `%APPDATA%/chdlite/chdlite.toml` (no registry)
- **macOS**: `~/Library/Application Support/chdlite/chdlite.toml`
- **Linux**: `~/.config/chdlite/chdlite.toml` (follows XDG standard)

#### First-Run Detection
- On app launch, check if config exists
- If not found → Show dialog: "Portable or Install?"
  - **Portable**: Store config next to exe, no system paths
  - **Install**: Store in system-standard location
- Save choice to `[app] mode = "portable"|"install"` in TOML
- Remember for future runs

## Settings File Format (TOML)

### Structure
```toml
[app]
portable = true                    # true = portable mode, false = install mode
first_run = false                  # set to false after first-run dialog
mode = "portable"                  # "portable" or "install"
log_level = "info"                 # debug, info, warning, error, critical, none

[output]
extract_output_dir = ""            # empty = same as input
compress_output_dir = ""           # empty = same as input
hash_output_dir = "desktop"        # "desktop", "logs", or absolute path

[hash]
algorithms = ["sha1", "md5"]       # List of: sha1, md5, crc32, sha256, xxh3
output_format = "text"             # "text" or "json"

[compress]
codec = "zstd"                     # Single codec or comma-list: "zstd,flac,lzma,zlib"
hunk_size = 65536                  # Bytes per hunk
unit_size = 2048                   # Bytes per unit
threads = 0                        # 0 = auto-detect

[extract]
split_bin = false                  # true = per-track bin files, false = single bin

[read]
display_format = "terminal"        # "terminal", "widget", or "both"

[ui]
theme = "light"                    # "light" or "dark"
window_width = 1000
window_height = 700
window_x = 100
window_y = 100

[result]
format = "text"                    # "text" or "json" for result.log
```

## Operations & Behavior

### Read
- **Input**: CHD file (drag onto Read button)
- **Output**: Terminal display + GUI text widget
- **Settings Override**: Display format, log level
- **Result**: Header info, tracks, metadata summary
- **Multi-file**: Display info for first file only (or all sequentially)

### Hash
- **Input**: CHD file or disc image
- **Output**: `.hashes` file to hash_output_dir (default: Desktop)
- **Settings Override**: Algorithms, output format, hash_output_dir
- **Result**: Per-track hashes (SHA1, MD5, CRC32, SHA256, XXH3)
- **Multi-file**: Process each file, create separate `.hashes` for each

### Extract (Lite)
- **Input**: CHD file → outputs: CUE/BIN, GDI, or raw image
- **Output**: Disc image to extract_output_dir (default: same as input)
- **Settings Override**: Output dir, split bin files
- **Result**: Ready-to-use disc image (CUE+BIN or single file)
- **Multi-file**: Extract each CHD separately

### Compress (Comp)
- **Input**: CUE/BIN, GDI, ISO, or raw disc image → outputs: CHD
- **Output**: CHD file to compress_output_dir (default: same as input)
- **Settings Override**: Codec, hunk size, unit size, threads
- **Result**: Compressed CHD with metadata (platform detected)
- **Multi-file**: Create separate CHD for each input

## UX Decisions

### Progress & Streaming
- Live progress bar (0-100%) during operations
- Live output streaming to text widget (updates every 0.5-1s)
- Progress callback from C invoked periodically: `callback(stage, progress_pct, message_string)`
- FFI marshals callbacks to Dart via SendPort → UI updates
- Smooth scaling animation when dragging icons (125-150%)

### Threading & Cancellation
- All operations run in background Dart isolate (non-blocking UI)
- Cancel button active during operation
- Click Cancel → sets shared flag, C callback checks flag and aborts
- Cancellation status displayed in output widget
- UI remains responsive throughout

### Error Handling
- Errors displayed in output widget (red text)
- Warnings in yellow text
- Success messages in green
- Full error stack logged to `chdlite.log`
- Operation summary appended to `result.log` (timestamp, status, duration, message)
- No system popups/dialogs (all in-GUI)

### Multi-File Handling
- Drag multiple files → process all with same operation
- For read: Show info for first file (or all sequentially if user sets in settings)
- For hash/extract/compress: Create output for each input file
- Progress bar shows current file progress, overall progress in title bar
- Cancel affects all queued files

## Directory Structure

```
CHDlite/
├── src/                    # C++ source (existing, unchanged)
├── include/                # C++ headers (existing, unchanged)
├── build/                  # C++ CMake build output (existing, unchanged)
├── gui/
│   ├── flutter/            # Flutter app root
│   │   ├── lib/
│   │   │   ├── main.dart                      # App entry point
│   │   │   ├── screens/
│   │   │   │   ├── home_screen.dart           # 4 buttons + output widget
│   │   │   │   └── settings_screen.dart       # Settings panel
│   │   │   ├── widgets/
│   │   │   │   ├── icon_button.dart           # Draggable button (scale on drag)
│   │   │   │   ├── output_display.dart        # Text widget + open editor button
│   │   │   │   └── progress_indicator.dart    # Custom progress bar
│   │   │   └── services/
│   │   │       ├── chdlite_ffi.dart           # FFI bindings to C wrapper
│   │   │       ├── settings_manager.dart      # Load/save TOML
│   │   │       └── operation_handler.dart     # Isolate spawning, callbacks
│   │   ├── macos/
│   │   │   └── Runner.xcworkspace/
│   │   │       └── CMakeLists.txt             # macOS: Link CHDlite static lib
│   │   ├── windows/
│   │   │   └── CMakeLists.txt                 # Windows: Link CHDlite static lib
│   │   ├── linux/
│   │   │   └── CMakeLists.txt                 # Linux: Link CHDlite static lib
│   │   ├── pubspec.yaml                       # Flutter dependencies
│   │   └── .gitignore
│   ├── assets/
│   │   ├── CHD read Icon1.png                 # Read button
│   │   ├── CHDcomp Icon1.png                  # Compress button
│   │   ├── Groove-Title.png                   # Extract button
│   │   ├── CHD hash Icon1.png                 # Hash button
│   │   └── .gitkeep
│   └── Documents/
│       └── gui_specs.md                       # This file
│
├── src/c-wrapper/          # NEW: C wrapper for CHDlite C++ API
│   ├── CMakeLists.txt
│   ├── chdlite_c_wrapper.h
│   └── chdlite_c_wrapper.cpp
```

## C Wrapper API (New Module)

### Header: `src/c-wrapper/chdlite_c_wrapper.h`
```c
#ifndef CHDLITE_C_WRAPPER_H
#define CHDLITE_C_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*progress_callback_t)(
    const char* operation,    // "extract", "hash", "compress", "read"
    int progress_pct,         // 0-100
    const char* message       // Current status message
);

// Set progress callback for all operations
void chdlite_set_progress_callback(progress_callback_t cb);

// Cancel current operation (safe to call from any thread)
void chdlite_cancel_operation(void);

// Operations return JSON string (caller must free with chdlite_free_json)
char* chdlite_extract(
    const char* input,
    const char* output,
    const char* codec,        // NULL for auto-detect
    const char* options_json  // {"split_bin": false, ...}
);

char* chdlite_hash(
    const char* input,
    const char* algorithms,   // "sha1,md5,crc32,sha256,xxh3"
    const char* output_file,  // Where to write .hashes file
    const char* options_json  // {"format": "text", ...}
);

char* chdlite_compress(
    const char* input,
    const char* output,
    const char* codec,        // "zstd", "zstd,flac", etc.
    const char* options_json  // {"hunk_size": 65536, "threads": 0, ...}
);

char* chdlite_read(
    const char* input,
    const char* options_json  // {"format": "json", ...}
);

// Free returned JSON
void chdlite_free_json(char* json);

#ifdef __cplusplus
}
#endif

#endif
```

### Return Format (JSON)
```json
{
  "status": "success|error|cancelled",
  "operation": "hash|extract|compress|read",
  "input": "/path/to/input.chd",
  "output": "/path/to/output.hashes",
  "duration_ms": 12500,
  "message": "Completed 2 tracks",
  "progress": 100,
  "result": {
    "tracks": [...],
    "hashes": {...}
  }
}
```

## Implementation Phases

### Phase 1: Setup & C Wrapper (1-2 days)
1. Create `src/c-wrapper/` module with header + implementation
2. Implement C wrapper functions as thin adapters to CHDlite C++ API
3. Callback mechanism for progress reporting
4. Cancellation flag (atomic bool)
5. Unit test the C wrapper on all platforms

### Phase 2: CMake Integration (1 day)
1. Update root `CMakeLists.txt` to build C wrapper as dynamic lib (.dylib/.dll/.so)
2. Create `gui/app/macos/Runner.xcworkspace/CMakeLists.txt` → link dynamic lib
3. Create `gui/app/windows/CMakeLists.txt` → link dynamic lib
4. Create `gui/app/linux/CMakeLists.txt` → link dynamic lib
5. Verify builds on all 3 platforms

### Phase 3: Flutter Project Setup (1 day)
1. Create Flutter project at `gui/app/`
2. Set up `pubspec.yaml` with dependencies: `toml_dart`, `file_picker`, `path_provider`
3. Create folder structure: lib/screens/, lib/widgets/, lib/services/
4. Configure native plugin paths in Flutter build

### Phase 4: Dart FFI Bindings (1 day)
1. Create `lib/services/chdlite_ffi.dart` — load dynamic lib, bind C functions
2. Create `lib/services/operation_handler.dart` — Dart isolate spawning
3. Implement callback marshaling: C callback → SendPort → Dart UI updates
4. Test each bound function on all platforms

### Phase 5: Settings Manager (1 day)
1. Create `lib/services/settings_manager.dart` — load/save TOML
2. Implement first-run detection: Portable vs Install dialog
3. Platform-specific paths for config + logs
4. Default values + override logic

### Phase 6: Core GUI (2-3 days)
1. `main.dart` — App initialization, settings load, log dir creation
2. `screens/home_screen.dart` — 4 image buttons, drag-and-drop detection
3. `widgets/icon_button.dart` — Draggable button (scale on drag)
4. `widgets/output_display.dart` — Scrollable text + cancel + open editor
5. `widgets/progress_indicator.dart` — Custom progress bar
6. Wire up button clicks → FFI calls → output display

### Phase 7: Settings Screen (1-2 days)
1. `screens/settings_screen.dart` — Tab/drawer layout
2. All CLI options exposed as form controls
3. Save/load from TOML
4. Portable vs Install mode toggle (if needed)

### Phase 8: Polish & Testing (1-2 days)
1. Error handling edge cases
2. Cancel operation interruption
3. Multi-file handling validation
4. Cross-platform testing (macOS, Windows, Linux)
5. Package for distribution
6. macOS notarization (if distributing on App Store)

## Known Technical Considerations

### FFI & Isolates
- C callbacks cannot directly update Dart UI (different thread)
- Solution: Use Dart `SendPort` to marshal messages from isolate to main thread
- Each operation spawns isolate, isolate posts progress messages back

### Build System Complexity
- Flutter manages its own build system (Gradle/Xcode/CMake)
- Native libs must be built separately → linked into Flutter build
- Platform differences: macOS uses xcworkspace, Windows/Linux use CMake
- Solution: Separate CMakeLists.txt for each platform, pre-built dynamic lib in flutter plugins

### Drag-and-Drop
- macOS requires entitlements: `com.apple.security.files.user-selected.read-write`
- Windows: Use `win32` package for native drop zone
- Linux: Use `xdg_desktop_portal` or X11 drag-and-drop

### Cross-Platform Paths
- Always use `path_provider` package for platform-standard paths
- Windows: `getApplicationDocumentsDirectory()` → `%APPDATA%/chdlite/`
- macOS: `getApplicationSupportDirectory()` → `~/Library/Application Support/chdlite/`
- Linux: `getApplicationSupportDirectory()` → `~/.local/share/chdlite/` (XDG default)

### Testing
- Unit test C wrapper on all platforms (before GUI)
- Integration test FFI bindings (operation round-trip)
- GUI test on real devices/OS: macOS, Windows, Linux

## Dependencies (pubspec.yaml)

```yaml
dependencies:
  flutter:
    sdk: flutter
  toml_dart: ^0.3.0              # TOML config loading
  file_picker: ^5.0.0            # File/folder selection
  path_provider: ^2.0.0          # Platform-standard paths
  desktop_drop: ^0.3.0           # Drag-and-drop for desktop
  ffi: ^2.0.0                    # FFI support
```

## Deliverables

1. **C Wrapper Library** — `src/c-wrapper/chdlite_c_wrapper.{h,cpp}`
2. **Flutter App** — Complete `gui/app/` directory
3. **CMakeLists.txt** — Updated for all platforms (macos, windows, linux)
4. **Cross-platform Executables** — `chdlite-gui.app` (macOS), `chdlite-gui.exe` (Windows), `chdlite-gui` (Linux)
5. **Installer/Package** — Platform-specific (DMG, MSI, AppImage)

## Future Enhancements

- Batch queue (add multiple files, process sequentially)
- Drag-and-drop folder recursion (auto-find all CHDs)
- Comparison mode (compare two CHDs)
- Metadata editor
- Cloud sync (store settings + recent files)
- Auto-update feature
- Localization (multi-language UI)
