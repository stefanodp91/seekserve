import 'package:flutter/widgets.dart';

import '../atoms/ss_badge.dart';
import '../theme/ss_theme.dart';

/// Displays the current stream mode as a coloured badge.
class SsStreamModeBadge extends StatelessWidget {
  final String? mode;

  const SsStreamModeBadge({super.key, this.mode});

  @override
  Widget build(BuildContext context) {
    if (mode == null || mode!.isEmpty) return const SizedBox.shrink();

    final theme = SsTheme.of(context);
    Color color;
    String label;

    switch (mode) {
      case 'streaming_first':
        color = theme.buffering;
        label = 'STREAM';
        break;
      case 'download_assist':
        color = theme.downloading;
        label = 'ASSIST';
        break;
      case 'download_first':
        color = theme.completed;
        label = 'DOWNLOAD';
        break;
      default:
        color = theme.paused;
        label = mode!;
    }

    return SsBadge(label: label, color: color);
  }
}
