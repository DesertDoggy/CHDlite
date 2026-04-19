import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

/// FFI bindings to the CHDlite C wrapper library (chdlite_c.h).
///
/// Native library locations:
///   macOS:   libchdlite_ffi.dylib  (in app bundle Frameworks/)
///   Windows: chdlite_ffi.dll       (next to exe)
///   Linux:   libchdlite_ffi.so     (in lib/)

// ---------------------------------------------------------------------------
// Native function signatures (C side)
// ---------------------------------------------------------------------------
typedef _ReadNative = Pointer<Utf8> Function(Pointer<Utf8> chdPath);
typedef _HashNative = Pointer<Utf8> Function(
    Pointer<Utf8> chdPath, Pointer<Utf8> algorithms);
typedef _ExtractNative = Pointer<Utf8> Function(
  Pointer<Utf8> chdPath, Pointer<Utf8> outputDir, Int32 splitBin, Int32 cueStyle);
typedef _CompressNative = Pointer<Utf8> Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> outputPath, Pointer<Utf8> codec,
  Int32 hunkSize, Int32 unitSize, Int32 threads, Int32 cueStyle);
typedef _FreeNative = Void Function(Pointer<Utf8> ptr);
typedef _CancelNative = Void Function();
typedef _VersionNative = Pointer<Utf8> Function();

typedef _ProgressCallbackNative = Int32 Function(Uint64 current, Uint64 total);
typedef _SetProgressNative = Void Function(
    Pointer<NativeFunction<_ProgressCallbackNative>> cb);

typedef _LogCallbackNative = Void Function(Int32 level, Pointer<Utf8> msg);
typedef _SetLogNative = Void Function(
    Pointer<NativeFunction<_LogCallbackNative>> cb);

// ---------------------------------------------------------------------------
// Dart-side function signatures
// ---------------------------------------------------------------------------
typedef _ReadDart = Pointer<Utf8> Function(Pointer<Utf8> chdPath);
typedef _HashDart = Pointer<Utf8> Function(
    Pointer<Utf8> chdPath, Pointer<Utf8> algorithms);
typedef _ExtractDart = Pointer<Utf8> Function(
  Pointer<Utf8> chdPath, Pointer<Utf8> outputDir, int splitBin, int cueStyle);
typedef _CompressDart = Pointer<Utf8> Function(
    Pointer<Utf8> inputPath, Pointer<Utf8> outputPath, Pointer<Utf8> codec,
  int hunkSize, int unitSize, int threads, int cueStyle);
typedef _FreeDart = void Function(Pointer<Utf8> ptr);
typedef _CancelDart = void Function();
typedef _VersionDart = Pointer<Utf8> Function();
typedef _SetProgressDart = void Function(
    Pointer<NativeFunction<_ProgressCallbackNative>> cb);
typedef _SetLogDart = void Function(
    Pointer<NativeFunction<_LogCallbackNative>> cb);

class ChdliteFfi {
  static ChdliteFfi? _instance;
  late final DynamicLibrary _lib;
  bool _loaded = false;

