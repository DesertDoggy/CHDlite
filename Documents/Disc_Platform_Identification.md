# Disc Platform Identification Signatures

Byte-level identification details for auto-detecting which console platform a CD/DVD disc image belongs to. All offsets are within the decoded/cooked sector data (i.e., after CHD hunk decompression and sector extraction).

---

## Priority Order for Detection

Detection should be performed in this order to avoid false positives:

1. **3DO** — unique Opera filesystem header at sector 0 (no ISO 9660)
2. **Sega Mega CD** — "SEGADISCSYSTEM" at byte 0 of sector 0
3. **Sega Saturn** — "SEGA SEGASATURN " at byte 0 of sector 0
4. **Sega Dreamcast** — "SEGA SEGAKATANA " at byte 0 of sector 0 (GD-ROM track 3)
5. **PS3** — "PlayStation3" at byte 0 of sector 1
6. **PS1 / PS2** — Read `SYSTEM.CNF` from ISO 9660 filesystem:
   - `BOOT2` key → **PS2**
   - `BOOT` key → **PS1**
7. **PSP / UMD** — ISO 9660 with `PSP_GAME/PARAM.SFO` present
8. **Neo Geo CD** — ISO 9660 with `IPL.TXT` or `ABS.TXT` in root
9. **PC Engine CD** — ISO 9660 PVD System ID containing "PC ENGINE" (rare); or process-of-elimination for disc with audio+data tracks and no other match
10. **DVD-Video** — ISO 9660/UDF with `VIDEO_TS/VIDEO_TS.IFO` present

---

## 1. Sega Mega CD / Sega CD

| Property | Value |
|---|---|
| **Sector size** | 2048 (Mode 1) or 2352 (raw) |
| **Check location** | Sector 0, byte offset 0x000 |
| **Magic string** | `SEGADISCSYSTEM  ` (16 bytes, space-padded) |
| **Alternatives** | `SEGABOOTDISC    `, `SEGADISC        `, `SEGADATADISC    ` |

### Header Layout (sector 0, offset 0x000)

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0x000 | 16 | Hardware ID | `SEGADISCSYSTEM  ` (bootable), `SEGADATADISC    ` (non-bootable) |
| 0x010 | 16 | Volume name | Disc title (space-padded ASCII) |
| 0x020 | 16 | System name | `SEGA MEGA DRIVE ` or `SEGA GENESIS    ` |
| 0x030 | 16 | System version | |
| 0x100 | 16 | Domestic name | Japanese title |
| 0x110 | 16 | Overseas name | International title |
| 0x120 | 14 | Product code | e.g., `GM MK-4407 -00` |
| 0x180 | 16 | Region codes | `J` (Japan), `U` (USA), `E` (Europe) |
| 0x1F0 | 16 | Reserved | |
| 0x200 | 0x600 | Security code | Copy protection area (0x200–0x7FF) |

### Detection Code
```
byte[16] header = read_sector(0, offset=0x000, length=16);
if (starts_with(header, "SEGADISCSYSTEM") ||
    starts_with(header, "SEGABOOTDISC") ||
    starts_with(header, "SEGADISC") ||
    starts_with(header, "SEGADATADISC"))
    → Platform = Mega CD
```

### Secondary Validation
- Offset 0x020: Should contain `SEGA MEGA DRIVE` or `SEGA GENESIS`
- Offset 0x180: Region code characters `J`, `U`, `E`

---

## 2. Sega Saturn

| Property | Value |
|---|---|
| **Sector size** | 2048 (Mode 1) or 2352 (raw) |
| **Check location** | Sector 0 of first data track (IP.BIN), byte offset 0x000 |
| **Magic string** | `SEGA SEGASATURN ` (16 bytes, space-padded) |

