import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';

import '../atoms/ss_badge.dart';
import '../atoms/ss_icon_button.dart';
import '../atoms/ss_progress_bar.dart';
import '../theme/ss_theme.dart';
import '../utils/format.dart';

/// A single row representing a torrent in a list.
///
/// Shows name, progress bar, download/upload rate, peer count, and state badge.
class SsTorrentTile extends StatelessWidget {
  final TorrentStatus status;
  final VoidCallback? onTap;
  final VoidCallback? onDelete;
  final VoidCallback? onTogglePause;

  const SsTorrentTile({
    super.key,
    required this.status,
    this.onTap,
    this.onDelete,
    this.onTogglePause,
  });

  Color _stateColor(SsThemeData t) {
    if (status.isPaused) return t.paused;
    // Completed torrents always show as completed.
    if (status.progress >= 0.999) {
      return status.state == 'seeding' ? t.seeding : t.completed;
    }
    switch (status.state) {
      case 'downloading':
      case 'downloading_metadata':
        return t.downloading;
      case 'seeding':
        return t.seeding;
      case 'finished':
        return t.completed;
      case 'checking_files':
      case 'checking_resume_data':
        return t.checking;
      default:
        return t.paused;
    }
  }

  String _stateLabel() {
    if (status.isPaused) return 'PAUSED';
    // Completed torrents: clear label regardless of libtorrent internal state.
    if (status.progress >= 0.999) {
      return status.state == 'seeding' ? 'SEED' : 'DONE';
    }
    switch (status.state) {
      case 'downloading':
        return 'DL';
      case 'downloading_metadata':
        return 'META';
      case 'seeding':
        return 'SEED';
      case 'finished':
        return 'DONE';
      case 'checking_files':
      case 'checking_resume_data':
        return 'CHECK';
      default:
        return status.state.toUpperCase();
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final color = _stateColor(theme);

    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: theme.surface,
          border: Border(
            bottom: BorderSide(color: theme.onSurface.withValues(alpha: 0.06)),
          ),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Row 1: name + pause button + badge
            Row(
              children: [
                Expanded(
                  child: Text(
                    status.name.isEmpty ? status.torrentId : status.name,
                    style: theme.bodyStyle,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                if (onTogglePause != null) ...[
                  const SizedBox(width: 4),
                  SsIconButton(
                    icon: status.isPaused
                        ? const IconData(0xe037, fontFamily: 'MaterialIcons') // play_arrow
                        : const IconData(0xe034, fontFamily: 'MaterialIcons'), // pause
                    color: theme.onSurface,
                    size: 20,
                    onPressed: onTogglePause,
                  ),
                ],
                const SizedBox(width: 8),
                SsBadge(label: _stateLabel(), color: color),
              ],
            ),
            const SizedBox(height: 6),
            // Row 2: progress bar
            SsProgressBar(value: status.progress, color: color),
            const SizedBox(height: 4),
            // Row 3: stats
            Row(
              children: [
                Text(
                  '${(status.progress * 100).toStringAsFixed(1)}%',
                  style: theme.captionStyle,
                ),
                const SizedBox(width: 12),
                Text(
                  '${formatRate(status.downloadRate)} dn',
                  style: theme.captionStyle.copyWith(color: theme.downloading),
                ),
                const SizedBox(width: 8),
                Text(
                  '${formatRate(status.uploadRate)} up',
                  style: theme.captionStyle.copyWith(color: theme.seeding),
                ),
                const Spacer(),
                Text(
                  '${status.numPeers} peers / ${status.numSeeds} seeds',
                  style: theme.captionStyle,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
