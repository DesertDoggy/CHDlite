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

  bool _isProgressLine(String line) => line.startsWith('Progress: ');

  double? _progressPercent(String line) {
    if (!_isProgressLine(line)) return null;
    final m = RegExp(r'^Progress:\s*([0-9]+(?:\.[0-9]+)?)%').firstMatch(line);
    if (m == null) return null;
    return double.tryParse(m.group(1)!);
  }

  void _finalizeLastProgressTo100() {
    if (_outputLines.isEmpty) return;
    final idx = _outputLines.length - 1;
    final last = _outputLines[idx];
    if (!_isProgressLine(last)) return;
    final pct = _progressPercent(last) ?? 0;
    if (pct >= 100.0) return;
    _outputLines[idx] = last.replaceFirst(RegExp(r'Progress:\s*[0-9]+(?:\.[0-9]+)?%'), 'Progress: 100.0%');
  }

  void _upsertProgressLine(String line) {
    if (_outputLines.isNotEmpty && _isProgressLine(_outputLines.last)) {
      _outputLines[_outputLines.length - 1] = line;
    } else {
      _outputLines.add(line);
    }
  }

  void _onFilesDropped(ChdOperation operation, List<String> paths) {
    _handleDropped(operation, paths);
  }

  Future<void> _handleDropped(ChdOperation operation, List<String> paths) async {
    final settings = SettingsManager.instance;

    // Accept dropped files and directories; expand directories recursively.
    final expandedPaths = await _expandInputPaths(paths);
    final validPaths = await _normalizeInputsForOperation(operation, expandedPaths);

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

  Future<String?> _findReferencingSheet(String binPath) async {
    final binFile = File(binPath);
    final dirPath = binFile.parent.path;
    final binNameLower = binFile.uri.pathSegments.last.toLowerCase();

    try {
      await for (final entity in Directory(dirPath).list(followLinks: false)) {
        if (entity is! File) continue;
        final ext = entity.path.toLowerCase();
        if (!ext.endsWith('.cue') && !ext.endsWith('.gdi')) continue;

        try {
          final lines = await entity.readAsLines();
          for (final line in lines) {
            if (line.toLowerCase().contains(binNameLower)) {
              return entity.path;
            }
          }
        } catch (_) {
          // Ignore unreadable sheet files.
        }
      }
    } catch (_) {
      // Ignore unreadable directories.
    }

    return null;
  }

  Future<List<String>> _normalizeInputsForOperation(
    ChdOperation operation,
    List<String> paths,
  ) async {
    if (operation != ChdOperation.compress) return paths;

    // For compress: if a .bin belongs to a .cue/.gdi sheet, use the sheet path.
    // This matches CLI expectations and avoids generating malformed single-track CD CHDs.
    final out = <String>[];
    final seen = <String>{};

    for (final p in paths) {
      final lower = p.toLowerCase();
      if (lower.endsWith('.bin')) {
        final sheet = await _findReferencingSheet(p);
        if (sheet != null) {
          if (seen.add(sheet)) out.add(sheet);
          continue;
        }
      }

      if (seen.add(p)) out.add(p);
    }

    return out;
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

    int cueStyleToInt(String style) {
      switch (style) {
        case 'redump':
          return 1;
        case 'redump_catalog':
          return 2;
        case 'chdman':
        default:
          return 0;
      }
    }

    switch (operation) {
      case ChdOperation.read:
        break;

      case ChdOperation.hash:
        final algos = settings.getList('hash.algorithms', ['sha1']);
        options['algorithms'] = algos;
        break;

      case ChdOperation.extract:
        final outDir = settings.get('output.extract_output_dir', '');
        if (outDir.isNotEmpty) options['output_dir'] = outDir;
        options['split_bin'] = settings.getBool('extract.split_bin', true);
        options['cue_style'] = cueStyleToInt(settings.get('extract.cue_style', 'chdman'));
        break;

      case ChdOperation.compress:
        final outDir = settings.get('output.compress_output_dir', '');
        if (outDir.isNotEmpty) options['output_path'] = outDir;

        final compCodec = settings.get('compress.comp_codec', 'best');
        final liteCodec = settings.get('compress.lite_codec', 'auto');
        if (liteCodec == 'custom') {
          final codecList = settings.get('compress.lite_codec_list', '');
          if (codecList.isNotEmpty) options['codec'] = codecList;
        } else if (compCodec == 'chdman') {
          options['codec'] = 'chdman';
        } else if (compCodec == 'best') {
          options['codec'] = 'best';
        } else {
          options['codec'] = 'auto';
        }

        final hunk = int.tryParse(settings.get('compress.hunk_size', '0')) ?? 0;
        final unit = int.tryParse(settings.get('compress.unit_size', '0')) ?? 0;
        final threads = int.tryParse(settings.get('compress.threads', '0')) ?? 0;
        options['split_bin'] = settings.getBool('extract.split_bin', true);
        options['cue_style'] = cueStyleToInt(settings.get('extract.cue_style', 'chdman'));
        if (hunk > 0) options['hunk_size'] = hunk;
        if (unit > 0) options['unit_size'] = unit;
        if (threads > 0) options['threads'] = threads;
        break;
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
          setState(() {
            if (line.startsWith('Processing: ')) {
              _finalizeLastProgressTo100();
              _outputLines.add(line);
              return;
            }

            if (_isProgressLine(line)) {
              _upsertProgressLine(line);
              return;
            }

            _outputLines.add(line);
          });
        }
      },
      onComplete: (success, error) async {
        await _tempLogSink?.flush();
        if (mounted) {
          setState(() {
            _finalizeLastProgressTo100();
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
            child: LayoutBuilder(
              builder: (context, constraints) {
                // Keep 4 icons visually edge-to-edge while avoiding startup edge clipping.
                final iconSize = ((constraints.maxWidth - 2) / 4).clamp(120.0, 480.0);
                return Column(
                  children: [
                    Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      mainAxisSize: MainAxisSize.min,
                      spacing: 0,
                      children: [
                        DropIconButton(
                          assetPath: 'assets/CHD read Icon1.png',
                          operation: ChdOperation.read,
                          onFilesDropped: _onFilesDropped,
                          isProcessing: _isProcessing,
                          size: iconSize,
                        ),
                        DropIconButton(
                          assetPath: 'assets/CHDcomp Icon1.png',
                          operation: ChdOperation.compress,
                          onFilesDropped: _onFilesDropped,
                          isProcessing: _isProcessing,
                          size: iconSize,
                        ),
                        DropIconButton(
                          assetPath: 'assets/Groove-Title.png',
                          operation: ChdOperation.extract,
                          onFilesDropped: _onFilesDropped,
                          isProcessing: _isProcessing,
                          size: iconSize,
                        ),
                        DropIconButton(
                          assetPath: 'assets/CHD hash Icon1.png',
                          operation: ChdOperation.hash,
                          onFilesDropped: _onFilesDropped,
                          isProcessing: _isProcessing,
                          size: iconSize,
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
                );
              },
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
