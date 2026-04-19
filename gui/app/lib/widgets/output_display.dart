import 'package:flutter/material.dart';
import 'package:url_launcher/url_launcher.dart';
import 'dart:io';

class OutputDisplay extends StatefulWidget {
  final List<String> lines;
  final double progress;
  final bool isProcessing;
  final VoidCallback onCancel;

  const OutputDisplay({
    super.key,
    required this.lines,
    required this.progress,
    required this.isProcessing,
    required this.onCancel,
  });

  @override
  State<OutputDisplay> createState() => _OutputDisplayState();
}

class _OutputDisplayState extends State<OutputDisplay> {
  final ScrollController _scrollController = ScrollController();

  @override
  void didUpdateWidget(OutputDisplay oldWidget) {
    super.didUpdateWidget(oldWidget);
    // Auto-scroll to bottom when new lines are added
    if (widget.lines.length != oldWidget.lines.length) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_scrollController.hasClients) {
          _scrollController.animateTo(
            _scrollController.position.maxScrollExtent,
            duration: const Duration(milliseconds: 100),
            curve: Curves.easeOut,
          );
        }
      });
    }
  }

  @override
  void dispose() {
    _scrollController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);

    return Padding(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Progress bar (visible during processing)
          if (widget.isProcessing) ...[
            LinearProgressIndicator(
              value: widget.progress > 0 ? widget.progress : null,
            ),
            const SizedBox(height: 8),
          ],
          // Output text area
          Expanded(
            child: Container(
              decoration: BoxDecoration(
                color: theme.colorScheme.surfaceContainerLowest,
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: theme.colorScheme.outlineVariant),
              ),
              child: widget.lines.isEmpty
                  ? Center(
                      child: Text(
                        'Drop files onto an operation button above to begin',
                        style: theme.textTheme.bodyMedium?.copyWith(
                          color: theme.colorScheme.onSurfaceVariant,
                        ),
                      ),
                    )
                  : SelectableRegion(
                      focusNode: FocusNode(),
                      selectionControls: materialTextSelectionControls,
                      child: ListView.builder(
                        controller: _scrollController,
                        padding: const EdgeInsets.all(12),
                        itemCount: widget.lines.length,
                        itemBuilder: (context, index) {
                          final line = widget.lines[index];
                          return Text(
                            line,
                            style: TextStyle(
                              fontFamily: 'monospace',
                              fontSize: 13,
                              color: _colorForLine(line, theme),
                            ),
                          );
                        },
                      ),
                    ),
            ),
          ),
          const SizedBox(height: 8),
          // Action buttons
          Row(
            mainAxisAlignment: MainAxisAlignment.end,
            children: [
              if (widget.isProcessing)
                FilledButton.tonal(
                  onPressed: widget.onCancel,
                  child: const Text('Cancel'),
                ),
              const Spacer(),
              OutlinedButton.icon(
                onPressed: widget.lines.isEmpty ? null : _openInEditor,
                icon: const Icon(Icons.open_in_new, size: 16),
                label: const Text('Open in Editor'),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Color _colorForLine(String line, ThemeData theme) {
    final lower = line.toLowerCase();
    if (lower.startsWith('error') || lower.contains('error:') || lower.contains('failed')) {
      return Colors.red;
    }
    if (lower.startsWith('warning') || lower.contains('warn')) {
      return Colors.orange;
    }
    if (lower.contains('success') || lower.contains('done') || lower.contains('completed')) {
      return Colors.green;
    }
    if (lower.startsWith('cancelled')) {
      return Colors.orange;
    }
    return theme.colorScheme.onSurface;
  }

  void _openInEditor() async {
    // Find the last mentioned output file in the lines
    String? outputFile;
    for (final line in widget.lines.reversed) {
      if (line.contains('.hashes') || line.contains('.log')) {
        // Try to extract a file path
        final match = RegExp(r'(/[^\s]+|[A-Z]:\\[^\s]+)').firstMatch(line);
        if (match != null) {
          outputFile = match.group(0);
          break;
        }
      }
    }

    if (outputFile != null && File(outputFile).existsSync()) {
      final uri = Uri.file(outputFile);
      if (await canLaunchUrl(uri)) {
        await launchUrl(uri);
      }
    }
  }
}
