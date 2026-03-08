import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';
import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';

import '../main.dart';
import '../router.dart';

/// Home screen: add torrent bar + list of torrents.
class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  SsTorrentManager? _manager;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final m = context.manager;
    if (m != _manager) {
      _manager?.removeListener(_onManagerChanged);
      _manager = m;
      m.addListener(_onManagerChanged);
    }
  }

  @override
  void dispose() {
    _manager?.removeListener(_onManagerChanged);
    super.dispose();
  }

  void _onManagerChanged() {
    if (mounted) setState(() {});
  }

  Future<void> _showAddDialog() async {
    final uri = await SsAddTorrentDialog.show(context);
    if (uri != null && uri.isNotEmpty && mounted) {
      context.manager.addTorrent(uri);
    }
  }

  void _onTorrentTap(TorrentStatus status) {
    Navigator.of(context).pushNamed(
      '/detail',
      arguments: TorrentDetailArgs(torrentId: status.torrentId),
    );
  }

  Future<void> _onTorrentDelete(TorrentStatus status) async {
    final result = await SsDeleteConfirm.show(
      context,
      torrentName: status.name.isEmpty ? status.torrentId : status.name,
    );
    if (result == null || !mounted) return;
    context.manager.removeTorrent(
      status.torrentId,
      deleteFiles: result == false,
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final manager = context.manager;

    final bottomPad = MediaQuery.of(context).padding.bottom;

    return ColoredBox(
      color: theme.background,
      child: Stack(
        children: [
          Column(
            children: [
              // Top bar
              Container(
                padding: EdgeInsets.only(
                  top: MediaQuery.of(context).padding.top + 8,
                  left: 16,
                  right: 16,
                  bottom: 8,
                ),
                color: theme.surface,
                child: Row(
                  children: [
                    Text('SeekServe', style: theme.headingStyle),
                    const Spacer(),
                    if (manager.serverPort != null)
                      GestureDetector(
                        onTap: () => SsServerStatusPanel.show(
                          context,
                          controlPort: manager.serverPort,
                          streamPort: manager.streamPort,
                          authToken: manager.authToken,
                          activeTorrents: manager.entries.length,
                          totalDownloadRate: manager.totalDownloadRate,
                          totalUploadRate: manager.totalUploadRate,
                          startedAt: manager.startedAt,
                        ),
                        child: SsBadge(
                          label: 'PORT ${manager.serverPort}',
                          color: theme.seeding,
                        ),
                      ),
                  ],
                ),
              ),
              // Error banner
              if (manager.errorMessage != null)
                Container(
                  width: double.infinity,
                  padding:
                      const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  color: theme.error.withValues(alpha: 0.15),
                  child: Text(
                    manager.errorMessage!,
                    style: theme.captionStyle.copyWith(color: theme.error),
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              // Torrent list
              Expanded(
                child: SsTorrentList(
                  torrents: manager.statuses,
                  onTap: _onTorrentTap,
                  onDelete: _onTorrentDelete,
                  onTogglePause: (s) => manager.togglePause(s.torrentId),
                ),
              ),
            ],
          ),
          // Floating add button
          Positioned(
            right: 20,
            bottom: bottomPad + 20,
            child: GestureDetector(
              onTap: _showAddDialog,
              child: Container(
                width: 56,
                height: 56,
                decoration: BoxDecoration(
                  color: theme.primary,
                  borderRadius: BorderRadius.circular(16),
                  boxShadow: const [
                    BoxShadow(
                      color: Color(0x40000000),
                      blurRadius: 8,
                      offset: Offset(0, 4),
                    ),
                  ],
                ),
                child: Icon(
                  const IconData(0xe047, fontFamily: 'MaterialIcons'), // add
                  color: theme.onPrimary,
                  size: 28,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
