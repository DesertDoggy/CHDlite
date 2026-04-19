#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_TYPE="Release"
JOBS="0"
WITH_FLUTTER="0"

print_usage() {
  cat <<'EOF'
Usage: bash scripts/build_all_linux.sh [options]

Options:
  --build-dir <path>    Build directory (default: build)
  --type <Debug|Release|RelWithDebInfo|MinSizeRel>
                        CMake build type (default: Release)
  --jobs <n>            Parallel jobs for cmake --build (0 = auto, default: 0)
  --with-flutter        Also run Flutter Linux build in gui/app
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

echo "==> Done"
