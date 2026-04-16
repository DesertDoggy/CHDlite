#!/usr/bin/env bash
# codec_decomp_benchmark.sh — Benchmark decompression speed for all CHDs produced by
# codec_benchmark.sh.
#
# Uses `chdlite hash` which reads and fully decompresses every hunk without writing
# any output files, isolating decompression from disk-write overhead.
#
# Input:   test_root/Roms/codec_testing/<Game>/<codec>/game.chd
# Output:  test_root/Roms/codec_testing/decomp_results.tsv
#
# Usage: ./scripts/codec_decomp_benchmark.sh [--runs N] [--flush-cache]
#   --runs N       number of timed runs per CHD (default: 3)
#   --flush-cache  attempt to flush the OS disk cache between runs (requires sudo on macOS,
#                  root on Linux — will prompt if not already elevated)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CLI="$ROOT/build/chdlite"
OUT_ROOT="$ROOT/test_root/Roms/codec_testing"
RUNS=3
FLUSH_CACHE=0

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --runs)        RUNS="$2"; shift 2 ;;
        --flush-cache) FLUSH_CACHE=1; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ ! -x "$CLI" ]]; then
    echo "ERROR: $CLI not found or not executable. Run cmake --build build first."
    exit 1
fi

if [[ ! -d "$OUT_ROOT" ]]; then
    echo "ERROR: $OUT_ROOT not found. Run codec_benchmark.sh first to create CHDs."
    exit 1
fi

# ─────────────────────────────────────────────
# Helper: flush OS disk cache
flush_cache() {
    if [[ "$FLUSH_CACHE" -eq 0 ]]; then return; fi
    local os
    os="$(uname)"
    if [[ "$os" == "Darwin" ]]; then
        sudo purge
    elif [[ "$os" == "Linux" ]]; then
        sync && sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
    else
        echo "  [warn] --flush-cache not supported on $os, skipping"
    fi
}

# Helper: get physical file size cross-platform
filesize() {
    local f="$1"
    if [[ "$(uname)" == "Darwin" ]]; then
        stat -f%z "$f"
    else
        stat -c%s "$f"
    fi
}

# ─────────────────────────────────────────────
RESULTS="$OUT_ROOT/decomp_results.tsv"
printf 'Game\tType\tCodec\tRun\tDecomp_s\tDecomp_MBps\n' > "$RESULTS"

echo ""
echo "Decompression benchmark  (runs per CHD: $RUNS, flush-cache: $FLUSH_CACHE)"
echo ""

TOTAL_CHDS=0

# Walk: codec_testing/<Game>/<codec>/game.chd
# Sort for reproducible order
while IFS= read -r chd; do
    rel="${chd#$OUT_ROOT/}"        # e.g.  PCEngine/cdzs_cdfl/game.chd
    game="${rel%%/*}"              # PCEngine
    rest="${rel#*/}"               # cdzs_cdfl/game.chd
    slug="${rest%%/*}"             # cdzs_cdfl
    codec="${slug//_/,}"           # cdzs,cdfl  (re-join underscore → comma)

    # Infer content type from game name
    case "$game" in
        PS2|PSP) ctype="dvd" ;;
        *)       ctype="cd"  ;;
    esac

    chd_size=$(filesize "$chd")

    echo "  $game / $slug  ($chd_size bytes)"

    run_times=()
    for ((run=1; run<=RUNS; run++)); do
        flush_cache

        t_start=$(date +%s%N)
        if "$CLI" hash "$chd" -hash sha1 -log none 2>/dev/null >/dev/null; then
            t_end=$(date +%s%N)
            elapsed=$(awk "BEGIN{printf \"%.3f\", ($t_end - $t_start) / 1e9}")
            mbps=$(awk "BEGIN{printf \"%.1f\", ($chd_size / 1048576) / ($t_end - $t_start) * 1e9}")
            printf '    run %d: %6.3fs  %6.1f MB/s\n' "$run" "$elapsed" "$mbps"
            printf '%s\t%s\t%s\t%d\t%s\t%s\n' \
                "$game" "$ctype" "$codec" "$run" "$elapsed" "$mbps" >> "$RESULTS"
            run_times+=("$elapsed")
        else
            printf '    run %d: FAILED\n' "$run"
            printf '%s\t%s\t%s\t%d\tFAILED\t-\n' "$game" "$ctype" "$codec" "$run" >> "$RESULTS"
        fi
    done

    # Print average if we have results
    if [[ ${#run_times[@]} -gt 0 ]]; then
        avg=$(awk "BEGIN{s=0; n=${#run_times[@]}; $(for t in "${run_times[@]}"; do echo "s+=$t;"; done) printf \"%.3f\", s/n}")
        avg_mbps=$(awk "BEGIN{printf \"%.1f\", ($chd_size / 1048576) / $avg}")
        printf '    avg:   %6.3fs  %6.1f MB/s\n' "$avg" "$avg_mbps"
        # Write average row (Run = 0 sentinel)
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$game" "$ctype" "$codec" "avg" "$avg" "$avg_mbps" >> "$RESULTS"
    fi
    echo ""
    TOTAL_CHDS=$((TOTAL_CHDS + 1))
done < <(find "$OUT_ROOT" -name "game.chd" | sort)

echo "════════════════════════════════════════"
echo "  Done. $TOTAL_CHDS CHDs benchmarked ($RUNS runs each)."
echo "  Results: $RESULTS"
echo "════════════════════════════════════════"