### Header Layout (IP.BIN at sector 0)

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0x000 | 16 | Hardware ID | `SEGA SEGASATURN ` (exactly 16 bytes, trailing space) |
| 0x010 | 16 | Maker ID | e.g., `SEGA ENTERPRISES` or `SEGA TP T-series ` |
| 0x020 | 10 | Product number | e.g., `T-1001G   ` |
| 0x02A | 6 | Version | e.g., `V1.000` |
| 0x030 | 8 | Release date | YYYYMMDD format |
| 0x038 | 8 | Device info | `CD-1/1  ` (single disc), `CD-1/2  ` (multi-disc) |
| 0x040 | 10 | Area symbols | Compatible regions: `JTUBKAEL` (J=Japan, T=Asia, U=USA, B=Brazil, K=Korea, A=Asia PAL, E=Europe, L=Latin America) |
| 0x04A | 16 | Peripheral codes | Controller compatibility flags |
| 0x060 | 112 | Game title | ASCII game title (space-padded) |
| 0x0D0 | 16 | Reserved | |
| 0x0E0 | 16 | IP size/reserve | Initial program metadata |
| 0x0F0 | 16 | Stack/master M | Master SH2 stack pointer and entry addresses |
| 0x100 | varies | Boot code | Initial program (IP) executable code begins |

### Detection Code
```
byte[16] header = read_sector(0, offset=0x000, length=16);
if (memcmp(header, "SEGA SEGASATURN ", 16) == 0)
    → Platform = Saturn
```

### Secondary Validation
- Offset 0x010: Maker ID typically starts with `SEGA` or a T-series ID
- Offset 0x038: Should contain `CD-` prefix (e.g., `CD-1/1  `)
- Offset 0x040: Area codes from `JTUBKAEL`

---

## 3. Sega Dreamcast (GD-ROM)

| Property | Value |
|---|---|
| **Sector size** | 2048 (Mode 1) or 2352 (raw) |
| **Check location** | Sector 0 of last data track (track 3 in GD-ROM), byte offset 0x000 |
| **Magic string** | `SEGA SEGAKATANA ` (16 bytes, space-padded) |

### Important: GD-ROM Track Structure
GD-ROM discs have a unique multi-session layout:
- **Low-density area** (tracks 1-2): Standard CD-ROM density, audio warning track + small data
- **High-density area** (track 3+): GD-ROM density, where the game data lives

For **CHD images**, the CHD metadata describes the full disc layout. The IP.BIN header is at sector 0 of the high-density data track (track 3).

For single-track CHD images or converted ISOs, the header is at sector 0.

### Header Layout (IP.BIN at sector 0 of data track)

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0x000 | 16 | Hardware ID | `SEGA SEGAKATANA ` (exactly 16 bytes, trailing space) |
| 0x010 | 16 | Maker ID | `SEGA ENTERPRISES` |
| 0x020 | 16 | Device info | `GD-ROM1/1      ` or `GD-ROM1/2      ` (disc count) |
| 0x030 | 10 | Area symbols | `JUE` (Japan/USA/Europe regions) |
| 0x03A | 6 | Reserved | |
| 0x040 | 10 | Product number | e.g., `T-8101D   ` |
| 0x04A | 6 | Version | e.g., `V1.000` |
| 0x050 | 16 | Release date | e.g., `20001129    ` |
| 0x060 | 16 | Boot filename | `1ST_READ.BIN   ` (main executable) |
| 0x070 | 16 | Publisher | |
| 0x080 | 128 | Game title | ASCII game title (space-padded) |

### Detection Code
```
byte[16] header = read_sector(0, offset=0x000, length=16);  // or track 3 sector 0
if (memcmp(header, "SEGA SEGAKATANA ", 16) == 0)
    → Platform = Dreamcast
```

### Secondary Validation
- Offset 0x020: Should contain `GD-ROM` prefix
- Offset 0x060: Boot filename (usually `1ST_READ.BIN`)

---

## 4. PlayStation 1 (PS1 / PSX)

