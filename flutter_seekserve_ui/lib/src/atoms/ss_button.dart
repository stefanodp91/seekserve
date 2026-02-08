import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// Button variant for [SsButton].
enum SsButtonVariant { primary, secondary, danger }

/// A custom button widget with no Material dependency.
class SsButton extends StatefulWidget {
  final String label;
  final VoidCallback? onPressed;
  final SsButtonVariant variant;
  final double? width;

  const SsButton({
    super.key,
    required this.label,
    this.onPressed,
    this.variant = SsButtonVariant.primary,
    this.width,
  });

  @override
  State<SsButton> createState() => _SsButtonState();
}

class _SsButtonState extends State<SsButton> {
  bool _pressed = false;

  Color _bgColor(SsThemeData t) {
    switch (widget.variant) {
      case SsButtonVariant.primary:
        return t.primary;
      case SsButtonVariant.secondary:
        return t.surface;
      case SsButtonVariant.danger:
        return t.error;
    }
  }

  Color _fgColor(SsThemeData t) {
    switch (widget.variant) {
      case SsButtonVariant.primary:
        return t.onPrimary;
      case SsButtonVariant.secondary:
        return t.onSurface;
      case SsButtonVariant.danger:
        return t.onError;
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final enabled = widget.onPressed != null;
    final opacity = enabled ? (_pressed ? 0.7 : 1.0) : 0.4;

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
        child: Container(
          width: widget.width,
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
          decoration: BoxDecoration(
            color: _bgColor(theme),
            borderRadius: BorderRadius.circular(theme.borderRadius),
          ),
          alignment: Alignment.center,
          child: Text(
            widget.label,
            style: theme.bodyStyle.copyWith(
              color: _fgColor(theme),
              fontWeight: FontWeight.w600,
            ),
          ),
        ),
      ),
    );
  }
}
