import 'dart:async';
import 'dart:io';

import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';
import 'package:media_kit/media_kit.dart';
import 'package:media_kit_video/media_kit_video.dart';

import '../atoms/ss_icon_button.dart';
import '../atoms/ss_slider.dart';
import '../theme/ss_theme.dart';
import '../utils/format.dart';
import 'ss_buffering_overlay.dart';
import 'ss_player_status_bar.dart';
import 'ss_track_selector.dart';

/// Complete video player widget with overlay controls, seek bar,
/// buffering indicator, and fullscreen toggle.
///
/// Manages its own [Player] and [VideoController] lifecycle.
/// Controls appear on tap and auto-hide after a few seconds.
class SsVideoPlayer extends StatefulWidget {
  final String streamUrl;
  final TorrentStatus? torrentStatus;
  final bool isFullscreen;
  final VoidCallback? onFullscreenToggle;

  /// All files in the torrent. The player filters subtitles internally.
  final List<FileInfo> torrentFiles;

  /// Generates an HTTP stream URL for a file by its index.
  final String Function(int fileIndex)? streamUrlBuilder;

  /// Requests SeekServe to prioritize downloading a file.
  final void Function(int fileIndex)? onFileSelectRequested;

  const SsVideoPlayer({
    super.key,
    required this.streamUrl,
    this.torrentStatus,
    this.isFullscreen = false,
    this.onFullscreenToggle,
    this.torrentFiles = const [],
    this.streamUrlBuilder,
    this.onFileSelectRequested,
  });

  @override
  State<SsVideoPlayer> createState() => _SsVideoPlayerState();
}

class _SsVideoPlayerState extends State<SsVideoPlayer> {
  Player? _player;
  VideoController? _videoController;
  String _error = '';
  bool _noVideoFrames = false;
  Timer? _formatRetryTimer;

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
      if (!mounted) return;
      // "Failed to recognize file format" means the torrent hasn't buffered
      // enough data yet. Retry silently after 4 seconds.
      if (err.contains('recognize file format') || err.contains('Invalid data found')) {
        _formatRetryTimer?.cancel();
        _formatRetryTimer = Timer(const Duration(seconds: 4), () {
          if (mounted) _startPlayer(url);
        });
        return;
      }
      setState(() => _error = err);
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

  Widget _buildControls(VideoState state) {
    return _PlayerOverlay(
      player: state.widget.controller.player,
      torrentStatus: widget.torrentStatus,
      error: _error,
      isFullscreen: widget.isFullscreen,
      onFullscreenToggle: widget.onFullscreenToggle,
      torrentFiles: widget.torrentFiles,
      streamUrlBuilder: widget.streamUrlBuilder,
      onFileSelectRequested: widget.onFileSelectRequested,
    );
  }

  @override
  void dispose() {
    _formatRetryTimer?.cancel();
    _player?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    if (_videoController != null) {
      return Video(
        controller: _videoController!,
        controls: _buildControls,
      );
    }

    // Error / connecting state
    return ColoredBox(
      color: theme.background,
      child: Center(
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
              ] else if (_noVideoFrames)
                Text(
                  'Video rendering not available on iOS Simulator.\n'
                  'Audio and seek work — test on a real device.',
                  style: theme.captionStyle,
                  textAlign: TextAlign.center,
                )
              else
                Text('Connecting...', style: theme.captionStyle),
            ],
          ),
        ),
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Overlay controls (private)
// ---------------------------------------------------------------------------

/// Full-screen overlay rendered via Video's `controls` parameter.
///
/// Tap anywhere to show/hide. Auto-hides after 4 seconds of inactivity.
class _PlayerOverlay extends StatefulWidget {
  final Player player;
  final TorrentStatus? torrentStatus;
  final String error;
  final bool isFullscreen;
  final VoidCallback? onFullscreenToggle;
  final List<FileInfo> torrentFiles;
  final String Function(int fileIndex)? streamUrlBuilder;
  final void Function(int fileIndex)? onFileSelectRequested;

