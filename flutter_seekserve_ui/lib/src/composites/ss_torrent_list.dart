import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';

import '../theme/ss_theme.dart';
import 'ss_torrent_tile.dart';

/// A scrollable list of torrents with swipe-to-delete.
class SsTorrentList extends StatelessWidget {
  final List<TorrentStatus> torrents;
  final void Function(TorrentStatus status)? onTap;
  final void Function(TorrentStatus status)? onDelete;

  const SsTorrentList({
    super.key,
    required this.torrents,
    this.onTap,
    this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    if (torrents.isEmpty) {
      return Center(
        child: Text('No torrents', style: theme.captionStyle),
      );
    }

    return ListView.builder(
      itemCount: torrents.length,
      itemBuilder: (ctx, i) {
        final status = torrents[i];
        final tile = SsTorrentTile(
          status: status,
          onTap: onTap != null ? () => onTap!(status) : null,
        );

        if (onDelete == null) return tile;

        return Dismissible(
          key: ValueKey(status.torrentId),
          direction: DismissDirection.endToStart,
          background: Container(
            alignment: Alignment.centerRight,
            padding: const EdgeInsets.only(right: 20),
            color: theme.error,
            child: Icon(
              const IconData(0xe1b9, fontFamily: 'MaterialIcons'), // delete
              color: theme.onError,
              size: 24,
            ),
          ),
          confirmDismiss: (_) {
            onDelete!(status);
            // Return false — the item is removed by rebuilding the list,
            // not by Dismissible's own removal (avoids "still part of tree").
            return Future.value(false);
          },
          child: tile,
        );
      },
    );
  }
}
