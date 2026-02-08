import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';
import 'package:media_kit/media_kit.dart';
import 'package:media_kit_video/media_kit_video.dart';

import '../theme/ss_theme.dart';
import 'ss_buffering_overlay.dart';
import 'ss_player_status_bar.dart';
import 'ss_seek_controls.dart';

/// Complete video player widget with seek controls, buffering overlay,
/// and optional torrent status bar.
///
/// Manages its own [Player] and [VideoController] lifecycle.
class SsVideoPlayer extends StatefulWidget {
  final String streamUrl;
  final TorrentStatus? torrentStatus;

  const SsVideoPlayer({
    super.key,
    required this.streamUrl,
    this.torrentStatus,
  });

  @override
  State<SsVideoPlayer> createState() => _SsVideoPlayerState();
}

class _SsVideoPlayerState extends State<SsVideoPlayer> {
  Player? _player;
  VideoController? _videoController;
  String _error = '';
  bool _noVideoFrames = false;

  @override
  void initState() {
    super.initState();
    _probeAndPlay();
  }

  @override
  void didUpdateWidget(SsVideoPlayer old) {
    super.didUpdateWidget(old);
    if (old.streamUrl != widget.streamUrl) {
      _probeAndPlay();
    }
  }

  Future<void> _probeAndPlay() async {
    setState(() {
      _error = '';
    });

    try {
      final uri = Uri.parse(widget.streamUrl);
      final client = HttpClient()
        ..connectionTimeout = const Duration(seconds: 5);
      final request = await client.openUrl('HEAD', uri);
      final response = await request.close();
      final statusCode = response.statusCode;
      client.close();

      if (!mounted) return;

      if (statusCode >= 200 && statusCode < 400) {
        _startPlayer(widget.streamUrl);
      } else {
        setState(() {
          _error = 'Stream server returned HTTP $statusCode';
        });
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = 'Cannot reach stream: $e';
      });
    }
  }

  void _startPlayer(String url) {
    _player?.dispose();
    _noVideoFrames = false;

    _player = Player();
    _videoController = VideoController(_player!);

    _player!.stream.error.listen((err) {
      if (mounted) setState(() => _error = err);
    });
    _player!.stream.width.listen((w) {
      if (w != null && w > 0 && _noVideoFrames && mounted) {
        setState(() => _noVideoFrames = false);
      }
    });
    // iOS simulator: detect missing video frames after 4s.
    if (Platform.isIOS) {
      Future.delayed(const Duration(seconds: 4), () {
        if (!mounted || _player == null) return;
        final w = _player!.state.width;
        if (w == null || w == 0) {
          setState(() => _noVideoFrames = true);
        }
      });
    }

    _player!.open(Media(url));
    if (mounted) setState(() {});
  }

  /// Buffering indicator rendered inside the [Video] widget's own
  /// compositing pipeline via the `controls` parameter.  This avoids
  /// placing sibling widgets in the Stack next to the Video, which
  /// breaks Android texture compositing and causes a black screen.
  static Widget _bufferingControls(VideoState state) {
    return StreamBuilder<bool>(
      stream: state.widget.controller.player.stream.buffering,
      initialData: true,
      builder: (ctx, snap) {
        final buffering = snap.data ?? false;
        if (!buffering) return const SizedBox.shrink();
        return const Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              SsSpinner(),
              SizedBox(height: 8),
              Text(
                'Buffering...',
                style: TextStyle(
                  fontSize: 13,
                  color: Color(0xCCFFFFFF),
                ),
              ),
            ],
          ),
        );
      },
    );
  }

  @override
  void dispose() {
    _player?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    return Column(
      children: [
        // Video area — no sibling widgets in the Stack when Video is
        // present, otherwise Android texture compositing breaks.
        Expanded(
          child: _videoController != null
              ? Video(
                  controller: _videoController!,
                  controls: _bufferingControls,
                )
              : Center(
                  child: Padding(
                    padding: const EdgeInsets.all(24),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        if (_error.isNotEmpty) ...[
                          Text(
                            _error,
                            style:
                                theme.bodyStyle.copyWith(color: theme.error),
                            textAlign: TextAlign.center,
                          ),
                          const SizedBox(height: 8),
                          Text(
                            widget.streamUrl,
                            style: theme.monoStyle.copyWith(fontSize: 10),
                            textAlign: TextAlign.center,
                            maxLines: 3,
                            overflow: TextOverflow.ellipsis,
                          ),
                        ] else if (_noVideoFrames)
                          Text(
                            'Video rendering not available on iOS Simulator.\n'
                            'Audio and seek work — test on a real device.',
                            style: theme.captionStyle,
                            textAlign: TextAlign.center,
                          )
                        else
                          Text(
                            'Connecting...',
                            style: theme.captionStyle,
                          ),
                      ],
                    ),
                  ),
                ),
        ),

        // Seek controls
        if (_player != null) SsSeekControls(player: _player!),

        // Error / probe info
        if (_error.isNotEmpty)
          Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
            color: theme.error.withValues(alpha: 0.15),
            child: Text(_error,
                style: theme.captionStyle.copyWith(color: theme.error)),
          ),

        // Torrent status bar
        if (widget.torrentStatus != null)
          SsPlayerStatusBar(status: widget.torrentStatus!),
      ],
    );
  }
}
