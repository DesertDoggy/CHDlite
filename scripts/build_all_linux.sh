#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_TYPE="Release"
JOBS="0"
WITH_FLUTTER="0"
WITH_APPIMAGE="0"
APP_VERSION="0.2.1"

print_usage() {
  cat <<'EOF'
Usage: bash scripts/build_all_linux.sh [options]

Options:
  --build-dir <path>    Build directory (default: build)
  --type <Debug|Release|RelWithDebInfo|MinSizeRel>
                        CMake build type (default: Release)
  --jobs <n>            Parallel jobs for cmake --build (0 = auto, default: 0)
  --with-flutter        Also run Flutter Linux build in gui/app
  --appimage            Package Flutter bundle as AppImage (implies --with-flutter)
  --version <ver>       App version for AppImage filename (default: 0.2.1)
  -h, --help            Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --type)
      BUILD_TYPE="$2"
      shift 2
      ;;
    --jobs)
      JOBS="$2"
      shift 2
      ;;
    --with-flutter)
      WITH_FLUTTER="1"
      shift
      ;;
    --appimage)
      WITH_APPIMAGE="1"
      WITH_FLUTTER="1"
      shift
      ;;
    --version)
      APP_VERSION="$2"
      shift 2
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      print_usage
      exit 1
      ;;
  esac
done

if ! command -v cmake >/dev/null 2>&1; then
  echo "Error: cmake is not installed or not in PATH"
  exit 1
fi
if ! command -v c++ >/dev/null 2>&1; then
  echo "Error: c++ compiler is not installed or not in PATH"
  exit 1
fi

if [[ "$BUILD_DIR" != /* ]]; then
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR}"
fi

echo "==> Configuring CMake"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON

echo "==> Building all C++ targets (binaries, libs, ffi, tests)"
if [[ "$JOBS" == "0" ]]; then
  cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel
else
  cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"
fi

echo "==> Verifying FFI output"
FFI_SO="${ROOT_DIR}/gui/app/linux/lib/libchdlite_ffi.so"
if [[ -f "$FFI_SO" ]]; then
  ls -l "$FFI_SO"
else
  echo "Warning: $FFI_SO was not found"
fi

if [[ "$WITH_FLUTTER" == "1" ]]; then
  if ! command -v flutter >/dev/null 2>&1; then
    echo "Error: --with-flutter was set but flutter is not in PATH"
    exit 1
  fi

  echo "==> Building Flutter Linux app"
  pushd "$ROOT_DIR/gui/app" >/dev/null
  flutter pub get
  flutter build linux
  popd >/dev/null
fi

if [[ "$WITH_APPIMAGE" == "1" ]]; then
  APP_NAME="chdlite-${APP_VERSION}-linux-x64"
  BUNDLE="${ROOT_DIR}/gui/app/build/linux/x64/release/bundle"
  APPDIR="${ROOT_DIR}/build/${APP_NAME}.AppDir"
  APPIMAGE_OUT="${ROOT_DIR}/${APP_NAME}.AppImage"
  APPIMAGETOOL="/tmp/appimagetool-x86_64.AppImage"
  ICON_SRC="${ROOT_DIR}/gui/assets/CHDlite Icon1.png"

  echo "==> Packaging AppImage: ${APP_NAME}.AppImage"

  APPIMAGETOOL_EXTRACTED="/tmp/appimagetool-squashfs/AppRun"
  if [[ ! -f "$APPIMAGETOOL_EXTRACTED" ]]; then
    if [[ ! -f "$APPIMAGETOOL" ]]; then
      echo "    Downloading appimagetool..."
      curl -fsSL "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" \
        -o "$APPIMAGETOOL"
      chmod +x "$APPIMAGETOOL"
    fi
    echo "    Extracting appimagetool (no FUSE required)..."
    pushd /tmp >/dev/null
    "$APPIMAGETOOL" --appimage-extract >/dev/null 2>&1 || true
    mv /tmp/squashfs-root /tmp/appimagetool-squashfs
    popd >/dev/null
  fi
  APPIMAGETOOL="$APPIMAGETOOL_EXTRACTED"

  rm -rf "$APPDIR"
  mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" \
            "$APPDIR/usr/share/applications" \
            "$APPDIR/usr/share/icons/hicolor/256x256/apps"

  # Copy Flutter bundle
  cp -r "$BUNDLE/." "$APPDIR/usr/bin/"
  mv "$APPDIR/usr/bin/lib/"* "$APPDIR/usr/lib/"
  rmdir "$APPDIR/usr/bin/lib"

  # Icon
  if [[ -f "$ICON_SRC" ]]; then
    cp "$ICON_SRC" "$APPDIR/usr/share/icons/hicolor/256x256/apps/chdlite.png"
    cp "$ICON_SRC" "$APPDIR/chdlite.png"
  fi

  # .desktop
  cat > "$APPDIR/chdlite.desktop" <<EOF
[Desktop Entry]
Name=CHDlite
Exec=app
Icon=chdlite
Type=Application
Categories=Utility;
EOF
  cp "$APPDIR/chdlite.desktop" "$APPDIR/usr/share/applications/chdlite.desktop"

  # AppRun
  cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/app" "$@"
APPRUN
  chmod +x "$APPDIR/AppRun"

  ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$APPIMAGE_OUT"
  echo "==> AppImage: $APPIMAGE_OUT"
  ls -lh "$APPIMAGE_OUT"
fi

echo "==> Done"
