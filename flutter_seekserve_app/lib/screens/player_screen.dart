import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';

import '../main.dart';

/// Player screen wrapping [SsVideoPlayer] with a top bar.
class AppPlayerScreen extends StatefulWidget {
  final String streamUrl;
  final String torrentId;
  final String fileName;

  const AppPlayerScreen({
    super.key,
    required this.streamUrl,
    required this.torrentId,
    required this.fileName,
  });

  @override
  State<AppPlayerScreen> createState() => _AppPlayerScreenState();
}

class _AppPlayerScreenState extends State<AppPlayerScreen> {
  final _playerKey = GlobalKey();
  SsTorrentManager? _manager;
  bool _isFullscreen = false;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final m = context.manager;
    if (m != _manager) {
      _manager?.removeListener(_onChange);
      _manager = m;
      m.addListener(_onChange);
    }
  }

  @override
  void dispose() {
    // Restore system UI when leaving the screen
    if (_isFullscreen) {
      SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
      SystemChrome.setPreferredOrientations(DeviceOrientation.values);
    }
    _manager?.removeListener(_onChange);
    super.dispose();
  }

  void _onChange() {
    if (mounted) setState(() {});
  }

  void _toggleFullscreen() {
    setState(() => _isFullscreen = !_isFullscreen);
    if (_isFullscreen) {
      SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
      SystemChrome.setPreferredOrientations([
        DeviceOrientation.landscapeLeft,
        DeviceOrientation.landscapeRight,
      ]);
    } else {
      SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
      SystemChrome.setPreferredOrientations(DeviceOrientation.values);
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final status = context.manager.getStatus(widget.torrentId);

    final player = SsVideoPlayer(
      key: _playerKey,
      streamUrl: widget.streamUrl,
      torrentStatus: status,
      isFullscreen: _isFullscreen,
      onFullscreenToggle: _toggleFullscreen,
    );

    // Fullscreen: only the video player, no top bar
    if (_isFullscreen) {
      return ColoredBox(
        color: const Color(0xFF000000),
        child: player,
      );
    }

    // Normal: top bar + player
    return ColoredBox(
      color: theme.background,
      child: Column(
        children: [
          // Top bar
          Container(
            padding: EdgeInsets.only(
              top: MediaQuery.of(context).padding.top + 4,
              left: 4,
              right: 16,
              bottom: 4,
            ),
            color: theme.surface,
            child: Row(
              children: [
                SsIconButton(
                  icon: const IconData(0xe092, fontFamily: 'MaterialIcons'),
                  color: theme.onSurface,
                  onPressed: () => Navigator.of(context).pop(),
                ),
                Expanded(
                  child: Text(
                    widget.fileName,
                    style: theme.bodyStyle,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ],
            ),
          ),
          // Player
          Expanded(child: player),
        ],
      ),
    );
  }
}
