import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';
import 'ss_button.dart';

/// A confirmation dialog with no Material dependency.
///
/// Use [SsDialog.show] to display it as a route overlay.
class SsDialog extends StatelessWidget {
  final String title;
  final Widget? content;
  final List<SsDialogAction> actions;

  const SsDialog({
    super.key,
    required this.title,
    this.content,
    this.actions = const [],
  });

  /// Shows the dialog as a transparent overlay route.
  static Future<T?> show<T>(
    BuildContext context, {
    required String title,
    Widget? content,
    required List<SsDialogAction> actions,
  }) {
    return Navigator.of(context).push<T>(
      PageRouteBuilder(
        opaque: false,
        barrierDismissible: true,
        barrierColor: const Color(0x80000000),
        pageBuilder: (ctx, anim2, anim3) => SsDialog(
          title: title,
          content: content,
          actions: actions,
        ),
        transitionsBuilder: (ctx, anim, anim2, child) {
          return FadeTransition(opacity: anim, child: child);
        },
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    return Center(
      child: Container(
        constraints: const BoxConstraints(maxWidth: 340),
        margin: const EdgeInsets.all(24),
        padding: const EdgeInsets.all(20),
        decoration: BoxDecoration(
          color: theme.surface,
          borderRadius: BorderRadius.circular(theme.borderRadius * 2),
          border: Border.all(color: theme.onSurface.withValues(alpha: 0.1)),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(title, style: theme.headingStyle),
            if (content != null) ...[
              const SizedBox(height: 12),
              content!,
            ],
            if (actions.isNotEmpty) ...[
              const SizedBox(height: 20),
              Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  for (int i = 0; i < actions.length; i++) ...[
                    if (i > 0) const SizedBox(width: 8),
                    SsButton(
                      label: actions[i].label,
                      variant: actions[i].variant,
                      onPressed: actions[i].onPressed,
                    ),
                  ],
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

/// A single action in an [SsDialog].
class SsDialogAction {
  final String label;
  final SsButtonVariant variant;
  final VoidCallback? onPressed;

  const SsDialogAction({
    required this.label,
    this.variant = SsButtonVariant.secondary,
    this.onPressed,
  });
}
