import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

/// FFI bindings to the CHDlite C wrapper library.
///
/// The native dynamic library is expected at:
///   macOS:   libchdlite.dylib  (bundled in app)
///   Windows: chdlite.dll
///   Linux:   libchdlite.so

// --- Native function typedefs (C side) ---
typedef ChdExtractNative = Int32 Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> outputDir, Int32 splitBin);
typedef ChdHashNative = Pointer<Utf8> Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> algorithms);
typedef ChdCompressNative = Int32 Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> outputPath, Pointer<Utf8> codec,
    Int32 hunkSize, Int32 unitSize, Int32 threads);
typedef ChdReadNative = Pointer<Utf8> Function(Pointer<Utf8> inputPath);
typedef ChdCancelNative = Void Function();
typedef ChdFreeJsonNative = Void Function(Pointer<Utf8> json);

typedef ProgressCallbackNative = Void Function(Double progress);
typedef ChdSetProgressCallbackNative = Void Function(
    Pointer<NativeFunction<ProgressCallbackNative>> callback);

// --- Dart function typedefs ---
typedef ChdExtractDart = int Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> outputDir, int splitBin);
typedef ChdHashDart = Pointer<Utf8> Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> algorithms);
typedef ChdCompressDart = int Function(Pointer<Utf8> inputPath,
    Pointer<Utf8> outputPath, Pointer<Utf8> codec,
    int hunkSize, int unitSize, int threads);
typedef ChdReadDart = Pointer<Utf8> Function(Pointer<Utf8> inputPath);
typedef ChdCancelDart = void Function();
typedef ChdFreeJsonDart = void Function(Pointer<Utf8> json);
typedef ChdSetProgressCallbackDart = void Function(
    Pointer<NativeFunction<ProgressCallbackNative>> callback);

class ChdliteFfi {
  static ChdliteFfi? _instance;
  // ignore: unused_field
  late final DynamicLibrary _lib;
  bool _loaded = false;

  ChdliteFfi._();

  static ChdliteFfi get instance {
    _instance ??= ChdliteFfi._();
    return _instance!;
  }

  bool get isLoaded => _loaded;

  /// Load the native CHDlite library. Returns false if not found.
  bool load() {
    if (_loaded) return true;

    try {
      _lib = _openLibrary();
      _loaded = true;
      return true;
    } catch (e) {
      _loaded = false;
      return false;
    }
  }

  DynamicLibrary _openLibrary() {
    if (Platform.isMacOS) {
      // In app bundle: Frameworks/libchdlite.dylib
      // Or next to executable for dev
      final exeDir = File(Platform.resolvedExecutable).parent.path;
      final paths = [
        '$exeDir/../Frameworks/libchdlite.dylib',
        '$exeDir/libchdlite.dylib',
        'libchdlite.dylib',
      ];
      for (final path in paths) {
        if (File(path).existsSync()) {
          return DynamicLibrary.open(path);
        }
      }
      return DynamicLibrary.open('libchdlite.dylib');
    } else if (Platform.isWindows) {
      return DynamicLibrary.open('chdlite.dll');
    } else {
      return DynamicLibrary.open('libchdlite.so');
    }
  }

  // --- Stub methods (active once native lib is built) ---

  /// Extract a CHD file to CUE/BIN.
  /// Returns 0 on success, non-zero on error.
  int extract(String inputPath, String outputDir, bool splitBin) {
    if (!_loaded) return -1;
    // TODO: Marshal strings and call native function
    return -1;
  }

  /// Hash a CHD file. Returns JSON result string.
  String? hash(String inputPath, List<String> algorithms) {
    if (!_loaded) return null;
    // TODO: Marshal strings and call native function
    return null;
  }

  /// Compress CUE/BIN to CHD.
  /// Returns 0 on success, non-zero on error.
  int compress(String inputPath, String outputPath, String codec,
      int hunkSize, int unitSize, int threads) {
    if (!_loaded) return -1;
    // TODO: Marshal strings and call native function
    return -1;
  }

  /// Read CHD header info. Returns JSON string.
  String? read(String inputPath) {
    if (!_loaded) return null;
    // TODO: Marshal strings and call native function
    return null;
  }

  /// Cancel the current operation.
  void cancel() {
    if (!_loaded) return;
    // TODO: Call native cancel
  }
}
