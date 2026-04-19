import 'package:flutter/material.dart';
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

  void _onFilesDropped(ChdOperation operation, List<String> paths) {
    setState(() {
      _isProcessing = true;
      _progress = 0.0;
      _outputLines.clear();
      _outputLines.add('Operation: ${operation.name}');
      _outputLines.add('Files: ${paths.length}');
      for (final path in paths) {
        _outputLines.add('  $path');
      }
      _outputLines.add('');
      _outputLines.add('Processing...');
    });

    // TODO: Call CHDlite FFI here
    // For now, simulate completion
    Future.delayed(const Duration(seconds: 1), () {
      if (mounted) {
        setState(() {
          _isProcessing = false;
          _progress = 1.0;
          _outputLines.add('Done. (FFI not yet connected)');
        });
      }
    });
  }

  void _onCancel() {
    // TODO: Call chdlite_cancel_operation() via FFI
    setState(() {
      _isProcessing = false;
      _outputLines.add('Cancelled by user.');
    });
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
