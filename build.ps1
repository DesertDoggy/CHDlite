#!/usr/bin/env pwsh
# CHDlite clean build script for Windows (Clang + Ninja + vcpkg x64-windows-static)
#
# Usage:
#   .\build.ps1              # clean build
#   .\build.ps1 -Jobs 8      # parallel jobs (default: 4)
#   .\build.ps1 -NoClear     # incremental (skip clean step)

param(
    [int]   $Jobs    = 4,
    [switch]$NoClear
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root      = $PSScriptRoot
$BuildDir  = Join-Path $Root "build"
$CMake     = "C:\CMake\bin\cmake.exe"
$Ninja     = (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter "ninja.exe" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
$RC        = "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe"
$Toolchain = "C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
$Overlays  = "$Root/vcpkg-overlays" -replace '\\', '/'

if (-not $Ninja) { throw "ninja.exe not found under LOCALAPPDATA\Microsoft\WinGet\Packages" }
if (-not (Test-Path $CMake)) { throw "cmake not found at $CMake" }

# --- 1. Clean ---
if (-not $NoClear -and (Test-Path $BuildDir)) {
    Write-Host "==> Cleaning build directory..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $BuildDir
}

# --- 2. Configure (installs vcpkg packages on first run) ---
Write-Host "==> Configuring..." -ForegroundColor Cyan
$cmakeArgs = @(
    "-B", $BuildDir, "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$Ninja",
    "-DCMAKE_C_COMPILER=C:/LLVM/bin/clang.exe",
    "-DCMAKE_CXX_COMPILER=C:/LLVM/bin/clang++.exe",
    "-DCMAKE_RC_COMPILER=$RC",
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DVCPKG_OVERLAY_PORTS=$Overlays",
    "-DCMAKE_BUILD_TYPE=Release",
    "-S", $Root
)
if ($NoClear -and (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    # Incremental: skip full configure, just re-run cmake to pick up CMakeLists changes
    & $CMake -B $BuildDir
} else {
    & $CMake @cmakeArgs
}
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

# --- 3. Disable vcpkg manifest re-install to prevent RERUN_CMAKE loop ---
#
# vcpkg.cmake uses CMAKE_DEPENDENT_OPTION which forces VCPKG_MANIFEST_INSTALL=ON
# during configure. After packages are installed we flip it OFF so that
# ninja's RERUN_CMAKE step does NOT trigger vcpkg again (vcpkg touches
# vcpkg_installed/** files which are listed as build.ninja deps, causing an
# infinite re-configure loop).
$cache = Join-Path $BuildDir "CMakeCache.txt"
(Get-Content $cache -Raw) -replace 'VCPKG_MANIFEST_INSTALL:BOOL=ON', 'VCPKG_MANIFEST_INSTALL:BOOL=OFF' |
    Set-Content $cache -NoNewline
Write-Host "    VCPKG_MANIFEST_INSTALL patched to OFF" -ForegroundColor DarkGray

# --- 4. Build ---
Write-Host "==> Building (jobs=$Jobs)..." -ForegroundColor Cyan
& $Ninja -C $BuildDir -j $Jobs
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }

Write-Host "`n==> Build complete. Binaries in Release\bin\" -ForegroundColor Green
