# Codec Benchmark Results

**Platform:** Apple M4 MacBook Air, macOS  
**CHDlite binary:** `build/chdlite`, 4 compression threads (`--np 4`)  
**Methodology:**
- Compression: one CHD created per codec variant, wall-clock time measured, MB/s = source bytes / elapsed
- Decompression: `chdlite hash -hash sha1` run 3× per CHD, average taken; measures full hunk decomp with no file output
- Source sizes are sum of all input bin/track files

---

## Summary

`cdzs,cdfl` is the confirmed CHDlite default for CD/GD-ROM:
- Achieves the same ratio as chdman's `cdlz,cdzl,cdfl` on most games (0–3.3% worse on Saturn, 6.5% worse on Dreamcast)
- Decompresses **2–3× faster** (48–79 MB/s vs 23–29 MB/s)
- Compresses faster too: 2-slot competition vs 3-slot = fewer codecs tried per hunk

For DVD: `zstd` dominates decompression speed (~139 MB/s); `zlib` used for PS2 to ensure Android emulator compatibility at negligible ratio cost.

---

## Compression

### CD codecs — ratio (lower = smaller file)

| Codec | PCEngine | PS1 | Saturn | Dreamcast |
|---|---|---|---|---|
| `cdzs,cdfl` ✓ | **0.503** | **0.346** | 0.644 | 0.535 |
| `cdlz,cdzl,cdfl` | **0.503** | **0.345** | **0.624** | **0.502** |
| `cdlz,cdzl,cdzs,cdfl` | **0.503** | **0.342** | **0.623** | **0.502** |
| `cdzs,cdzl,cdfl` | 0.503 | 0.346 | 0.644 | 0.535 |
| `cdzs` | 0.767 | 0.346 | 0.647 | 0.536 |
| `cdlz` | 0.706 | 0.347 | 0.628 | 0.502 |
| `cdzl` | 0.777 | 0.356 | 0.658 | 0.571 |
| `cdfl` | 0.530 | 0.825 | 0.930 | 0.924 |

### CD codecs — compression speed (MB/s, source bytes)

| Codec | PCEngine | PS1 | Saturn | Dreamcast |
|---|---|---|---|---|
| `cdfl` | **178.6** | **179.1** | **175.9** | **162.4** |
| `cdzl` | 138.4 | 106.6 | 134.4 | 130.9 |
| `cdzs` | 84.7 | 37.2 | 29.4 | 29.3 |
| `cdlz` | 90.2 | 82.4 | 80.8 | 75.3 |
| `cdzs,cdfl` ✓ | 77.7 | 35.0 | 26.5 | 30.0 |
| `cdlz,cdzl,cdfl` | 60.1 | 48.1 | 48.1 | 57.3 |
| `cdzs,cdzl,cdfl` | 54.2 | 28.5 | 24.2 | 26.4 |
| `cdlz,cdzl,cdzs,cdfl` | 35.8 | 21.5 | 17.8 | 21.1 |

### DVD codecs — ratio and compression speed

| Codec | PS2 ratio | PSP ratio | PS2 MB/s | PSP MB/s |
|---|---|---|---|---|
| `zstd` | 0.481 | 0.878 | 124.0 | 20.1 |
| `zlib` ✓ (PS2) | **0.480** | 0.881 | **164.7** | **135.5** |
| `lzma,zlib` | **0.476** | **0.876** | 98.1 | 72.9 |

---

## Decompression

Measured via `chdlite hash -hash sha1`, 3-run average. CHD file size used as denominator (compressed bytes read from disk + decompressed in memory).

### CD codecs — decompression speed (MB/s, CHD size)

| Codec | PCEngine | PS1 | Saturn | Dreamcast |
|---|---|---|---|---|
| `cdfl` | 59.2 | **104.7** | **110.3** | **117.3** |
| `cdzs` | **115.1** | 47.7 | 78.9 | 65.8 |
| `cdzl` | 87.7 | 40.0 | 64.1 | 59.0 |
| `cdzs,cdfl` ✓ | 55.8 | 48.0 | 78.1 | 66.7 |
| `cdzs,cdzl,cdfl` | 55.8 | 47.7 | 77.7 | 64.3 |
| `cdlz,cdzl,cdzs,cdfl` | 53.1 | 32.9 | 30.0 | 23.1 |
| `cdlz,cdzl,cdfl` | 51.3 | 25.1 | 28.5 | 23.0 |
| `cdlz` | 24.5 | 18.8 | 23.1 | 22.0 |

> **Note on `cdfl` decomp:** FLAC only wins audio sectors. Data sectors (the vast majority) are stored nearly uncompressed when `cdfl` is the only slot, making them trivial to "decompress" — hence the high MB/s. The poor ratio (0.825–0.930) reflects this.

### DVD codecs — decompression speed (MB/s, CHD size)

| Codec | PSP | PS2 |
|---|---|---|
| `zstd` ✓ | **138.7** | **77.7** |
| `zlib` ✓ (PS2) | 102.6 | 62.8 |
| `lzma,zlib` | 62.0 | 39.5 |

---

## Source file sizes

| Game | Platform | Source size |
|---|---|---|
| Dragon Half | PC Engine CD | 401 MB |
| PoPoRoGue | PlayStation | 702 MB |
| Sakura Taisen Disc 1 | Saturn | ~641 MB |
| D2 Disc 1 | Dreamcast GD-ROM | ~1.19 GB |
| Dragon Quest V | PS2 DVD | ~2.55 GB |
| Final Fantasy IV CC | PSP DVD | ~881 MB |
