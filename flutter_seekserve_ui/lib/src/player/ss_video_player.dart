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
  bool _buffering = true;
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
      _buffering = true;
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
          _buffering = false;
        });
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = 'Cannot reach stream: $e';
        _buffering = false;
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
    _player!.stream.buffering.listen((b) {
      if (mounted) setState(() => _buffering = b);
    });

    // Detect missing video frames (iOS simulator limitation).
    // If no video width is reported within 4 seconds of playback starting,
    // show a warning instead of a black screen.
    _player!.stream.width.listen((w) {
      if (w != null && w > 0 && _noVideoFrames && mounted) {
        setState(() => _noVideoFrames = false);
      }
    });
    Future.delayed(const Duration(seconds: 4), () {
      if (!mounted || _player == null) return;
      final w = _player!.state.width;
      if (w == null || w == 0) {
        setState(() => _noVideoFrames = true);
      }
    });

    _player!.open(Media(url));
    if (mounted) setState(() {});
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
        // Video area
        Expanded(
          child: Stack(
            children: [
              if (_videoController != null)
                Positioned.fill(
                  child: Video(
                    controller: _videoController!,
                    controls: NoVideoControls,
                  ),
                )
              else
                Center(
                  child: Padding(
                    padding: const EdgeInsets.all(24),
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        if (_error.isNotEmpty) ...[
                          Text(
                            _error,
                            style: theme.bodyStyle.copyWith(color: theme.error),
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
                        ] else
                          Text(
                            'Connecting...',
                            style: theme.captionStyle,
                          ),
                      ],
                    ),
                  ),
                ),
              SsBufferingOverlay(
                  isBuffering: _buffering && _error.isEmpty),
              // Simulator warning: video frames not available.
              if (_noVideoFrames)
                Center(
                  child: Container(
                    margin: const EdgeInsets.all(32),
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 12),
                    decoration: BoxDecoration(
                      color: theme.surface.withValues(alpha: 0.9),
                      borderRadius:
                          BorderRadius.circular(theme.borderRadius),
                    ),
                    child: Text(
                      'Video rendering not available on iOS Simulator.\n'
                      'Audio and seek work — test on a real device to see video.',
                      style: theme.captionStyle,
                      textAlign: TextAlign.center,
                    ),
                  ),
                ),
            ],
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
            child: Text(_error, style: theme.captionStyle.copyWith(color: theme.error)),
          ),

        // Torrent status bar
        if (widget.torrentStatus != null)
          SsPlayerStatusBar(status: widget.torrentStatus!),
      ],
    );
  }
}