  const _PlayerOverlay({
    required this.player,
    this.torrentStatus,
    this.error = '',
    this.isFullscreen = false,
    this.onFullscreenToggle,
    this.torrentFiles = const [],
    this.streamUrlBuilder,
    this.onFileSelectRequested,
  });

  @override
  State<_PlayerOverlay> createState() => _PlayerOverlayState();
}

class _PlayerOverlayState extends State<_PlayerOverlay> {
  bool _visible = true;
  Timer? _hideTimer;

  static const _fullscreen = IconData(0xe2cb, fontFamily: 'MaterialIcons');
  static const _fullscreenExit = IconData(0xe2cc, fontFamily: 'MaterialIcons');
  static const _playArrow = IconData(0xe4cb, fontFamily: 'MaterialIcons');
  static const _pause = IconData(0xe47c, fontFamily: 'MaterialIcons');
  static const _replay10 = IconData(0xe524, fontFamily: 'MaterialIcons');
  static const _forward10 = IconData(0xe2c5, fontFamily: 'MaterialIcons');
  static const _audiotrack = IconData(0xe0b6, fontFamily: 'MaterialIcons');
  static const _subtitles = IconData(0xe619, fontFamily: 'MaterialIcons');

  @override
  void initState() {
    super.initState();
    _scheduleHide();
  }

  @override
  void dispose() {
    _hideTimer?.cancel();
    super.dispose();
  }

  void _scheduleHide() {
    _hideTimer?.cancel();
    _hideTimer = Timer(const Duration(seconds: 4), () {
      if (mounted) setState(() => _visible = false);
    });
  }

  void _toggle() {
    setState(() {
      _visible = !_visible;
      if (_visible) _scheduleHide();
    });
  }

  void _onInteraction() => _scheduleHide();

  List<FileInfo> get _subtitleFiles =>
      widget.torrentFiles.where((f) => f.isSubtitle).toList();

  bool get _hasExternalSubtitles =>
      _subtitleFiles.isNotEmpty && widget.streamUrlBuilder != null;

  Future<void> _showAudioTrackSelector() async {
    final tracks = widget.player.state.tracks.audio;
    final current = widget.player.state.track.audio;

    final options = tracks
        .where((t) => t.id != 'auto')
        .map((t) => SsTrackOption(
              id: t.id,
              label: t.title ?? t.language ?? 'Track ${t.id}',
              subtitle: t.language,
            ))
        .toList();

    if (options.isEmpty) return;

    final selected = await SsTrackSelector.show(
      context,
      title: 'Audio',
      options: options,
      selectedId: current.id,
    );

    if (selected != null && mounted) {
      final track = tracks.firstWhere((t) => t.id == selected);
      widget.player.setAudioTrack(track);
    }
    _onInteraction();
  }

