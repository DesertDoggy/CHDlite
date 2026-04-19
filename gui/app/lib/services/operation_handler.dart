import 'dart:async';
import 'dart:isolate';
import 'chdlite_ffi.dart';

/// Describes an operation request to run in a background isolate.
class OperationRequest {
  final String operation; // read, compress, extract, hash
  final List<String> inputPaths;
  final Map<String, dynamic> options;
  final SendPort sendPort;

  OperationRequest({
    required this.operation,
    required this.inputPaths,
    required this.options,
    required this.sendPort,
  });
}

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

/// Manages running CHDlite operations in background isolates.
class OperationHandler {
  Isolate? _isolate;
  ReceivePort? _receivePort;
  StreamSubscription? _subscription;

  bool get isRunning => _isolate != null;

  /// Start an operation in a background isolate.
  /// [onProgress] called with 0.0-1.0 values.
  /// [onOutput] called with each output line.
  /// [onComplete] called when the operation finishes.
  Future<void> start({
    required String operation,
    required List<String> inputPaths,
    required Map<String, dynamic> options,
    required void Function(double progress) onProgress,
    required void Function(String line) onOutput,
    required void Function(bool success, String? error) onComplete,
  }) async {
    await cancel(); // Cancel any previous operation

    _receivePort = ReceivePort();

    final request = OperationRequest(
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
  static void _isolateEntry(OperationRequest request) {
    final ffi = ChdliteFfi.instance;
    final port = request.sendPort;

    if (!ffi.load()) {
      port.send(OutputLineMessage('Error: Native library not found.'));
      port.send(OutputLineMessage(
          'The CHDlite native library must be built and placed alongside the app.'));
      port.send(CompletedMessage(success: false, error: 'Native library not loaded'));
      return;
    }

    port.send(OutputLineMessage('Starting ${request.operation}...'));

    try {
      for (final path in request.inputPaths) {
        port.send(OutputLineMessage('Processing: $path'));

        switch (request.operation) {
          case 'read':
            final json = ffi.read(path);
            if (json != null) {
              port.send(OutputLineMessage(json));
            } else {
              port.send(OutputLineMessage('Error reading: $path'));
            }

          case 'hash':
            final algorithms =
                (request.options['algorithms'] as List<String>?) ?? ['sha1'];
            final json = ffi.hash(path, algorithms);
            if (json != null) {
              port.send(OutputLineMessage(json));
            } else {
              port.send(OutputLineMessage('Error hashing: $path'));
            }

          case 'extract':
            final outputDir =
                (request.options['output_dir'] as String?) ?? '';
            final splitBin =
                (request.options['split_bin'] as bool?) ?? false;
            final rc = ffi.extract(path, outputDir, splitBin);
            if (rc != 0) {
              port.send(OutputLineMessage('Error extracting: $path (code $rc)'));
            }

          case 'compress':
            final outputPath =
                (request.options['output_path'] as String?) ?? '';
            final codec =
                (request.options['codec'] as String?) ?? 'zstd';
            final hunkSize =
                (request.options['hunk_size'] as int?) ?? 65536;
            final unitSize =
                (request.options['unit_size'] as int?) ?? 2048;
            final threads =
                (request.options['threads'] as int?) ?? 0;
            final rc = ffi.compress(
                path, outputPath, codec, hunkSize, unitSize, threads);
            if (rc != 0) {
              port.send(
                  OutputLineMessage('Error compressing: $path (code $rc)'));
            }
        }
      }

      port.send(OutputLineMessage('Done.'));
      port.send(CompletedMessage(success: true));
    } catch (e) {
      port.send(OutputLineMessage('Error: $e'));
      port.send(CompletedMessage(success: false, error: e.toString()));
    }
  }
}
