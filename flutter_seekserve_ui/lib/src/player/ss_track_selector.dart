import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A single option in a [SsTrackSelector].
class SsTrackOption {
  final String id;
  final String label;
  final String? subtitle;

  const SsTrackOption({
    required this.id,
    required this.label,
    this.subtitle,
  });
}

/// A bottom-anchored overlay panel for selecting audio or subtitle tracks.
///
/// Use [SsTrackSelector.show] to display it as a route overlay.
class SsTrackSelector extends StatelessWidget {
  final String title;
  final List<SsTrackOption> options;
  final String? selectedId;
  final ValueChanged<String> onSelected;

  static const _check = IconData(0xe159, fontFamily: 'MaterialIcons'); // check

  const SsTrackSelector({
    super.key,
    required this.title,
    required this.options,
    this.selectedId,
    required this.onSelected,
  });

  /// Shows the selector as a bottom-anchored overlay.
  static Future<String?> show(
    BuildContext context, {
    required String title,
    required List<SsTrackOption> options,
    String? selectedId,
  }) {
    return Navigator.of(context).push<String>(
      PageRouteBuilder(
        opaque: false,
        barrierDismissible: true,
        barrierColor: const Color(0x80000000),
        pageBuilder: (ctx, anim, anim2) {
          final theme = SsTheme.of(ctx);
          return DefaultTextStyle(
            style: theme.bodyStyle.copyWith(decoration: TextDecoration.none),
            child: SsTrackSelector(
              title: title,
              options: options,
              selectedId: selectedId,
              onSelected: (id) => Navigator.of(ctx).pop(id),
            ),
          );
        },
        transitionsBuilder: (ctx, anim, anim2, child) {
          return SlideTransition(
            position: Tween(
              begin: const Offset(0, 1),
              end: Offset.zero,
            ).animate(CurvedAnimation(parent: anim, curve: Curves.easeOutCubic)),
            child: child,
          );
        },
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    return Align(
      alignment: Alignment.bottomCenter,
      child: Container(
        constraints: const BoxConstraints(maxHeight: 320),
        margin: const EdgeInsets.only(left: 16, right: 16, bottom: 16),
        decoration: BoxDecoration(
          color: theme.surface,
          borderRadius: BorderRadius.circular(theme.borderRadius * 2),
          border: Border.all(color: theme.onSurface.withValues(alpha: 0.1)),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
              child: Text(title, style: theme.headingStyle),
            ),
            Flexible(
              child: ListView.builder(
                shrinkWrap: true,
                padding: const EdgeInsets.only(bottom: 8),
                itemCount: options.length,
                itemBuilder: (ctx, i) {
                  final option = options[i];
                  final isSelected = option.id == selectedId;
                  return GestureDetector(
                    behavior: HitTestBehavior.opaque,
                    onTap: () => onSelected(option.id),
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 16,
                        vertical: 12,
                      ),
                      color: isSelected
                          ? theme.primary.withValues(alpha: 0.12)
                          : null,
                      child: Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  option.label,
                                  style: theme.bodyStyle.copyWith(
                                    color: isSelected
                                        ? theme.primary
                                        : theme.onSurface,
                                    fontWeight: isSelected
                                        ? FontWeight.w600
                                        : FontWeight.normal,
                                  ),
                                  maxLines: 1,
                                  overflow: TextOverflow.ellipsis,
                                ),
                                if (option.subtitle != null)
                                  Text(
                                    option.subtitle!,
                                    style: theme.captionStyle,
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                  ),
                              ],
                            ),
                          ),
                          if (isSelected)
                            Icon(_check, size: 20, color: theme.primary),
                        ],
                      ),
                    ),
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}
