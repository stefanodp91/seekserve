import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A text input field with no Material dependency.
class SsTextField extends StatelessWidget {
  final String? hint;
  final TextEditingController? controller;
  final ValueChanged<String>? onSubmitted;
  final Widget? suffix;
  final bool autofocus;

  const SsTextField({
    super.key,
    this.hint,
    this.controller,
    this.onSubmitted,
    this.suffix,
    this.autofocus = false,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    return Container(
      decoration: BoxDecoration(
        color: theme.surface,
        borderRadius: BorderRadius.circular(theme.borderRadius),
        border: Border.all(color: theme.onSurface.withValues(alpha: 0.2)),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        children: [
          Expanded(
            child: EditableText(
              controller: controller ?? TextEditingController(),
              focusNode: FocusNode(),
              style: theme.bodyStyle,
              cursorColor: theme.primary,
              backgroundCursorColor: theme.surface,
              onSubmitted: onSubmitted,
              autofocus: autofocus,
            ),
          ),
          ?suffix,
        ],
      ),
    );
  }
}
