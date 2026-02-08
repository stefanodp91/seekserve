import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';

import '../atoms/ss_badge.dart';
import '../atoms/ss_progress_bar.dart';
import '../theme/ss_theme.dart';
import '../utils/format.dart';
import 'ss_stream_mode_badge.dart';
import 'ss_transfer_stats.dart';

/// Full detail panel for a torrent showing all available metadata.
class SsTorrentDetail extends StatelessWidget {
  final TorrentStatus status;
  final List<FileInfo>? files;

  const SsTorrentDetail({
    super.key,
    required this.status,
    this.files,
  });

  Color _stateColor(SsThemeData t) {
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

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final color = _stateColor(theme);
    final totalSize = files?.fold<int>(0, (s, f) => s + f.size) ?? 0;

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Name
          Text(
            status.name.isEmpty ? 'Unknown' : status.name,
            style: theme.headingStyle,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
          ),
          const SizedBox(height: 8),

          // State badge + stream mode
          Row(
            children: [
              SsBadge(label: status.state, color: color),
              const SizedBox(width: 8),
              SsStreamModeBadge(mode: status.streamMode),
            ],
          ),
          const SizedBox(height: 12),

          // Progress
          SsProgressBar(value: status.progress, color: color, height: 6),
          const SizedBox(height: 4),
          Text(
            '${(status.progress * 100).toStringAsFixed(1)}% complete',
            style: theme.captionStyle,
          ),
          const SizedBox(height: 12),

          // Transfer stats
          SsTransferStats(status: status),
          const SizedBox(height: 12),

          // Metadata rows
          _DetailRow(label: 'Infohash', value: status.torrentId, theme: theme),
          if (totalSize > 0)
            _DetailRow(
                label: 'Total size', value: formatBytes(totalSize), theme: theme),
          if (files != null)
            _DetailRow(
                label: 'Files', value: '${files!.length}', theme: theme),
          if (status.hasMetadata)
            _DetailRow(label: 'Metadata', value: 'received', theme: theme),
          if (status.selectedFile != null)
            _DetailRow(
              label: 'Selected file',
              value: '#${status.selectedFile}',
              theme: theme,
            ),
          if (status.playheadPiece != null)
            _DetailRow(
              label: 'Playhead piece',
              value: '${status.playheadPiece}',
              theme: theme,
            ),
          if (status.activeDeadlines != null)
            _DetailRow(
              label: 'Active deadlines',
              value: '${status.activeDeadlines}',
              theme: theme,
            ),
        ],
      ),
    );
  }
}

class _DetailRow extends StatelessWidget {
  final String label;
  final String value;
  final SsThemeData theme;

  const _DetailRow({
    required this.label,
    required this.value,
    required this.theme,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          SizedBox(
            width: 130,
            child: Text(label, style: theme.captionStyle),
          ),
          Expanded(
            child: Text(
              value,
              style: theme.monoStyle,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
          ),
        ],
      ),
    );
  }
}
