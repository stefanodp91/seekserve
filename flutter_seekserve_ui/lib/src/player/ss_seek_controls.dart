import 'package:flutter/widgets.dart';
import 'package:media_kit/media_kit.dart';

import '../atoms/ss_icon_button.dart';
import '../atoms/ss_slider.dart';
import '../theme/ss_theme.dart';
import '../utils/format.dart';

/// Seek slider + transport buttons for the video player.
class SsSeekControls extends StatelessWidget {
  final Player player;

  /// Called after a seek completes. Receives the target position.
  final void Function(Duration position)? onSeek;

  /// Called after play/pause is toggled. Receives the new playing state.
  final void Function({required bool isPlaying})? onPlayPause;

  const SsSeekControls({
    super.key,
    required this.player,
    this.onSeek,
    this.onPlayPause,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    const replay10 = IconData(0xe524, fontFamily: 'MaterialIcons');
    const playArrow = IconData(0xe4cb, fontFamily: 'MaterialIcons');
    const pause = IconData(0xe47c, fontFamily: 'MaterialIcons');
    const forward10 = IconData(0xe2c5, fontFamily: 'MaterialIcons');

    return Container(
      color: theme.background.withValues(alpha: 0.9),
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      child: StreamBuilder<Duration>(
        stream: player.stream.position,
        builder: (ctx, posSnap) {
          return StreamBuilder<Duration>(
            stream: player.stream.duration,
            builder: (ctx, durSnap) {
              final position = posSnap.data ?? Duration.zero;
              final duration = durSnap.data ?? Duration.zero;
              final maxMs = duration.inMilliseconds.toDouble();
              final posMs = position.inMilliseconds
                  .toDouble()
                  .clamp(0.0, maxMs > 0 ? maxMs : 1.0);

              return Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  // Slider row
                  Row(
                    children: [
                      Text(
                        formatDuration(position),
                        style: theme.captionStyle,
                      ),
                      Expanded(
                        child: SsSlider(
                          value: posMs,
                          max: maxMs > 0 ? maxMs : 1.0,
                          onChanged: (v) {
                            final target = Duration(milliseconds: v.toInt());
                            player.seek(target);
                            onSeek?.call(target);
                          },
                        ),
                      ),
                      Text(
                        formatDuration(duration),
                        style: theme.captionStyle,
                      ),
                    ],
                  ),
                  // Transport buttons
                  StreamBuilder<bool>(
                    stream: player.stream.playing,
                    builder: (ctx, playSnap) {
                      final playing = playSnap.data ?? false;
                      return Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          SsIconButton(
                            icon: replay10,
                            color: theme.onSurface,
                            onPressed: () {
                              final t =
                                  position - const Duration(seconds: 10);
                              final target =
                                  t < Duration.zero ? Duration.zero : t;
                              player.seek(target);
                              onSeek?.call(target);
                            },
                          ),
                          SsIconButton(
                            icon: playing ? pause : playArrow,
                            size: 36,
                            color: theme.onSurface,
                            onPressed: () {
                              player.playOrPause();
                              onPlayPause?.call(isPlaying: !playing);
                            },
                          ),
                          SsIconButton(
                            icon: forward10,
                            color: theme.onSurface,
                            onPressed: () {
                              final t =
                                  position + const Duration(seconds: 10);
                              final target = t > duration ? duration : t;
                              player.seek(target);
                              onSeek?.call(target);
                            },
                          ),
                        ],
                      );
                    },
                  ),
                ],
              );
            },
          );
        },
      ),
    );
  }
}
