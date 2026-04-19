### Alpha testing
Basic functions are working but not enough testing yet.

# CHDlite
Basically a CHDman wrapper with added functions and drag&drop support. Priority on disc rom consoles.
 Read, hash, extract, and create CHD files for CD, DVD, GD-ROM, and raw disc formats.

Built on MAME's CHD/chdman implementation with a C++

## Acknowledgments

This project incorporates code and concepts from the following open-source projects:

- **[MAME](https://github.com/mamedev/mame)** — CHD/cdhman core library and codec implementations (BSD-3-Clause)
- **[chdman-simd](https://github.com/grouik1er-coder/chdman-simd)** — SIMD-optimized codec improvements (BSD-2-Clause)
- **[7-Zip](https://github.com/ip7z/7zip)** — LZMA ASM decoder (x86-64) (LGPL)


## License

**AGPL-3.0**
(This project incorporates CHD/cdhman code from [MAME](https://github.com/mamedev/mame), which is licensed under BSD-3-Clause. If for what ever miraculous reason the MAME development team wishes to use or incorporate code/ideas from CHDlite back into MAME, this project's license can be lowered, limiting it to the mamedev team, on notice.)

- **Read** CHD headers, tracks, and metadata
- **Hash** CHD content (SHA-1, MD5, CRC32, SHA-256, XXH3)
- **Extract** CHD to CUE/BIN, GDI, or raw image
- **Create** CHD from CUE/BIN, GDI, ISO, or raw image
- **Auto-detect** content type: CD-ROM, DVD, GD-ROM (Dreamcast), Hard Disk, Raw
- **System detection**: PS1, PS2, PSP, Saturn, Mega CD, 3DO, Dreamcast (PC Engine detection is WIP)
- **Drag-and-drop** support — drop files or folders onto the binary **WIP For the moment Windows only + read/hash is useless due to terminal closing on exit. (use cli for read/hash)
- **Batch processing** — process multiple files and folders in one invocation
- **chdman-compatible** commands for drop-in replacement workflows
- **Usable as a C++ static library** in other projects

## Downloads

Pre-built binaries for macOS, (Linux WIP) and Windows are available on the [Releases](https://github.com/DesertDoggy/CHDlite/releases) page.

Each release contains four binaries:

| Binary      | Default Behavior            | Purpose |
|-------------|-----------------------------|----------|
| `chdlite`   | Auto (extract or create)    | Main binary, speed-optimized |
| `chdread`   | Display CHD info            | Symlink to chdlite |
| `chdhash`   | Hash CHD content            | Symlink to chdlite |
| `chdcomp`   | Create with max compression | Symlink to chdlite, uses `--best` automatically |

**Windows:** All binaries are copies of the same executable with different defaults.  
**Mac/Linux:** `chdread`, `chdhash`, and `chdcomp` are symlinks to `chdlite`.

## Usage

Drag&Drop file, files, folder. (Windows tested.)

```
chdlite <command> [options]
```

### Generic commands

```
extract  <input.chd> [-o output] [options]    Extract CHD to disc image
create   <input>     [-o output.chd] [options] Create CHD from disc image
read     <input.chd> [options]                 Read CHD info (header + tracks)
hash     <input.chd> [-hash sha1,md5,...] [options] Hash CHD content
auto     <input>     [options]                 Auto: .chd → extract, else → create
```

### Compression defaults

CHDlite picks codecs automatically based on content type with priority on speed, unless emulator compatibility issues. You can override with `-c`.

| Content | Default | Reason |
|---------|---------|--------|
| CD / GD-ROM | `cdzs, cdfl` | ZSTD wins data sectors (fast decomp, strong ratio); FLAC wins audio sectors (lossless PCM). Two-slot competition — no wasted cycles testing codecs that can't win. |
| DVD — PS2 | `zlib` | Some Android PS2 emulators only support `zlib` for DVD CHDs. Using ZSTD would break compatibility on mobile. |
| DVD — PSP / generic | `zstd` | Fast decompression, especially when hashing CHD contents — which requires reading and decompressing every hunk. |

**`--best` preset**: `cdlz, cdzs, cdzl, cdfl` for CD / `zstd, lzma, zlib, huff` for DVD — maximises compression. slowest. Use `chdcomp` binary for automatic `--best` compression.
**`-c chdman` preset**: chdman default`cdlz, cdzl, cdfl` for CD / `lzma, zlib, huff` -usually maximises compression.  slow but faster than --best.

Single codec is faster than chdman due to simd optimization
Multi codec is about 0-10% slower than chdman due to unknown bottleneck probably due to adding architecture changes for simultaneous hashing etc.
If you have a preference for a codec, it is recommended to use single codec.

> Compatibility tests are limited. If you experience any issues with certain emulators, reports will be appreciated.


### chdman-compatible commands

```
createcd   -i <input> -o <output.chd>    Create CD CHD
createdvd  -i <input> -o <output.chd>    Create DVD CHD
createraw  -i <input> -o <output.chd>    Create raw CHD
extractcd  -i <input.chd> -o <output>    Extract CD from CHD
extractdvd -i <input.chd> -o <output>    Extract DVD from CHD
extractraw -i <input.chd> -o <output>    Extract raw from CHD
```

### Options

```
-i, --input <file>           Input file
-o, --output <file>          Output file
-f, --force                  Overwrite existing output
-c, --compression <codecs>   Compression: none, zlib, zstd, lzma, flac, cdzl, cdzs, cdlz, cdfl
-hs, --hunksize <bytes>      Hunk size in bytes
-us, --unitsize <bytes>      Unit size in bytes
-np, --numprocessors <n>     Number of compression threads
-ob, --outputbin <file>      Output bin filename template (%t = track#)
-sb, --splitbin              Split output into per-track bin files
--no-splitbin                Single bin file for all tracks
-hash <algorithms>           Hash algorithms: sha1, md5, crc32, sha256, xxh3
--result <on|off|format>     Pretty log output control (on/off, or for hash: text/json/lot/svg)
-log <level>                 Structured log level: debug, info, warning, error, critical, none
-v, --verbose                Verbose output
```

### Examples

```bash
# Read CHD info
chdlite read game.chd

# Hash a CHD
chdlite hash game.chd
chdlite hash game.chd -hash sha1,md5

# Extract CD image
chdlite extract game.chd -o game.cue
chdlite extractcd game.chd -o game.cue

# Extract with split bin files
chdlite extract game.chd -o game.cue -sb

# Create CHD from CUE/BIN
chdlite create game.cue -o game.chd
chdlite createcd game.cue -o game.chd

# Drag-and-drop style (auto-detect action)
chdlite game.chd              # extracts
chdlite game.cue              # creates CHD

# Batch process a folder
chdlite /path/to/chd/folder/
```

### chdman compatibility

CHDlite aims for full chdman compatibility with one exception:

- **Uncompressed CHD (`-c none`) is not supported.** Creating or working with uncompressed CHD files is not currently implemented. This is the only known chdman non-compliance.


