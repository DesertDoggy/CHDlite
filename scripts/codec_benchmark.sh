#!/usr/bin/env bash
# codec_benchmark.sh — Create CHDs for every codec combination across all 6 sample games
# and record compression time + output size.
#
# Output directory: test_root/Roms/codec_testing/<GameSlug>/<codec>/game.chd
# Result log:       test_root/Roms/codec_testing/results.tsv
#
# Usage: ./scripts/codec_benchmark.sh [--np N]
#   --np N   number of compression threads (default: 1 for fair timing comparison)
#            Pass --np 0 to use all cores.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLI="$ROOT/build/chdlite"
OUT_ROOT="$ROOT/test_root/Roms/codec_testing"
NP=1

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --np) NP="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ ! -x "$CLI" ]]; then
    echo "ERROR: $CLI not found or not executable. Run cmake --build build first."
    exit 1
fi

mkdir -p "$OUT_ROOT"
RESULTS="$OUT_ROOT/results.tsv"

# Write header
printf "Game\tType\tCodec\tCompress_s\tCompress_MBps\tSize_bytes\tRatio\n" > "$RESULTS"

# ─────────────────────────────────────────────
# Source file lookup (bash 3.2 compatible — no declare -A)
game_source() {
    case "$1" in
        PCEngine)  echo "$ROOT/test_root/Roms/DiscRomsChd/PCEngine/Dragon Half (Japan).cue" ;;
        PS1)       echo "$ROOT/test_root/Roms/DiscRomsChd/PS1/PoPoRoGue (Japan).cue" ;;
        Saturn)    echo "$ROOT/test_root/Roms/DiscRomsChd/Saturn/Sakura Taisen (Japan) (Disc 1) (6M).cue" ;;
        Dreamcast) echo "$ROOT/test_root/Roms/DiscRomsChd/Dreamcast/D2 - D no Shokutaku 2 (Japan) (Disc 1).gdi" ;;
        PS2)       echo "$ROOT/test_root/Roms/DiscRomsChd/PS2/Dragon Quest V - Tenkuu no Hanayome (Japan).iso" ;;
        PSP)       echo "$ROOT/test_root/Roms/DiscRomsChd/PSP/Final Fantasy IV Complete Collection - Final Fantasy IV & The After Years (Japan) (En,Ja,Fr).iso" ;;
    esac
}

# Content type lookup
game_ctype() {
    case "$1" in
        PS2|PSP) echo "dvd" ;;
        *)       echo "cd"  ;;
    esac
}

# CD codec sets to test (space-separated; each becomes one variant)
CD_CODECS=(
    "cdzs,cdfl"       # CHDlite default: ZSTD data + FLAC audio (fast decomp)
    "cdlz,cdzl,cdfl"  # chdman canonical default (best ratio)
    "cdlz,cdzl,cdzs,cdfl"  # all 4 slots
    "cdzs,cdzl,cdfl"  # ZSTD + ZLIB + FLAC (no LZMA)
    "cdfl"            # FLAC only
    "cdzs"            # ZSTD only (no FLAC — audio penalty)
    "cdlz"            # LZMA only
    "cdzl"            # ZLIB only
)

# DVD codec sets to test
DVD_CODECS=(
    "zstd"            # CHDlite default for PSP/generic
    "zlib"            # CHDlite default for PS2 / compat
    "lzma,zlib"       # chdman hd/raw default (max ratio)
)

# ─────────────────────────────────────────────
# Helper: get file size cross-platform (macOS stat differs from GNU)
filesize() {
    local f="$1"
    if [[ "$(uname)" == "Darwin" ]]; then
        stat -f%z "$f"
    else
        stat -c%s "$f"
    fi
}

