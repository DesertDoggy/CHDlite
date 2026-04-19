import 'dart:io';

import 'package:flutter/material.dart';
import '../services/operation_handler.dart';
import '../services/settings_manager.dart';
import '../widgets/drop_icon_button.dart';
import '../widgets/output_display.dart';

enum ChdOperation { read, compress, extract, hash }

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final List<String> _outputLines = [];
  double _progress = 0.0;
  bool _isProcessing = false;
  final OperationHandler _handler = OperationHandler();
  File? _tempLogFile;
  IOSink? _tempLogSink;

  void _onFilesDropped(ChdOperation operation, List<String> paths) {
    _handleDropped(operation, paths);
  }

  Future<void> _handleDropped(ChdOperation operation, List<String> paths) async {
    final settings = SettingsManager.instance;

    // Accept dropped files and directories; expand directories recursively.
    final validPaths = await _expandInputPaths(paths);

    if (validPaths.isEmpty) {
      setState(() {
        _outputLines.add('No input files.');
      });
      return;
    }

    _startOperation(operation, validPaths, settings);
  }

  Future<List<String>> _expandInputPaths(List<String> paths) async {
    final expanded = <String>[];
    for (final raw in paths) {
      final p = raw.trim();
      if (p.isEmpty) continue;

      final type = await FileSystemEntity.type(p, followLinks: true);
      if (type == FileSystemEntityType.file) {
        expanded.add(p);
        continue;
      }

      if (type == FileSystemEntityType.directory) {
        try {
          await for (final entity
              in Directory(p).list(recursive: true, followLinks: false)) {
            if (entity is File) expanded.add(entity.path);
          }
        } catch (_) {
          // Ignore unreadable directories and continue with remaining inputs.
        }
      }
    }

    // Deduplicate while preserving order.
    final seen = <String>{};
    final result = <String>[];
    for (final p in expanded) {
      if (seen.add(p)) result.add(p);
    }
    return result;
  }

  Future<void> _startTempLog() async {
    // Close and delete any previous temp log
    await _tempLogSink?.close();
    try { await _tempLogFile?.delete(); } catch (_) {}
    _tempLogFile = null;
    _tempLogSink = null;

    final tmp = Directory.systemTemp;
    final ts = DateTime.now().millisecondsSinceEpoch;
    _tempLogFile = File('${tmp.path}/chdlite_output_$ts.txt');
    _tempLogSink = _tempLogFile!.openWrite();
  }

  void _startOperation(
    ChdOperation operation,
    List<String> paths,
    SettingsManager settings,
  ) {
    _startTempLog(); // fire and forget — sync enough for append writes
    setState(() {
      _isProcessing = true;
      _progress = 0.0;
      _outputLines.clear();
    });

    // Build options map from current settings
    final options = <String, dynamic>{};

    switch (operation) {
      case ChdOperation.read:
        break;

      case ChdOperation.hash:
        final algos = settings.getList('hash.algorithms', ['sha1']);
        options['algorithms'] = algos;

      case ChdOperation.extract:
        final outDir = settings.get('output.extract_output_dir', '');
        if (outDir.isNotEmpty) options['output_dir'] = outDir;
        options['split_bin'] = settings.getBool('extract.split_bin', true);

      case ChdOperation.compress:
        final outDir = settings.get('output.compress_output_dir', '');
        if (outDir.isNotEmpty) options['output_path'] = outDir;

        final compCodec = settings.get('compress.comp_codec', 'best');
        final liteCodec = settings.get('compress.lite_codec', 'auto');
        if (compCodec == 'chdman') {
          options['codec'] = 'chdman';
        } else if (liteCodec == 'custom') {
          final codecList = settings.get('compress.lite_codec_list', '');
          if (codecList.isNotEmpty) options['codec'] = codecList;
        }

        final hunk = int.tryParse(settings.get('compress.hunk_size', '0')) ?? 0;
        final unit = int.tryParse(settings.get('compress.unit_size', '0')) ?? 0;
        final threads = int.tryParse(settings.get('compress.threads', '0')) ?? 0;
        if (hunk > 0) options['hunk_size'] = hunk;
        if (unit > 0) options['unit_size'] = unit;
        if (threads > 0) options['threads'] = threads;
    }

    _handler.start(
      operation: operation.name,
      inputPaths: paths,
      options: options,
      onProgress: (p) {
        if (mounted) setState(() => _progress = p);
      },
      onOutput: (line) {
        if (mounted) {
          _tempLogSink?.writeln(line);
          setState(() => _outputLines.add(line));
        }
      },
      onComplete: (success, error) async {
        await _tempLogSink?.flush();
        if (mounted) {
          setState(() {
            _isProcessing = false;
            _progress = success ? 1.0 : _progress;
          });
        }
      },
    );
  }

  void _onCancel() {
    _handler.cancel();
    _tempLogSink?.writeln('Cancelled by user.');
    _tempLogSink?.flush();
    setState(() {
      _isProcessing = false;
      _outputLines.add('Cancelled by user.');
    });
  }

  @override
  void dispose() {
    _tempLogSink?.close();
    // Best-effort delete — ignore errors
    _tempLogFile?.delete().catchError((_) => _tempLogFile!);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const SizedBox.shrink(),
        centerTitle: false,
        actions: [
          IconButton(
            icon: const Icon(Icons.settings),
            tooltip: 'Settings',
            onPressed: () => Navigator.pushNamed(context, '/settings'),
          ),
          IconButton(
            icon: const Icon(Icons.close),
            tooltip: 'Close',
            onPressed: () => _showExitDialog(context),
          ),
        ],
      ),
      body: Column(
        children: [
          // Top: 4 operation buttons — clipBehavior: none so scale animation can overflow
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 16),
            child: Column(
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  mainAxisSize: MainAxisSize.min,
                  spacing: 0,
                  children: [
                    DropIconButton(
                      assetPath: '../assets/CHD read Icon1.png',
                      operation: ChdOperation.read,
                      onFilesDropped: _onFilesDropped,
                      isProcessing: _isProcessing,
                    ),
                    DropIconButton(
                      assetPath: '../assets/CHDcomp Icon1.png',
                      operation: ChdOperation.compress,
                      onFilesDropped: _onFilesDropped,
                      isProcessing: _isProcessing,
                    ),
                    DropIconButton(
                      assetPath: '../assets/Groove-Title.png',
                      operation: ChdOperation.extract,
                      onFilesDropped: _onFilesDropped,
                      isProcessing: _isProcessing,
                    ),
                    DropIconButton(
                      assetPath: '../assets/CHD hash Icon1.png',
                      operation: ChdOperation.hash,
                      onFilesDropped: _onFilesDropped,
                      isProcessing: _isProcessing,
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                Text(
                  'Comp: Max compression. Slow.',
                  style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                    fontSize: 18,
                  ),
                ),
                Text(
                  'Lite: Fast.',
                  style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: Theme.of(context).colorScheme.onSurfaceVariant,
                    fontSize: 18,
                  ),
                ),
              ],
            ),
          ),
          const Divider(height: 1),
          // Bottom: Output display
          Expanded(
            child: OutputDisplay(
              lines: _outputLines,
              progress: _progress,
              isProcessing: _isProcessing,
              onCancel: _onCancel,
              tempLogFile: _tempLogFile,
            ),
          ),
        ],
      ),
    );
  }

  Future<void> _showExitDialog(BuildContext context) async {
    if (_isProcessing) {
      final shouldExit = await showDialog<bool>(
        context: context,
        builder: (ctx) => AlertDialog(
          title: const Text('Operation in progress'),
          content: const Text('Cancel the current operation and exit?'),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('Stay'),
            ),
            TextButton(
              onPressed: () => Navigator.pop(ctx, true),
              child: const Text('Exit'),
            ),
          ],
        ),
      );
      if (shouldExit != true) return;
      _onCancel();
    }
    // Exit the app
    if (context.mounted) {
      Navigator.of(context).maybePop();
    }
  }
}