| Property | Value |
|---|---|
| **Sector size** | 2352 (Mode 2/XA raw), 2048 (cooked Mode 1) |
| **Check location** | ISO 9660 PVD at sector 16 + `SYSTEM.CNF` file |
| **Primary magic** | `SYSTEM.CNF` contains `BOOT = cdrom:\` |
| **PVD magic** | System ID = `PLAYSTATION` at PVD offset 0x008 |

### Identification Methods (in priority order)

#### Method 1: SYSTEM.CNF (most reliable)
Read `SYSTEM.CNF` from ISO 9660 filesystem:
```
BOOT = cdrom:\SCUS_942.28;1
VMODE = NTSC
```
- Key is **`BOOT`** (not `BOOT2` — that's PS2)
- Value format: `cdrom:\XXXX_XXX.XX;1` or `cdrom:XXXX_XXX.XX;1`
- Serial prefix identifies region:
  - `SCUS` / `SLUS` → USA
  - `SCES` / `SLES` → Europe
  - `SCPS` / `SLPS` → Japan
  - `SCAJ` → Asia

#### Method 2: ISO 9660 PVD (sector 16)
| PVD Offset | Size | Field | Expected Value |
|------------|------|-------|---------------|
| 0x001 | 5 | Standard ID | `CD001` |
| 0x008 | 32 | System ID | `PLAYSTATION` (space-padded to 32 bytes) |
| 0x028 | 32 | Volume ID | Game-specific |
| 0x23E | 128 | Application ID | `PLAYSTATION` |
| 0x400 | 8 | XA identifier | `CD-XA001` (identifies Mode 2 / XA disc) |

#### Method 3: License String (sector 4, raw mode)
At sector 4 (Mode 2 Form 1), look for:
- `"Licensed  by          Sony Computer Entertainment Inc."`
- `"Licensed  by          Sony Computer Entertainment Amer"` (USA)
- `"Licensed  by          Sony Computer Entertainment Euro"` (Europe)

#### Method 4: PS-X EXE Header
The boot executable (referenced in SYSTEM.CNF) starts with:
- Offset 0x000: `PS-X EXE` (8 bytes)

### Detection Code
```
// Method 1: Parse SYSTEM.CNF from ISO 9660
system_cnf = iso_read_file("SYSTEM.CNF");
if (system_cnf contains "BOOT =") AND NOT (system_cnf contains "BOOT2")
    → Platform = PS1

// Method 2: Fallback - check PVD
pvd = read_sector(16);
if (pvd[0x008..0x014] == "PLAYSTATION" &&
    pvd[0x400..0x408] == "CD-XA001")
    → Platform = PS1 (if no BOOT2 found)
```

---

## 5. PlayStation 2 (PS2)

| Property | Value |
|---|---|
| **Sector size** | 2048 (DVD Mode 1) or 2352 (CD raw) |
| **Check location** | `SYSTEM.CNF` from ISO 9660 filesystem |
| **Primary magic** | `SYSTEM.CNF` contains `BOOT2 = cdrom0:\` |
| **Media** | DVD-ROM (most games) or CD-ROM (some early titles) |

### SYSTEM.CNF Format
```
BOOT2 = cdrom0:\SLUS_200.62;1
VER = 1.00
VMODE = NTSC
```
- Key is **`BOOT2`** (distinguishes from PS1's `BOOT`)
- Value format: `cdrom0:\XXXX_XXX.XX;1`
- Serial prefix: same scheme as PS1 (`SLUS`, `SLES`, `SLPS`, `SCUS`, etc.)

### ISO 9660 PVD (sector 16)
| PVD Offset | Size | Field | Expected Value |
|------------|------|-------|---------------|
| 0x001 | 5 | Standard ID | `CD001` |
| 0x008 | 32 | System ID | `PLAYSTATION` (space-padded) |

### Detection Code (from PCSX2 source)
```
// Parse SYSTEM.CNF from ISO 9660
lines = iso_read_file("SYSTEM.CNF").split('\n');
for each line:
    parse key=value
    if (key == "BOOT2")
        → Platform = PS2 (CDVDDiscType::PS2Disc)
    if (key == "BOOT")
        → Platform = PS1 (CDVDDiscType::PS1Disc)
