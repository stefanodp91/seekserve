import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A simple icon button with no Material dependency.
class SsIconButton extends StatefulWidget {
  final IconData icon;
  final VoidCallback? onPressed;
  final double? size;
  final Color? color;

  const SsIconButton({
    super.key,
    required this.icon,
    this.onPressed,
    this.size,
    this.color,
  });

  @override
  State<SsIconButton> createState() => _SsIconButtonState();
}

class _SsIconButtonState extends State<SsIconButton> {
  bool _pressed = false;

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final enabled = widget.onPressed != null;
    final size = widget.size ?? theme.iconSize;
    final color = widget.color ?? theme.onSurface;
    final opacity = enabled ? (_pressed ? 0.5 : 1.0) : 0.3;

    return GestureDetector(
      onTapDown: enabled ? (_) => setState(() => _pressed = true) : null,
      onTapUp: enabled
          ? (_) {
              setState(() => _pressed = false);
              widget.onPressed?.call();
            }
          : null,
      onTapCancel: enabled ? () => setState(() => _pressed = false) : null,
      child: Opacity(
        opacity: opacity,
        child: Padding(
          padding: const EdgeInsets.all(8),
          child: Icon(icon, size: size, color: color),
        ),
      ),
    );
  }

  // Expose the raw IconData so widget tree finds it.
  IconData get icon => widget.icon;
}
