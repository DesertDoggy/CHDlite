import 'dart:async';
import 'dart:convert';
import 'dart:isolate';
import 'dart:math' as math;
import 'chdlite_ffi.dart';

/// Messages sent from the background isolate to the UI.
sealed class OperationMessage {}

class ProgressMessage extends OperationMessage {
  final double progress;
  ProgressMessage(this.progress);
}

class OutputLineMessage extends OperationMessage {
  final String line;
  OutputLineMessage(this.line);
}

class CompletedMessage extends OperationMessage {
  final bool success;
  final String? error;
  CompletedMessage({required this.success, this.error});
}

/// Request to run in a background isolate.
class _IsolateRequest {
  final String operation;
  final List<String> inputPaths;
  final Map<String, dynamic> options;
  final SendPort sendPort;

  _IsolateRequest({
    required this.operation,
    required this.inputPaths,
    required this.options,
    required this.sendPort,
  });
}

/// Manages running CHDlite operations in background isolates.
class OperationHandler {
  Isolate? _isolate;
  ReceivePort? _receivePort;
  StreamSubscription? _subscription;

  bool get isRunning => _isolate != null;

  /// Start an operation in a background isolate.
  Future<void> start({
    required String operation,
    required List<String> inputPaths,
    required Map<String, dynamic> options,
    required void Function(double progress) onProgress,
    required void Function(String line) onOutput,
    required void Function(bool success, String? error) onComplete,
  }) async {
    await cancel();

    _receivePort = ReceivePort();

    final request = _IsolateRequest(
      operation: operation,
      inputPaths: inputPaths,
      options: options,
      sendPort: _receivePort!.sendPort,
    );

    _subscription = _receivePort!.listen((message) {
      if (message is ProgressMessage) {
        onProgress(message.progress);
      } else if (message is OutputLineMessage) {
        onOutput(message.line);
      } else if (message is CompletedMessage) {
        onComplete(message.success, message.error);
        _cleanup();
      }
    });

    _isolate = await Isolate.spawn(_isolateEntry, request);
  }

  /// Cancel the current operation.
  Future<void> cancel() async {
    ChdliteFfi.instance.cancel();
    _cleanup();
  }

  void _cleanup() {
    _subscription?.cancel();
    _subscription = null;
    _receivePort?.close();
    _receivePort = null;
    _isolate?.kill(priority: Isolate.immediate);
    _isolate = null;
  }