```

### Secondary Validation
- PS2 DVDs: single data track, 2048-byte sectors
- PS2 CDs: may have 2352-byte sectors with Mode 2 XA
- Serial format: `????_???.??*` matching pattern (from PCSX2)

---

## 6. PlayStation 3 (PS3)

| Property | Value |
|---|---|
| **Sector size** | 2048 (Blu-ray / DVD) |
| **Check location** | Sector 1 (offset 0x800), byte offset 0x000 |
| **Primary magic** | `PlayStation3` (12 bytes at sector 1 offset 0) |
| **Filesystem** | UDF |

### Detection Points
| Location | Field | Expected Value |
|----------|-------|---------------|
| Sector 1, offset 0x000 | Platform string | `PlayStation3` (12 bytes) |
| Sector 1, offset 0x00C | Disc ID | Game-specific identifier |
| Filesystem | Required file | `PS3_DISC.SFB` in root |
| Filesystem | Required dir | `PS3_GAME/` containing `PARAM.SFO` |

### Detection Code
```
byte[12] header = read_sector(1, offset=0x000, length=12);
if (memcmp(header, "PlayStation3", 12) == 0)
    → Platform = PS3
```

---

## 7. PSP / UMD

| Property | Value |
|---|---|
| **Sector size** | 2048 |
| **Physical media** | UMD (Universal Media Disc, 60mm, 1.8GB) |
| **Filesystem** | ISO 9660 |
| **Check location** | ISO 9660 filesystem structure |
| **Primary magic** | Directory `PSP_GAME/` with `PARAM.SFO` |

### Detection Methods

#### Method 1: Filesystem Structure (most reliable)
UMD discs have a mandatory directory layout:
```
UMD_DATA.BIN         ← disc metadata (game ID)
PSP_GAME/
  PARAM.SFO          ← game metadata (title, serial)
  SYSDIR/
    BOOT.BIN         ← main executable (encrypted EBOOT.BIN)
    EBOOT.BIN        ← decrypted executable
```

Check for existence of `PSP_GAME/PARAM.SFO` in the ISO 9660 directory tree.

#### Method 2: UMD_DATA.BIN Content
The `UMD_DATA.BIN` file in the root contains:
```
SERIAL_NUMBER|TITLE
```
e.g., `UCUS-98615|GAME_TITLE`

#### Method 3: PBP Container (digital distribution, not UMD disc)
PBP files (PSP downloadable games stored as ISO within PBP):
| Offset | Size | Field | Expected Value |
|--------|------|-------|---------------|
| 0x000 | 4 | Magic | `\x00PBP` (0x00504250) |
| 0x004 | 4 | Version | `\x00\x00\x01\x00` (v1.0) |

At the DATA.PSAR section (offset from PBP header):
| Offset | Size | Field | Expected Value |
|--------|------|-------|---------------|
| +0x000 | 12 | Magic | `PSISOIMG0000` (contains ISO image) |
| +0x400 | varies | Game ID | Serial number |

### Detection Code
```
// Primary: Check for PSP directory structure in ISO
if (iso_file_exists("PSP_GAME/PARAM.SFO"))
    → Platform = PSP

// Alternative: PBP container
byte[4] pbp_magic = read_bytes(0x000, 4);
if (pbp_magic == {0x00, 'P', 'B', 'P'})
    → Platform = PSP (PBP format)
```

---

## 8. 3DO Interactive Multiplayer

| Property | Value |
|---|---|
| **Sector size** | 2048 |
| **Filesystem** | Opera File System (NOT ISO 9660) |
| **Check location** | Sector 0, byte offset 0x000 |
| **Primary magic** | `0x01 0x5A 0x5A 0x5A 0x5A 0x5A` (record type + sync bytes) |

### Volume Header (Sector 0)

| Offset | Size | Field | Expected Value |
|--------|------|-------|---------------|
| 0x00 | 1 | Record type | `0x01` (Volume Header) |
| 0x01 | 5 | Sync bytes | `0x5A 0x5A 0x5A 0x5A 0x5A` |
| 0x06 | 1 | Structure version | `0x01` |
| 0x07 | 1 | Flags | Volume flags |
| 0x08 | 32 | Comment | ASCII volume comment (space-padded) |
| 0x28 | 32 | Label | ASCII volume label (space-padded) |
| 0x48 | 4 | Volume ID | Unique disc identifier |
| 0x4C | 4 | Block size | Block size in bytes (typically 2048 = `0x00000800`) |
| 0x50 | 4 | Block count | Total blocks on disc |
| 0x54 | 4 | Root dir ID | Root directory block location |
| 0x58 | 4 | Root dir blocks | Root directory block count |
| 0x5C | 4 | Root dir size | Root directory byte size |
| 0x60 | 4 | Last root copy | Last root directory block copy |

### Detection Code
```
byte[7] header = read_sector(0, offset=0x000, length=7);
if (header[0] == 0x01 &&
    header[1] == 0x5A && header[2] == 0x5A && header[3] == 0x5A &&
    header[4] == 0x5A && header[5] == 0x5A &&
    header[6] == 0x01)
    → Platform = 3DO