# Helper: compute input size for ratio (sum of all source files for multi-bin CUE/GDI)
input_size() {
    local src="$1"
    local dir
    dir="$(dirname "$src")"
    local ext="${src##*.}"
    local total=0
    if [[ "$ext" == "cue" || "$ext" == "gdi" ]]; then
        # Sum all .bin/.raw in same directory
        while IFS= read -r -d '' f; do
            local sz
            sz="$(filesize "$f")"
            total=$((total + sz))
        done < <(find "$dir" \( -name "*.bin" -o -name "*.raw" -o -name "*.iso" \) -print0)
        # Also count the sheet itself
        total=$((total + $(filesize "$src")))
    else
        total=$(filesize "$src")
    fi
    echo "$total"
}

# ─────────────────────────────────────────────
# Main loop
TOTAL=0
for game in PCEngine PS1 Saturn Dreamcast PS2 PSP; do
    src="$(game_source "$game")"
    ctype="$(game_ctype "$game")"

    if [[ ! -f "$src" ]]; then
        echo "SKIP $game: source not found: $src"
        continue
    fi

    src_size=$(input_size "$src")
    echo ""
    echo "════════════════════════════════════════"
    ctype_upper=$(echo "$ctype" | tr '[:lower:]' '[:upper:]')
    echo "  $game  ($ctype_upper)  source: $src_size bytes"
    echo "════════════════════════════════════════"

    if [[ "$ctype" == "cd" ]]; then
        codecs=("${CD_CODECS[@]}")
    else
        # DVD: test all DVD codecs; also test all CD codecs on PS2 for completeness
        if [[ "$game" == "PS2" ]]; then
            codecs=("${DVD_CODECS[@]}" "${CD_CODECS[@]}")
        else
            codecs=("${DVD_CODECS[@]}")
        fi
    fi

    for codec in "${codecs[@]}"; do
        # Sanitise codec string for use as directory name
        slug="${codec//,/_}"

        out_dir="$OUT_ROOT/$game/$slug"
        mkdir -p "$out_dir"
        out_chd="$out_dir/game.chd"

        # Skip if already exists (re-run safe)
        if [[ -f "$out_chd" ]]; then
            sz=$(filesize "$out_chd")
            ratio=$(awk "BEGIN{printf \"%.3f\", $sz / $src_size}")
            echo "  [skip] $slug  ($sz bytes, ratio $ratio — already exists)"
            # Ensure it's in the results (re-add if results was deleted)
            grep -q "$(printf '%s\t%s\t%s\t' "$game" "$ctype" "$codec")" "$RESULTS" 2>/dev/null || \
                printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$game" "$ctype" "$codec" "-" "-" "$sz" "$ratio" >> "$RESULTS"
            TOTAL=$((TOTAL + 1))
            continue
        fi

        printf "  %-30s ... " "$slug"

        # Build compression flags
        if [[ "$codec" == "none" ]]; then
            comp_flag="-c none"
        else
            comp_flag="-c $codec"
        fi

        # Run archiver and time it
        t_start=$(date +%s%N)
        if "$CLI" create "$src" -o "$out_chd" $comp_flag -np "$NP" -f \
                -log none 2>/dev/null; then
            t_end=$(date +%s%N)
            elapsed=$(awk "BEGIN{printf \"%.2f\", ($t_end - $t_start) / 1e9}")
            sz=$(filesize "$out_chd")
            ratio=$(awk "BEGIN{printf \"%.3f\", $sz / $src_size}")
            mbps=$(awk "BEGIN{printf \"%.1f\", ($src_size / 1048576) / ($t_end - $t_start) * 1e9}")
            printf "done  %6.1fs  %6.1f MB/s  ratio %.3f\n" "$elapsed" "$mbps" "$ratio"
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$game" "$ctype" "$codec" "$elapsed" "$mbps" "$sz" "$ratio" >> "$RESULTS"
            TOTAL=$((TOTAL + 1))
        else
            printf "FAILED\n"
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                "$game" "$ctype" "$codec" "FAILED" "-" "-" "-" >> "$RESULTS"
        fi
    done
done

echo ""
echo "════════════════════════════════════════"
echo "  Done. $TOTAL CHDs created/verified."
echo "  Results: $RESULTS"
echo "════════════════════════════════════════"
