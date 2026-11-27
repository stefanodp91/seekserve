import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

import 'ss_extra_subtitles.dart';
import 'ss_hold_button.dart';
import 'ss_hold_overlay.dart';
import 'ss_strings.dart';

/// The app's own wait over the video (app DEC-988, DEC-1016): the video that
/// loads, Tor away. The same panel as a hold of the host, but the video is
/// not paused for it: the player goes on by itself when the data come. Its
/// button plays or pauses the video; over a hold of the host it is the
/// hold's, a real pause or back to waiting, never the video let go.
class SsAppWaitOverlay extends StatelessWidget {
  const SsAppWaitOverlay({
    super.key,
    required this.wait,
    required this.playing,
    required this.onPlayOrPause,
    required this.strings,
    this.host,
  });

  final ValueListenable<SsHold?> wait;
  final ValueListenable<bool> playing;
  final VoidCallback onPlayOrPause;
  final SsStrings strings;
  final SsSubtitleHostController? host;

  @override
  Widget build(BuildContext context) {
    final h = host;
    if (h == null) {
      return SsHoldOverlay(hold: wait, paused: _Not(playing), onTogglePause: onPlayOrPause, strings: strings);
    }
    return ValueListenableBuilder<SsHold?>(
      valueListenable: h.held,
      builder: (context, hold, _) => hold == null
          ? SsHoldOverlay(hold: wait, paused: _Not(playing), onTogglePause: onPlayOrPause, strings: strings)
          : SsHoldOverlay(hold: wait, paused: h.holdPaused, onTogglePause: h.toggleHoldPause, strings: strings),
    );
  }
}

/// The ▶/⏸ of the controls (app DEC-1012, DEC-1016): [plain] without a
/// wait; during one of the app or a hold of the host, the waiting button
/// with its ring, which in the [center] gives way to the panel's own.
/// [onTap] after each tap.
class SsWaitPlayButton extends StatelessWidget {
  const SsWaitPlayButton({
    super.key,
    required this.plain,
    required this.size,
    required this.playing,
    required this.onPlayOrPause,
    required this.strings,
    this.host,
    this.wait,
    this.center = false,
    this.onTap,
  });

  final Widget plain;
  final double size;
  final ValueListenable<bool> playing;
  final VoidCallback onPlayOrPause;
  final SsStrings strings;
  final SsSubtitleHostController? host;
  final ValueListenable<SsHold?>? wait;
  final bool center;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final sources = <Listenable>[?host?.held, ?wait];
    if (sources.isEmpty) return plain;
    return ListenableBuilder(
      listenable: Listenable.merge(sources),
      builder: (context, _) {
        final hostHold = host?.held.value;
        final shown = wait?.value ?? hostHold;
        if (shown == null) return plain;
        if (center && shown.visible) return SizedBox.square(dimension: size);
        final h = host;
        final byHost = h != null && hostHold != null;
        return ValueListenableBuilder<bool>(
          valueListenable: byHost ? h.holdPaused : _Not(playing),
          builder: (context, paused, _) => SsHoldButton(
            paused: paused,
            stopped: shown.stopped,
            size: size,
            label: paused
                ? strings.holdPausedLabel
                : (shown.stopped ? strings.holdStoppedLabel : strings.holdWaitingLabel),
            onPressed: () {
              byHost ? h.toggleHoldPause() : onPlayOrPause();
              onTap?.call();
            },
          ),
        );
      },
    );
  }
}

/// The video paused: not [of] playing.
class _Not implements ValueListenable<bool> {
  _Not(this.of);

  final ValueListenable<bool> of;

  @override
  bool get value => !of.value;

  @override
  void addListener(VoidCallback listener) => of.addListener(listener);

  @override
  void removeListener(VoidCallback listener) => of.removeListener(listener);
}

/// The player's own circle while the video loads (app DEC-1016, DEC-1023):
/// only for a film that wants to go, and only while the app's own wait does
/// not say it. A film the person paused shows no circle while it loads (app
/// BUG-806).
class SsLoadingCircle extends StatelessWidget {
  const SsLoadingCircle({
    super.key,
    required this.buffering,
    required this.bufferingNow,
    required this.playing,
    required this.circle,
    this.appWait,
  });

  final Stream<bool> buffering;

  /// What the player says now: the controls can be built again while the
  /// video plays.
  final bool bufferingNow;
  final ValueListenable<bool> playing;
  final ValueListenable<SsHold?>? appWait;
  final Widget circle;

  @override
  Widget build(BuildContext context) => StreamBuilder<bool>(
        stream: buffering,
        initialData: bufferingNow,
        builder: (context, snap) {
          if (snap.data != true) return const SizedBox.shrink();
          final wait = appWait;
          return ListenableBuilder(
            listenable: Listenable.merge([playing, ?wait]),
            builder: (context, _) => playing.value && wait?.value == null ? circle : const SizedBox.shrink(),
          );
        },
      );
}
