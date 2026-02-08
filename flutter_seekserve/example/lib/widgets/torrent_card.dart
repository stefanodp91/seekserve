import 'package:flutter/material.dart';

import '../providers/seekserve_provider.dart';

class TorrentCard extends StatelessWidget {
  final TorrentEntry entry;
  final VoidCallback onTap;

  const TorrentCard({super.key, required this.entry, required this.onTap});

  String _formatRate(double bytesPerSec) {
    if (bytesPerSec < 1024) return '${bytesPerSec.toStringAsFixed(0)} B/s';
    if (bytesPerSec < 1024 * 1024) {
      return '${(bytesPerSec / 1024).toStringAsFixed(1)} KB/s';
    }
    return '${(bytesPerSec / (1024 * 1024)).toStringAsFixed(1)} MB/s';
  }

  @override
  Widget build(BuildContext context) {
    final status = entry.status;
    final name = status?.name ?? entry.torrentId.substring(0, 8);
    final progress = status?.progress ?? 0.0;
    final dlRate = status?.downloadRate ?? 0.0;
    final peers = status?.numPeers ?? 0;
    final state = status?.state.toUpperCase() ?? 'LOADING';

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                name,
                style: Theme.of(context).textTheme.titleMedium,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
              const SizedBox(height: 8),
              LinearProgressIndicator(
                value: progress,
                borderRadius: BorderRadius.circular(4),
              ),
              const SizedBox(height: 8),
              Row(
                children: [
                  Text('${(progress * 100).toStringAsFixed(1)}%'),
                  const SizedBox(width: 12),
                  const Icon(Icons.download, size: 14),
                  const SizedBox(width: 4),
                  Text(_formatRate(dlRate)),
                  const SizedBox(width: 12),
                  const Icon(Icons.people, size: 14),
                  const SizedBox(width: 4),
                  Text('$peers'),
                  const Spacer(),
                  Container(
                    padding:
                        const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                    decoration: BoxDecoration(
                      color: Theme.of(context).colorScheme.primaryContainer,
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: Text(
                      state,
                      style: Theme.of(context).textTheme.labelSmall,
                    ),
                  ),
                ],
              ),
              if (entry.metadataReceived)
                Padding(
                  padding: const EdgeInsets.only(top: 8),
                  child: Text(
                    'Tap to view files',
                    style: Theme.of(context).textTheme.bodySmall?.copyWith(
                          color: Theme.of(context).colorScheme.primary,
                        ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