  // Resolved function pointers
  late final _ReadDart _read;
  late final _HashDart _hash;
  late final _ExtractDart _extract;
  late final _CompressDart _compress;
  late final _FreeDart _free;
  late final _CancelDart _cancel;
  late final _VersionDart _version;
  late final _SetProgressDart _setProgress;
  late final _SetLogDart _setLog;

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
      _read = _lib.lookupFunction<_ReadNative, _ReadDart>('chdlite_read');
      _hash = _lib.lookupFunction<_HashNative, _HashDart>('chdlite_hash');
      _extract =
          _lib.lookupFunction<_ExtractNative, _ExtractDart>('chdlite_extract');
      _compress = _lib
          .lookupFunction<_CompressNative, _CompressDart>('chdlite_compress');
      _free = _lib.lookupFunction<_FreeNative, _FreeDart>('chdlite_free');
      _cancel =
          _lib.lookupFunction<_CancelNative, _CancelDart>('chdlite_cancel');
      _version =
          _lib.lookupFunction<_VersionNative, _VersionDart>('chdlite_version');
      _setProgress = _lib.lookupFunction<_SetProgressNative, _SetProgressDart>(
          'chdlite_set_progress_callback');
      _setLog = _lib.lookupFunction<_SetLogNative, _SetLogDart>(
          'chdlite_set_log_callback');
      _loaded = true;
      return true;
    } catch (e) {
      _loaded = false;
      return false;
    }
  }

  // -----------------------------------------------------------------------
  // Library search paths per platform
  // -----------------------------------------------------------------------
  DynamicLibrary _openLibrary() {
    if (Platform.isMacOS) {
      final exeDir = File(Platform.resolvedExecutable).parent.path;
      for (final path in [
        '$exeDir/../Frameworks/libchdlite_ffi.dylib',
        '$exeDir/libchdlite_ffi.dylib',
        'libchdlite_ffi.dylib',
      ]) {
        if (File(path).existsSync()) return DynamicLibrary.open(path);
      }
      return DynamicLibrary.open('libchdlite_ffi.dylib');
    } else if (Platform.isWindows) {
      return DynamicLibrary.open('chdlite_ffi.dll');
    } else {
      return DynamicLibrary.open('libchdlite_ffi.so');
    }
  }

  // -----------------------------------------------------------------------
  // Public API — each returns a JSON string (caller must not free)
  // -----------------------------------------------------------------------

  /// Read CHD header info. Returns JSON string.
  String? read(String chdPath) {
    if (!_loaded) return null;
    final pathPtr = chdPath.toNativeUtf8();
    final resultPtr = _read(pathPtr);
    malloc.free(pathPtr);
    if (resultPtr == nullptr) return null;
    final json = resultPtr.toDartString();
    _free(resultPtr);
    return json;
  }

  /// Hash CHD content. Returns JSON string.
  String? hash(String chdPath, List<String> algorithms) {
    if (!_loaded) return null;
    final pathPtr = chdPath.toNativeUtf8();
    final algoPtr = algorithms.join(',').toNativeUtf8();
    final resultPtr = _hash(pathPtr, algoPtr);
    malloc.free(pathPtr);
    malloc.free(algoPtr);
    if (resultPtr == nullptr) return null;
    final json = resultPtr.toDartString();
    _free(resultPtr);
    return json;
  }

  /// Extract CHD to CUE/BIN. Returns JSON string.
  String? extract(String chdPath, String outputDir, bool splitBin, int cueStyle) {
    if (!_loaded) return null;
    final pathPtr = chdPath.toNativeUtf8();
    final dirPtr = outputDir.toNativeUtf8();
    final resultPtr = _extract(pathPtr, dirPtr, splitBin ? 1 : 0, cueStyle);
    malloc.free(pathPtr);
    malloc.free(dirPtr);
    if (resultPtr == nullptr) return null;
    final json = resultPtr.toDartString();
    _free(resultPtr);
    return json;
  }

  /// Compress input to CHD. Returns JSON string.
  String? compress(String inputPath, String outputPath, String codec,
      int hunkSize, int unitSize, int threads, int cueStyle) {
    if (!_loaded) return null;
    final inPtr = inputPath.toNativeUtf8();
    final outPtr = outputPath.toNativeUtf8();
    final codecPtr = codec.toNativeUtf8();
    final resultPtr =
      _compress(inPtr, outPtr, codecPtr, hunkSize, unitSize, threads, cueStyle);
    malloc.free(inPtr);
    malloc.free(outPtr);
    malloc.free(codecPtr);
    if (resultPtr == nullptr) return null;
    final json = resultPtr.toDartString();
    _free(resultPtr);
    return json;
  }

  /// Cancel the current operation (thread-safe).
  void cancel() {
    if (!_loaded) return;
    _cancel();
  }

  /// Get native library version.
  String? version() {
    if (!_loaded) return null;
    final ptr = _version();
    if (ptr == nullptr) return null;
    return ptr.toDartString(); // static string — do NOT free
  }

  /// Set progress callback (must be a static/top-level function).
  void setProgressCallback(
      Pointer<NativeFunction<_ProgressCallbackNative>> cb) {
    if (!_loaded) return;
    _setProgress(cb);
  }

  /// Clear progress callback.
  void clearProgressCallback() {
    if (!_loaded) return;
    _setProgress(nullptr);
  }

  /// Set log callback (must be a static/top-level function).
  void setLogCallback(Pointer<NativeFunction<_LogCallbackNative>> cb) {
    if (!_loaded) return;
    _setLog(cb);
  }

  /// Clear log callback.
  void clearLogCallback() {
    if (!_loaded) return;
    _setLog(nullptr);
  }
}