  /// Entry point for the background isolate.
  static void _isolateEntry(_IsolateRequest request) {
    final ffi = ChdliteFfi.instance;
    final port = request.sendPort;

    if (!ffi.load()) {
      port.send(OutputLineMessage('Native library not found.'));
      port.send(OutputLineMessage(
          'Build chdlite_ffi first: cmake --build build --target chdlite_ffi'));
      port.send(CompletedMessage(success: false, error: 'Native library not loaded'));
      return;
    }

    final ver = ffi.version() ?? '?';
    port.send(OutputLineMessage('CHDlite $ver — ${_displayOperationName(request.operation)}'));

    var currentFileIndex = 0;
    final totalFiles = request.inputPaths.isEmpty ? 1 : request.inputPaths.length;
    var lastProgressEmitMs = 0;

    ffi.installLogCallback((level, message) {
      if (message.trim().isEmpty) return;
      port.send(OutputLineMessage(message));
    });

    ffi.installProgressCallback((current, total) {
      if (total <= 0) return;
      final nowMs = DateTime.now().millisecondsSinceEpoch;
      if (nowMs - lastProgressEmitMs < 120 && current < total) return;
      lastProgressEmitMs = nowMs;
      final fileProgress = (current / total).clamp(0.0, 1.0);
      final overall = ((currentFileIndex - 1) + fileProgress) / totalFiles;
      port.send(ProgressMessage(math.max(0.0, math.min(1.0, overall))));
      port.send(OutputLineMessage(
          'Progress: ${(fileProgress * 100).toStringAsFixed(1)}%  File $currentFileIndex/$totalFiles'));
    });

    bool allOk = true;
    String? lastError;

    for (final path in request.inputPaths) {
      currentFileIndex += 1;
      port.send(OutputLineMessage('Processing: $path'));
      switch (request.operation) {
        case 'hash':
          port.send(OutputLineMessage('Hashing...'));
        case 'extract':
          port.send(OutputLineMessage('Extracting...'));
        case 'compress':
          port.send(OutputLineMessage('Compressing...'));
        default:
          break;
      }

      String? json;
      switch (request.operation) {
        case 'read':
          json = ffi.read(path);

        case 'hash':
          final algorithms =
              (request.options['algorithms'] as List<String>?) ?? ['sha1'];
          json = ffi.hash(path, algorithms);

        case 'extract':
          final outputDir = (request.options['output_dir'] as String?) ?? '';
          final splitBin = (request.options['split_bin'] as bool?) ?? true;
          final cueStyle = (request.options['cue_style'] as int?) ?? 0;
          json = ffi.extract(path, outputDir, splitBin, cueStyle);

        case 'compress':
          final outputPath = (request.options['output_path'] as String?) ?? '';
          final codec = (request.options['codec'] as String?) ?? '';
          final hunkSize = (request.options['hunk_size'] as int?) ?? 0;
          final unitSize = (request.options['unit_size'] as int?) ?? 0;
          final threads = (request.options['threads'] as int?) ?? 0;
          final cueStyle = (request.options['cue_style'] as int?) ?? 0;
          final splitBin = (request.options['split_bin'] as bool?) ?? true;
          json = ffi.compress(path, outputPath, codec, hunkSize, unitSize, threads, cueStyle, splitBin);
      }

      if (json == null) {
        port.send(OutputLineMessage('Error: null result for $path'));
        allOk = false;
        continue;
      }

      // Parse JSON result and display
      try {
        final result = jsonDecode(json) as Map<String, dynamic>;
        final success = result['success'] as bool? ?? false;

        if (!success) {
          final error = result['error'] as String? ?? 'Unknown error';
          port.send(OutputLineMessage('Error: $error'));
          allOk = false;
          lastError = error;
        } else {
          // Format output depending on operation
          _formatResult(port, request.operation, result);
        }
      } catch (e) {
        // If JSON parse fails, dump raw
        port.send(OutputLineMessage(json));
      }
    }

    ffi.clearProgressCallback();
    ffi.clearLogCallback();

    port.send(OutputLineMessage(allOk ? 'Done.' : 'Completed with errors.'));
    port.send(CompletedMessage(success: allOk, error: lastError));
  }

  static String _displayOperationName(String operation) {
    switch (operation) {
      case 'compress':
        return 'create';
      default:
        return operation;
    }
  }

  static void _formatResult(
      SendPort port, String operation, Map<String, dynamic> result) {
    switch (operation) {
      case 'read':
        // C++ formats all output lines; just split on newlines and emit each.
        final formatted = result['formatted'] as String? ?? '';
        for (final line in formatted.split('\n')) {
          if (line.isNotEmpty) port.send(OutputLineMessage(line));
        }

      case 'hash':
        final tracks = result['tracks'] as List? ?? [];
        for (final t in tracks) {
          final track = t as Map<String, dynamic>;
          var line = 'Track ${track['track']} (${track['type']})';
          if (track['sha1'] != null) line += '  SHA1: ${track['sha1']}';
          if (track['md5'] != null) line += '  MD5: ${track['md5']}';
          if (track['crc32'] != null) line += '  CRC32: ${track['crc32']}';
          port.send(OutputLineMessage(line));
        }

      case 'extract':
        port.send(OutputLineMessage('Output: ${result['output_path']}'));
        port.send(OutputLineMessage(
            'Written: ${result['bytes_written']} bytes  '
            'Type: ${result['detected_type']}'));
        final files = result['files'] as List? ?? [];
        for (final f in files) {
          port.send(OutputLineMessage('  $f'));
        }

      case 'compress':
        final formatted = result['formatted'] as String? ?? '';
        if (formatted.isNotEmpty) {
          for (final line in formatted.split('\n')) {
            if (line.isNotEmpty) port.send(OutputLineMessage(line));
          }
        } else {
          port.send(OutputLineMessage('Output: ${result['output_path']}'));
          port.send(OutputLineMessage(
              'Ratio: ${result['compression_ratio']}  '
              'Codec: ${result['codec_used']}'));
        }
    }
  }
}
