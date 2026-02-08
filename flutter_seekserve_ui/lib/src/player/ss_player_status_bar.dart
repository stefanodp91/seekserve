import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';

import '../atoms/ss_progress_bar.dart';
import '../composites/ss_stream_mode_badge.dart';
import '../composites/ss_transfer_stats.dart';
import '../theme/ss_theme.dart';

/// Status bar below the video player showing torrent stats during playback.
class SsPlayerStatusBar extends StatelessWidget {
  final TorrentStatus status;

  const SsPlayerStatusBar({super.key, required this.status});

  Color _progressColor(SsThemeData t) {
    switch (status.state) {
      case 'downloading':
        return t.downloading;
      case 'seeding':
      case 'finished':
        return t.completed;
      default:
        return t.paused;
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      color: theme.background.withValues(alpha: 0.9),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          // Download progress
          SsProgressBar(
            value: status.progress,
            color: _progressColor(theme),
            height: 3,
          ),
          const SizedBox(height: 6),
          // Stats row
          Row(
            children: [
              Expanded(child: SsTransferStats(status: status)),
              SsStreamModeBadge(mode: status.streamMode),
            ],
          ),
        ],
      ),
    );
  }
}
