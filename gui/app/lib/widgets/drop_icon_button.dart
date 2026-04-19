import 'package:flutter/material.dart';
import 'package:desktop_drop/desktop_drop.dart';
import '../screens/home_screen.dart';

class DropIconButton extends StatefulWidget {
  final String assetPath;
  final ChdOperation operation;
  final void Function(ChdOperation operation, List<String> paths) onFilesDropped;
  final bool isProcessing;

  const DropIconButton({
    super.key,
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
    _scaleAnimation = Tween<double>(begin: 1.0, end: 1.3).animate(
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
          width: 200,
          height: 200,
          decoration: _isDragging
              ? BoxDecoration(
                  borderRadius: BorderRadius.circular(12),
                  border: Border.all(
                    color: Theme.of(context).colorScheme.primary,
                    width: 2,
                  ),
                  color: Theme.of(context).colorScheme.primary.withValues(alpha: 0.1),
                )
              : null,
          child: ClipRRect(
            borderRadius: BorderRadius.circular(10),
            child: Image.asset(
              widget.assetPath,
              width: 200,
              height: 200,
              fit: BoxFit.contain,
              errorBuilder: (_, e, st) => Container(
                width: 190,
                height: 190,
                color: Theme.of(context).colorScheme.surfaceContainerHighest,
                child: Icon(
                  _iconForOperation(widget.operation),
                  size: 64,
                  color: Theme.of(context).colorScheme.primary,
                ),
              ),
            ),
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