```

### Secondary Validation
- Offset 0x4C: Block size should be 2048 (`0x00000800`, big-endian)
- The disc does NOT have an ISO 9660 PVD at sector 16

### Important Note
3DO uses **big-endian** byte order for multi-byte fields. The Opera filesystem is entirely custom — no ISO 9660, no UDF.

---

## 9. Neo Geo CD

| Property | Value |
|---|---|
| **Sector size** | 2048 (Mode 1) or 2352 (raw) |
| **Filesystem** | ISO 9660 |
| **Check location** | ISO 9660 filesystem — check for `IPL.TXT` |
| **Primary magic** | Presence of `IPL.TXT` (boot script) in root directory |

### Identification Methods

#### Method 1: Boot Script Presence (most reliable)
Neo Geo CD games always contain `IPL.TXT` in the root directory. This file is the Initial Program Loader script that tells the BIOS what to load:
```
ICON_.PCK,0,0
FIX_.PCK,0,0
SPR_.PCK,0,0
Z80_.PCK,0,0
PCM_.PCK,0,0
PRG_.PCK,0,0
```

Also check for `ABS.TXT` (abstract file) or `BIB.TXT` (bibliography file) which are common.

#### Method 2: Copyright String
Some Neo Geo CD discs contain identifiable strings in the boot data:
- `"SNK"` or `"SNK CORPORATION"` in early sectors
- ISO 9660 PVD Publisher ID may contain `SNK`

#### Method 3: ISO 9660 PVD System ID
| PVD Offset | Size | Field | Possible Values |
|------------|------|-------|----------------|
| 0x008 | 32 | System ID | May be blank or generic |
| 0x028 | 32 | Volume ID | Game title |
| 0x12E | 128 | Publisher ID | May contain `SNK` |

#### Method 4: File Structure
Neo Geo CD games have a distinctive file naming convention:
- `.PRG` — 68000 program ROM
- `.FIX` — Fix layer tiles
- `.SPR` — Sprite data
- `.Z80` — Z80 sound program
- `.PCM` — ADPCM sound samples
- `.PCK` — Packed versions of the above

### Detection Code
```
// Primary: Check for Neo Geo CD boot script
if (iso_file_exists("IPL.TXT"))
    → Platform = Neo Geo CD

// Secondary: Check for distinctive Neo Geo file types
if (iso_file_exists("*.PRG") || iso_file_exists("*.FIX") || iso_file_exists("*.SPR"))
    → Platform = Neo Geo CD (probable)