  Future<void> _showSubtitleSelector() async {
    // Embedded subtitle tracks from the media container
    final embeddedTracks = widget.player.state.tracks.subtitle
        .where((t) => t.id != 'auto' && t.id != 'no')
        .toList();
    final current = widget.player.state.track.subtitle;

    final options = <SsTrackOption>[
      const SsTrackOption(id: 'off', label: 'Off'),
    ];

    // Add embedded tracks
    for (final t in embeddedTracks) {
      options.add(SsTrackOption(
        id: 'embedded:${t.id}',
        label: t.title ?? t.language ?? 'Track ${t.id}',
        subtitle: t.language,
      ));
    }

    // Add external subtitle files from the torrent
    for (final f in _subtitleFiles) {
      options.add(SsTrackOption(
        id: 'external:${f.index}',
        label: f.name,
      ));
    }

    // Determine current selection ID
    String? selectedId;
    if (current.id == 'no' || current.id == 'auto') {
      selectedId = 'off';
    } else {
      // Check if it's an embedded track
      final isEmbedded = embeddedTracks.any((t) => t.id == current.id);
      if (isEmbedded) {
        selectedId = 'embedded:${current.id}';
      }
      // External tracks don't have a stable "current" ID from media_kit,
      // so we can't reliably highlight them after selection.
    }

    final selected = await SsTrackSelector.show(
      context,
      title: 'Subtitles',
      options: options,
      selectedId: selectedId,
    );

    if (selected == null || !mounted) return;

    if (selected == 'off') {
      widget.player.setSubtitleTrack(SubtitleTrack.no());
    } else if (selected.startsWith('embedded:')) {
      final id = selected.replaceFirst('embedded:', '');
      final track = embeddedTracks.firstWhere((t) => t.id == id);
      widget.player.setSubtitleTrack(track);
    } else if (selected.startsWith('external:')) {
      final fileIndex = int.parse(selected.replaceFirst('external:', ''));
      // Prioritize download of the subtitle file
      widget.onFileSelectRequested?.call(fileIndex);
      final url = widget.streamUrlBuilder!(fileIndex);
      final file = _subtitleFiles.firstWhere((f) => f.index == fileIndex);
      widget.player.setSubtitleTrack(
        SubtitleTrack.uri(url, title: file.name),
      );
    }
    _onInteraction();
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      fit: StackFit.expand,
      children: [
        // Background tap area — catches taps not handled by controls
        GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: _toggle,
        ),
        // Buffering indicator (always visible when buffering)
        StreamBuilder<bool>(
          stream: widget.player.stream.buffering,
          initialData: true,
          builder: (ctx, snap) {
            if (snap.data != true) return const SizedBox.shrink();
            return const Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  SsSpinner(),
                  SizedBox(height: 8),
                  Text(
                    'Buffering...',
                    style: TextStyle(fontSize: 13, color: Color(0xCCFFFFFF)),
                  ),
                ],
              ),
            );
          },
        ),
        // Controls overlay (shown/hidden with fade)
        IgnorePointer(
          ignoring: !_visible,
          child: AnimatedOpacity(
            opacity: _visible ? 1.0 : 0.0,
            duration: const Duration(milliseconds: 250),
            child: _buildControlsPanel(),
          ),
        ),
      ],
    );
  }

  Widget _buildControlsPanel() {
    final theme = SsTheme.of(context);

    return Column(
      children: [
        // -- Top gradient: track selectors + fullscreen --
        Container(
          decoration: const BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.topCenter,
              end: Alignment.bottomCenter,
              colors: [Color(0xCC000000), Color(0x00000000)],
            ),
          ),
          padding: EdgeInsets.only(
            top: widget.isFullscreen ? 16.0 : 4.0,
            left: 4,
            right: 4,
          ),
          child: StreamBuilder<Tracks>(
            stream: widget.player.stream.tracks,
            builder: (ctx, tracksSnap) {
              final tracks = tracksSnap.data;
              final hasMultipleAudio =
                  (tracks?.audio.where((t) => t.id != 'auto').length ?? 0) > 1;
              final hasEmbeddedSubs =
                  (tracks?.subtitle.where((t) => t.id != 'auto' && t.id != 'no').length ?? 0) > 0;
              final showAudio = hasMultipleAudio;
              final showSubs = hasEmbeddedSubs || _hasExternalSubtitles;

              return Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  if (showAudio)
                    SsIconButton(
                      icon: _audiotrack,
                      color: const Color(0xFFFFFFFF),
                      onPressed: () {
                        _showAudioTrackSelector();
                        _onInteraction();
                      },
                    ),
                  if (showSubs)
                    SsIconButton(
                      icon: _subtitles,
                      color: const Color(0xFFFFFFFF),
                      onPressed: () {
                        _showSubtitleSelector();
                        _onInteraction();
                      },
                    ),
                  SsIconButton(
                    icon: widget.isFullscreen ? _fullscreenExit : _fullscreen,
                    color: const Color(0xFFFFFFFF),
                    onPressed: () {
                      widget.onFullscreenToggle?.call();
                      _onInteraction();
                    },
                  ),
                ],
              );
            },
          ),
        ),

        // -- Center: play/pause --
        const Spacer(),
        StreamBuilder<bool>(
          stream: widget.player.stream.playing,
          builder: (ctx, snap) {
            final playing = snap.data ?? false;
            return SsIconButton(
              icon: playing ? _pause : _playArrow,
              size: 52,
              color: const Color(0xFFFFFFFF),
              onPressed: () {
                widget.player.playOrPause();
                _onInteraction();
              },
            );
          },
        ),
        const Spacer(),

        // -- Bottom gradient: seek + transport + status --
        Container(
          decoration: const BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment.bottomCenter,
              end: Alignment.topCenter,
              colors: [Color(0xCC000000), Color(0x00000000)],
            ),
          ),
          padding: EdgeInsets.only(
            bottom: widget.isFullscreen ? 16.0 : 0.0,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              // Seek bar
              _buildSeekBar(theme),
              // Transport buttons
              _buildTransportRow(),
              // Error
              if (widget.error.isNotEmpty)
                Container(
                  width: double.infinity,
                  padding:
                      const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                  color: theme.error.withValues(alpha: 0.15),
                  child: Text(
                    widget.error,
                    style: theme.captionStyle.copyWith(color: theme.error),
                  ),
                ),
              // Torrent status bar
              if (widget.torrentStatus != null)
                SsPlayerStatusBar(status: widget.torrentStatus!),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildSeekBar(SsThemeData theme) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: StreamBuilder<Duration>(
        stream: widget.player.stream.position,
        builder: (ctx, posSnap) {
          return StreamBuilder<Duration>(
            stream: widget.player.stream.duration,
            builder: (ctx, durSnap) {
              final position = posSnap.data ?? Duration.zero;
              final duration = durSnap.data ?? Duration.zero;
              final maxMs = duration.inMilliseconds.toDouble();
              final posMs = position.inMilliseconds
                  .toDouble()
                  .clamp(0.0, maxMs > 0 ? maxMs : 1.0);

              return Row(
                children: [
                  Text(
                    formatDuration(position),
                    style: const TextStyle(
                      color: Color(0xCCFFFFFF),
                      fontSize: 12,
                    ),
                  ),
                  Expanded(
                    child: SsSlider(
                      value: posMs,
                      max: maxMs > 0 ? maxMs : 1.0,
                      activeColor: theme.primary,
                      trackColor: const Color(0x40FFFFFF),
                      onChanged: (v) {
                        widget.player
                            .seek(Duration(milliseconds: v.toInt()));
                        _onInteraction();
                      },
                    ),
                  ),
                  Text(
                    formatDuration(duration),
                    style: const TextStyle(
                      color: Color(0xCCFFFFFF),
                      fontSize: 12,
                    ),
                  ),
                ],
              );
            },
          );
        },
      ),
    );
  }

  Widget _buildTransportRow() {
    return StreamBuilder<bool>(
      stream: widget.player.stream.playing,
      builder: (ctx, snap) {
        final playing = snap.data ?? false;
        return StreamBuilder<Duration>(
          stream: widget.player.stream.position,
          builder: (ctx, posSnap) {
            final position = posSnap.data ?? Duration.zero;
            return StreamBuilder<Duration>(
              stream: widget.player.stream.duration,
              builder: (ctx, durSnap) {
                final duration = durSnap.data ?? Duration.zero;
                return Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    SsIconButton(
                      icon: _replay10,
                      color: const Color(0xFFFFFFFF),
                      onPressed: () {
                        final t = position - const Duration(seconds: 10);
                        widget.player
                            .seek(t < Duration.zero ? Duration.zero : t);
                        _onInteraction();
                      },
                    ),
                    SsIconButton(
                      icon: playing ? _pause : _playArrow,
                      size: 32,
                      color: const Color(0xFFFFFFFF),
                      onPressed: () {
                        widget.player.playOrPause();
                        _onInteraction();
                      },
                    ),
                    SsIconButton(
                      icon: _forward10,
                      color: const Color(0xFFFFFFFF),
                      onPressed: () {
                        final t = position + const Duration(seconds: 10);
                        widget.player.seek(t > duration ? duration : t);
                        _onInteraction();
                      },
                    ),
                  ],
                );
              },
            );
          },
        );
      },
    );
  }
}
