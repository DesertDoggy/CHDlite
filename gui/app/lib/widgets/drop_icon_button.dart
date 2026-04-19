import 'package:flutter/material.dart';
import 'package:desktop_drop/desktop_drop.dart';
import '../screens/home_screen.dart';

class DropIconButton extends StatefulWidget {
  final String label;
  final String assetPath;
  final ChdOperation operation;
  final void Function(ChdOperation operation, List<String> paths) onFilesDropped;
  final bool isProcessing;

  const DropIconButton({
    super.key,
    required this.label,
    required this.assetPath,
    required this.operation,
    required this.onFilesDropped,
    required this.isProcessing,
  });

  @override
  State<DropIconButton> createState() => _DropIconButtonState();
}

class _DropIconButtonState extends State<DropIconButton>
    with SingleTickerProviderStateMixin {
  bool _isDragging = false;
  late AnimationController _scaleController;
  late Animation<double> _scaleAnimation;

  @override
  void initState() {
    super.initState();
    _scaleController = AnimationController(
      duration: const Duration(milliseconds: 150),
      vsync: this,
    );
    _scaleAnimation = Tween<double>(begin: 1.0, end: 1.25).animate(
      CurvedAnimation(parent: _scaleController, curve: Curves.easeOut),
    );
  }

  @override
  void dispose() {
    _scaleController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return DropTarget(
      onDragEntered: (_) {
        if (!widget.isProcessing) {
          setState(() => _isDragging = true);
          _scaleController.forward();
        }
      },
      onDragExited: (_) {
        setState(() => _isDragging = false);
        _scaleController.reverse();
      },
      onDragDone: (details) {
        _scaleController.reverse();
        setState(() => _isDragging = false);

        if (widget.isProcessing) return;

        final paths = details.files.map((f) => f.path).toList();
        if (paths.isNotEmpty) {
          widget.onFilesDropped(widget.operation, paths);
        }
      },
      child: AnimatedBuilder(
        animation: _scaleAnimation,
        builder: (context, child) {
          return Transform.scale(
            scale: _scaleAnimation.value,
            child: child,
          );
        },
        child: Container(
          width: 120,
          height: 140,
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(12),
            border: Border.all(
              color: _isDragging
                  ? Theme.of(context).colorScheme.primary
                  : Colors.transparent,
              width: 2,
            ),
            color: _isDragging
                ? Theme.of(context).colorScheme.primary.withValues(alpha: 0.1)
                : null,
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: Image.asset(
                  widget.assetPath,
                  width: 80,
                  height: 80,
                  fit: BoxFit.contain,
                  errorBuilder: (_, e, st) => Container(
                    width: 80,
                    height: 80,
                    color: Theme.of(context).colorScheme.surfaceContainerHighest,
                    child: Icon(
                      _iconForOperation(widget.operation),
                      size: 40,
                      color: Theme.of(context).colorScheme.primary,
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 8),
              Text(
                widget.label,
                style: Theme.of(context).textTheme.labelLarge,
                textAlign: TextAlign.center,
              ),
            ],
          ),
        ),
      ),
    );
  }

  static IconData _iconForOperation(ChdOperation op) {
    return switch (op) {
      ChdOperation.read => Icons.info_outline,
      ChdOperation.compress => Icons.compress,
      ChdOperation.extract => Icons.unarchive,
      ChdOperation.hash => Icons.tag,
    };
  }
}