```

---

## 10. PC Engine CD / TurboGrafx-CD

| Property | Value |
|---|---|
| **Sector size** | 2048 (Mode 1) or 2352 (raw) |
| **Filesystem** | Proprietary (most games) or rare ISO 9660 |
| **Check location** | TOC structure + sector 0/1 of data track |
| **Primary detection** | Process of elimination or PCE-specific boot structure |

### Identification Challenge
PC Engine CD-ROM² games do **not** have a standardized magic string at a fixed offset like Sega platforms. Detection relies on multiple heuristics.

### Method 1: TOC / Track Layout
PC Engine CD games have a characteristic disc layout:
- **Track 1**: Audio track (2-second warning message, required by CD-ROM² spec)
- **Track 2**: Data track (Mode 1, 2048 bytes/sector) — contains the game program
- **Tracks 3+**: Mix of audio and data tracks

Key indicator: First track is audio, second track is data.

### Method 2: Boot Sector Content (sector 0 of data track)
The data track (usually track 2) begins with the IPL (Initial Program Loader) which is loaded by the System Card BIOS. The first few sectors contain:
- Load address and execution entry point
- The actual program data

At sector 0 of the data track, some games have identifiable content, but there is no universal "PC Engine" magic at byte 0.

### Method 3: ISO 9660 PVD (if present)
A small number of PC Engine CD titles use ISO 9660. When present:
| PVD Offset | Field | Possible Values |
|------------|-------|----------------|
| 0x008 | System ID | May contain `PC ENGINE`, `NEC`, or be blank |
| 0x028 | Volume ID | Game title |

### Method 4: Process of Elimination (recommended for CHDlite)
After checking all other platforms, if the disc:
1. Has audio + data track layout (typical CD-ROM game)
2. Does NOT match any Sega header (no `SEGA` at byte 0)
3. Does NOT have `SYSTEM.CNF` (not PlayStation)
4. Does NOT have `PSP_GAME/` (not PSP)
5. Does NOT have Opera filesystem header (not 3DO)
6. Does NOT have `IPL.TXT` (not Neo Geo CD)
7. Does NOT have `VIDEO_TS/` (not DVD-Video)
8. Has small data size consistent with PCE (typically < 700MB)
9. Track 1 is audio, Track 2 is data

Then it may be a PC Engine CD title.

### Method 5: BRAM Init String (emulator reference)
The PC Engine System Card initializes backup RAM with the signature:
```
'H', 'U', 'B', 'M', 0x00, 0x88, 0x10, 0x80
```
This string appears in BRAM (battery RAM), **not** on the disc itself — but it confirms PCE hardware association.

### Detection Code
```
// Check for PCE-specific data track content
// After all other platform checks fail:
track_layout = get_toc();
if (track_layout[0].type == AUDIO &&
    track_layout[1].type == DATA &&
    no_other_platform_matched)
    → Platform = PC Engine CD (probable)

// Optional: Check for rare ISO 9660 with PCE system ID
pvd = read_sector(16);
if (pvd[0x008] contains "PC ENGINE" || pvd[0x008] contains "NEC")
    → Platform = PC Engine CD
```

---

## 11. DVD-Video

| Property | Value |
|---|---|
| **Sector size** | 2048 |
| **Filesystem** | UDF and/or ISO 9660 |
| **Check location** | ISO/UDF filesystem — check for `VIDEO_TS/` directory |
| **Primary magic** | Presence of `VIDEO_TS/VIDEO_TS.IFO` |

### Identification Methods

#### Method 1: Filesystem Structure (definitive)
All DVD-Video discs contain:
```
VIDEO_TS/
  VIDEO_TS.IFO    ← Video Manager (VMG) information file
  VIDEO_TS.VOB    ← Video Manager menus (optional)
  VIDEO_TS.BUP    ← Backup of VIDEO_TS.IFO
  VTS_01_0.IFO    ← Video Title Set 1 info
  VTS_01_0.VOB    ← Video Title Set 1 menus
  VTS_01_1.VOB    ← Video Title Set 1 data
  ...
```

#### Method 2: VCD/SVCD (bonus — from psx-spx research)
For Video CD / Super VCD:
| Location | Field | Expected Value |
|----------|-------|---------------|
| Sector 150 (00:04:00) | VCD Info | `VIDEO_CD` (VCD), `SUPERVCD` (SVCD), `HQ-VCD  ` (HQ-VCD) |
| PVD sector 16, offset 0x008 | System ID | `CD-RTOS CD-BRIDGE` |
| PVD sector 16, offset 0x400 | XA ID | `CD-XA001` |

### Detection Code
```
// DVD-Video
if (iso_file_exists("VIDEO_TS/VIDEO_TS.IFO") ||
    udf_file_exists("VIDEO_TS/VIDEO_TS.IFO"))
    → Platform = DVD-Video

