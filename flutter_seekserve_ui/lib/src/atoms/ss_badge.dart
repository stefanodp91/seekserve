import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A small pill-shaped status badge.
class SsBadge extends StatelessWidget {
  final String label;
  final Color? color;
  final Color? textColor;

  const SsBadge({
    super.key,
    required this.label,
    this.color,
    this.textColor,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final bg = color ?? theme.primary;
    final fg = textColor ?? theme.onPrimary;

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      decoration: BoxDecoration(
        color: bg,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Text(
        label.toUpperCase(),
        style: TextStyle(
          fontSize: 10,
          fontWeight: FontWeight.w700,
          color: fg,
          letterSpacing: 0.5,
        ),
      ),
    );
  }
}
