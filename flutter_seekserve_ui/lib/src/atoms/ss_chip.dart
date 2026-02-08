import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A small info chip with an optional icon and label.
class SsChip extends StatelessWidget {
  final IconData? icon;
  final String label;
  final Color? color;

  const SsChip({
    super.key,
    this.icon,
    required this.label,
    this.color,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final c = color ?? theme.onSurface.withValues(alpha: 0.7);

    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        if (icon != null) ...[
          Icon(icon, size: 14, color: c),
          const SizedBox(width: 4),
        ],
        Text(label, style: theme.captionStyle.copyWith(color: c)),
      ],
    );
  }
}