// VCD/SVCD
byte[8] vcd = read_sector(150, offset=0x000, length=8);
if (vcd == "VIDEO_CD" || vcd == "SUPERVCD" || vcd == "HQ-VCD  ")
    → Platform = VCD/SVCD
```

---

## Quick Reference Table

| Platform | Sector | Offset | Magic Bytes / String | Sector Size | Method |
|----------|--------|--------|---------------------|-------------|--------|
| **3DO** | 0 | 0x00 | `01 5A 5A 5A 5A 5A 01` | 2048 | Opera filesystem header |
| **Mega CD** | 0 | 0x00 | `SEGADISCSYSTEM  ` | 2048/2352 | Fixed string |
| **Saturn** | 0 | 0x00 | `SEGA SEGASATURN ` | 2048/2352 | Fixed string (IP.BIN) |
| **Dreamcast** | 0* | 0x00 | `SEGA SEGAKATANA ` | 2048/2352 | Fixed string (IP.BIN, track 3) |
| **PS3** | 1 | 0x00 | `PlayStation3` | 2048 | Fixed string |
| **PS2** | — | — | `SYSTEM.CNF` → `BOOT2` | 2048 | ISO 9660 file parse |
| **PS1** | — | — | `SYSTEM.CNF` → `BOOT` | 2048/2352 | ISO 9660 file parse |
| **PSP** | — | — | `PSP_GAME/PARAM.SFO` | 2048 | ISO 9660 dir exists |
| **Neo Geo CD** | — | — | `IPL.TXT` in root | 2048/2352 | ISO 9660 file exists |
| **PC Engine** | — | — | (heuristic) | 2048/2352 | Track layout + elimination |
| **DVD-Video** | — | — | `VIDEO_TS/VIDEO_TS.IFO` | 2048 | ISO/UDF dir exists |

---

## Implementation Notes for CHDlite

### Reading Sectors from CHD
1. Read CHD metadata to determine track layout (number of tracks, types, sector sizes)
2. For each track, determine the sector size from CHD metadata:
   - `CHTR` metadata: contains `TYPE:MODE1`, `TYPE:MODE1_RAW`, `TYPE:MODE2_RAW`, etc.
   - For raw sectors (2352 bytes), extract the 2048-byte data portion:
     - Mode 1: data at offset 16 (after 12-byte sync + 4-byte header)
     - Mode 2 Form 1: data at offset 24 (after 12+4+8 subheader)
3. Read sector 0 of the first data track for Sega/3DO checks
4. Read sector 16 for ISO 9660 PVD checks
5. Parse ISO 9660 filesystem for file-based checks (SYSTEM.CNF, IPL.TXT, etc.)

### CHD Hunk → Sector Mapping
```
sector_offset_in_chd = track_data_offset + (sector_number * bytes_per_sector)
hunk_number = sector_offset_in_chd / hunk_size
offset_in_hunk = sector_offset_in_chd % hunk_size
```

### Handling Multi-Track CHDs (CD-ROM)
- CHD metadata (`CHCD` tag) describes the full disc layout
- First data track might not be track 1 (PC Engine: track 2 is typically data)
- For Mega CD / Saturn: data track is typically track 1
- For Dreamcast GD-ROM: data is in track 3 (high-density area)
- Use CHD track metadata to find the correct data track before checking headers

### Sources
- **Dreamcast**: SEGA technical documentation (IP.BIN format, Katana SDK)
- **PS1/PS2**: PCSX2 source code (`CDVD.cpp` — `GetPS2ElfName()` function); psx-spx documentation
- **PS3**: ps3devwiki documentation
- **Mega CD**: SEGA development documentation; segaretro.org
- **Saturn**: SEGA Disc Format Standards Specification (ST-040-R4-051795); same header format as Dreamcast
- **3DO**: Opera File System documentation (FreeDO/Opera emulator source)
- **Neo Geo CD**: neocd_libretro emulator source; Neo Geo development resources
- **PC Engine CD**: MAME pce_cd.cpp; Mednafen PCE emulator source
- **PSP**: PSP homebrew documentation; PPSSPP emulator source
- **DVD-Video**: DVD Forum specifications (ECMA-267)
