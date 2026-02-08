import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A themed container card with optional tap handler.
class SsCard extends StatelessWidget {
  final Widget child;
  final VoidCallback? onTap;
  final EdgeInsets? padding;

  const SsCard({
    super.key,
    required this.child,
    this.onTap,
    this.padding,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    final content = Container(
      padding: padding ?? theme.cardPadding,
      decoration: BoxDecoration(
        color: theme.surface,
        borderRadius: BorderRadius.circular(theme.borderRadius),
        border: Border.all(color: theme.onSurface.withValues(alpha: 0.08)),
      ),
      child: child,
    );

    if (onTap != null) {
      return GestureDetector(onTap: onTap, child: content);
    }
    return content;
  }
}
