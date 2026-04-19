// license:GPLv3

#include "chd_frontend.hpp"
#include "chd_reader.hpp"
#include "cdrom.h"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace chdlite {

static Codec parse_codec(const std::string& s)
{
    if (s == "none")   return Codec::None;
    if (s == "zlib")   return Codec::Zlib;
    if (s == "zstd")   return Codec::Zstd;
    if (s == "lzma")   return Codec::LZMA;
    if (s == "flac")   return Codec::FLAC;
    if (s == "huff")   return Codec::Huffman;
    if (s == "huffman")return Codec::Huffman;
    if (s == "avhuff") return Codec::AVHUFF;
    if (s == "cdzl")   return Codec::CD_Zlib;
    if (s == "cdzs")   return Codec::CD_Zstd;
    if (s == "cdlz")   return Codec::CD_LZMA;
    if (s == "cdfl")   return Codec::CD_FLAC;
    return Codec::None;
}

static void fill_codecs(Codec (&dst)[4], Codec c0, Codec c1 = Codec::None,
                        Codec c2 = Codec::None, Codec c3 = Codec::None)
{
    dst[0] = c0;
    dst[1] = c1;
    dst[2] = c2;
    dst[3] = c3;
}

static std::string resolve_create_output(const std::string& input, const std::string& output)
{
    if (output.empty()) {
        fs::path p(input);
        return (p.parent_path() / p.stem()).string() + ".chd";
    }
    if (fs::is_directory(output) || output.back() == '/' || output.back() == '\\') {
        return (fs::path(output) / (fs::path(input).stem().string() + ".chd")).string();
    }
    return output;
}

bool apply_cli_compression_arg(const std::string& compression_arg,
                               ArchiveOptions& opts,
                               std::string& err)
{
    if (compression_arg.empty())
        return true;

    std::string s = compression_arg;
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (s == "none") {
        err = "uncompressed CHD (-c none) is not yet supported";
        return false;
    }
    if (s == "chdman") {
        err = "'-c chdman' was removed; use --chdman (or -chdman)";
        return false;
    }

    int slot = 0;
    size_t pos = 0;
    while (pos <= s.size()) {
        auto comma = s.find(',', pos);
        std::string tok = (comma == std::string::npos)
            ? s.substr(pos) : s.substr(pos, comma - pos);

        while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back() == ' ') tok.pop_back();

        if (tok.empty()) {
            err = "invalid compression list: empty codec token";
            return false;
        }
        if (tok == "none") {
            err = "codec 'none' cannot be mixed; use only -c none";
            return false;
        }

        Codec c = parse_codec(tok);
        if (c == Codec::None) {
            err = "unknown codec in -c/--compression: '" + tok + "'";
            return false;
        }
        if (slot >= 4) {
            err = "too many codecs in -c/--compression (max 4)";
            return false;
        }

        opts.compression[slot++] = c;

        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }

    return true;
}

bool compute_auto_compression_plan(const std::string& input_path,
                                   std::string& out_media_format,
                                   GamePlatform& out_platform,
                                   Codec (&out_codecs)[4])
{
    DetectionResult detection = detect_input(input_path, false);

    std::string fmt;
    std::string ext = fs::path(input_path).extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".cue" || ext == ".gdi" || ext == ".toc" || ext == ".nrg")
        fmt = "cd";
    else if (ext == ".iso" || ext == ".bin" || ext == ".img")
        fmt = detection.format.empty() ? "raw" : detection.format;
    else
        fmt = "raw";

    std::string smart_fmt = (ext == ".gdi") ? "gd" : fmt;
    out_media_format = smart_fmt;
    out_platform = detection.game_platform;

    if (out_platform == GamePlatform::PS2) {
        if (smart_fmt == "dvd")
            fill_codecs(out_codecs, Codec::Zlib);
        else
            fill_codecs(out_codecs, Codec::CD_Zlib, Codec::CD_FLAC);
        return true;
    }

    if (smart_fmt == "dvd") {
        fill_codecs(out_codecs, Codec::Zstd);
        return true;
    }
    if (smart_fmt == "cd" || smart_fmt == "gd") {
        fill_codecs(out_codecs, Codec::CD_Zstd, Codec::CD_FLAC);
        return true;
    }

    fill_codecs(out_codecs, Codec::LZMA, Codec::Zlib, Codec::Huffman, Codec::FLAC);
    return true;
}

