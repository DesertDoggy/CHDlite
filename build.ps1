#!/usr/bin/env pwsh
# CHDlite clean build script for Windows (Clang / MSVC + Ninja + vcpkg x64-windows-static)
#
# Usage:
#   .\build.ps1                    # build with Clang (default)
#   .\build.ps1 -Compiler msvc     # build with MSVC
#   .\build.ps1 -Compiler both     # build with both compilers
#   .\build.ps1 -Jobs 8            # parallel jobs (default: 4)
#   .\build.ps1 -NoClear           # incremental (skip clean step)

param(
    [ValidateSet("clang", "msvc", "both")]
    [string]$Compiler = "clang",
    [int]   $Jobs     = 4,
    [switch]$NoClear
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root      = $PSScriptRoot
$CMake     = "C:\CMake\bin\cmake.exe"
$Ninja     = (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter "ninja.exe" -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
$RC        = "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe"
$Toolchain = "C:/vcpkg/scripts/buildsystems/vcpkg.cmake"
$Overlays  = "$Root/vcpkg-overlays" -replace '\\', '/'
$VCVars    = "C:\VisualStudio\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if (-not $Ninja) { throw "ninja.exe not found under LOCALAPPDATA\Microsoft\WinGet\Packages" }
if (-not (Test-Path $CMake)) { throw "cmake not found at $CMake" }

# Determine which compilers to build
$compilers = switch ($Compiler) {
    "both"  { @("clang", "msvc") }
    default { @($Compiler) }
}

function Build-WithCompiler {
    param([string]$Comp)

    if ($Comp -eq "msvc" -and -not (Test-Path $VCVars)) {
        throw "vcvars64.bat not found at $VCVars – install VS Build Tools C++ workload"
    }

    $suffix    = $Comp            # "clang" or "msvc"
    $BuildDir  = Join-Path $Root "build_$suffix"
    $OutputDir = "$Root/Release_$suffix" -replace '\\', '/'

    Write-Host "`n===== Building with $($Comp.ToUpper()) =====" -ForegroundColor Yellow

    # --- 1. Clean ---
    if (-not $NoClear -and (Test-Path $BuildDir)) {
        Write-Host "==> Cleaning $BuildDir ..." -ForegroundColor Cyan
        Remove-Item -Recurse -Force $BuildDir
    }

    # --- 2. Configure ---
    Write-Host "==> Configuring ($Comp)..." -ForegroundColor Cyan
    $cmakeArgs = @(
        "-B", $BuildDir, "-G", "Ninja",
        "-DCMAKE_MAKE_PROGRAM=$Ninja",
        "-DCMAKE_RC_COMPILER=$RC",
        "-DCMAKE_TOOLCHAIN_FILE=$Toolchain",
        "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
        "-DVCPKG_OVERLAY_PORTS=$Overlays",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCHDLITE_OUTPUT_DIR=$OutputDir",
        "-S", $Root
    )

    if ($Comp -eq "clang") {
        $cmakeArgs += "-DCMAKE_C_COMPILER=C:/LLVM/bin/clang.exe"
        $cmakeArgs += "-DCMAKE_CXX_COMPILER=C:/LLVM/bin/clang++.exe"
    } else {
        # MSVC: run cmake inside a vcvars64 environment
        $cmakeArgs += "-DCMAKE_C_COMPILER=cl"
        $cmakeArgs += "-DCMAKE_CXX_COMPILER=cl"
    }

    if ($NoClear -and (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
        if ($Comp -eq "msvc") {
            cmd /c "`"$VCVars`" >nul 2>&1 && `"$CMake`" -B `"$BuildDir`" 2>&1"
        } else {
            & $CMake -B $BuildDir
        }
    } else {
        if ($Comp -eq "msvc") {
            $argStr = ($cmakeArgs | ForEach-Object { "`"$_`"" }) -join " "
            cmd /c "`"$VCVars`" >nul 2>&1 && `"$CMake`" $argStr 2>&1"
        } else {
            & $CMake @cmakeArgs
        }
    }
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $Comp (exit $LASTEXITCODE)" }

    # --- 3. Patch vcpkg manifest install ---
    $cache = Join-Path $BuildDir "CMakeCache.txt"
    (Get-Content $cache -Raw) -replace 'VCPKG_MANIFEST_INSTALL:BOOL=ON', 'VCPKG_MANIFEST_INSTALL:BOOL=OFF' |
        Set-Content $cache -NoNewline
    Write-Host "    VCPKG_MANIFEST_INSTALL patched to OFF" -ForegroundColor DarkGray

    # --- 4. Build ---
    Write-Host "==> Building $Comp (jobs=$Jobs)..." -ForegroundColor Cyan
    if ($Comp -eq "msvc") {
        cmd /c "`"$VCVars`" >nul 2>&1 && `"$Ninja`" -C `"$BuildDir`" -j $Jobs 2>&1"
    } else {
        & $Ninja -C $BuildDir -j $Jobs
    }
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $Comp (exit $LASTEXITCODE)" }

    Write-Host "==> $($Comp.ToUpper()) build complete. Binaries in Release_$suffix\bin\" -ForegroundColor Green
}

# --- Run builds ---
foreach ($c in $compilers) {
    Build-WithCompiler $c
}