FrontendCreateResult run_frontend_create(const FrontendCreateOptions& options)
{
    FrontendCreateResult out{};

    try {
        ArchiveOptions opts;
        opts.parent_chd_path = options.input_parent;
        opts.hunk_bytes = options.hunk_size;
        opts.unit_bytes = options.unit_size;
        opts.num_processors = options.num_processors;
        opts.detect_title = options.detect_title;
        opts.best = options.best;
        opts.chdman_compat = options.chdman_compat;
        opts.progress_callback = options.progress_callback;
        opts.log_callback = options.log_callback;

        if (!options.compression_arg.empty()) {
            std::string err;
            if (!apply_cli_compression_arg(options.compression_arg, opts, err)) {
                out.error_message = err;
                return out;
            }
        }

        out.resolved_output = resolve_create_output(options.input_path, options.output_path);
        compute_auto_compression_plan(options.input_path, out.detected_media, out.detected_platform, out.selected_codecs);

        // CD/GD creation requires hunk alignment to frame size. Legacy GUI settings
        // like 65536 can fail with "Invalid argument" for CUE/GDI inputs.
        if (opts.hunk_bytes > 0 && (out.detected_media == "cd" || out.detected_media == "gd")) {
            if (opts.hunk_bytes % cdrom_file::FRAME_SIZE != 0) {
                opts.hunk_bytes = 0; // auto
                if (options.log_callback)
                    options.log_callback(LogLevel::Warning,
                        "Invalid CD/GD hunk size detected; falling back to auto hunk size");
            }
        }

        ChdArchiver archiver;
        out.archive = archiver.archive(options.input_path, out.resolved_output, opts);
        out.success = out.archive.success;
        if (!out.success)
            out.error_message = out.archive.error_message;
    }
    catch (const std::exception& e) {
        out.success = false;
        out.error_message = e.what();
    }

    return out;
}

FrontendExtractResult run_frontend_extract(const FrontendExtractOptions& options)
{
    FrontendExtractResult out{};

    try {
        ExtractOptions opts;
        opts.parent_chd_path = options.input_parent;
        opts.force_overwrite = options.force_overwrite;
        opts.output_bin = options.output_bin;
        opts.input_start_byte = options.input_start_byte;
        opts.input_start_hunk = options.input_start_hunk;
        opts.input_bytes = options.input_bytes;
        opts.input_hunks = options.input_hunks;
        opts.input_start_frame = options.input_start_frame;
        opts.input_frames = options.input_frames;
        opts.hash = options.hash;
        opts.split_bin = options.split_bin;
        opts.progress_callback = options.progress_callback;
        opts.log_callback = options.log_callback;
        if (options.cue_style != CueStyle::Unmatched)
            opts.cue_style = options.cue_style;

        if (!options.output_path.empty()) {
            fs::path outp(options.output_path);
            if (fs::is_directory(outp) || options.output_path.back() == '/' || options.output_path.back() == '\\') {
                opts.output_dir = outp.string();
            } else {
                opts.output_dir = outp.parent_path().string();
                opts.output_filename = outp.filename().string();
                std::string ext = outp.extension().string();
                for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ext == ".bin") opts.force_raw = true;
            }
        } else {
            opts.output_dir = fs::path(options.input_path).parent_path().string();
        }

        ChdExtractor extractor;
        out.extract = extractor.extract(options.input_path, opts);
        out.success = out.extract.success;
        if (!out.success)
            out.error_message = out.extract.error_message;
    }
    catch (const std::exception& e) {
        out.success = false;
        out.error_message = e.what();
    }

    return out;
}

} // namespace chdlite
