import 'dart:async';

import '../utils/clock.dart';
import 'ss_player_probe.dart';

/// How a video player stands at one moment, for [SsPlayerUnstick]: mpv's
/// `pause`, `core-idle`, `paused-for-cache`, and whether an image is on the
/// screen ([ssIsVisibleOutput]).
class SsPlayerMotion {
  const SsPlayerMotion({
    required this.paused,
    required this.idle,
    required this.waitingForData,
    required this.visible,
  });

  /// Paused by the user or the app.
  final bool paused;

  /// Doing nothing at all (`core-idle`).
  final bool idle;

  /// Standing until data arrives (`paused-for-cache`).
  final bool waitingForData;

  /// An image is on the screen.
  final bool visible;

  /// Neither paused nor waiting for data, and yet not going: the player
  /// waits for a seek that mpv queued behind a read (app BUG-246).
  bool get standsOnASeek => !paused && idle && !waitingForData;

  /// The image is on the screen and the video goes.
  bool get goesOnScreen => visible && !idle;
}

/// Whether mpv shows images where they can be seen: an output is set up
/// (`vo-configured`) and it is not the empty one media_kit uses on Android
/// until the surface of the video is ready (`current-vo` `null`).
bool ssIsVisibleOutput({
  required String voConfigured,
  required String currentVo,
}) => voConfigured == 'yes' && currentVo.isNotEmpty && currentVo != 'null';

/// Reads how the player stands; null if it does not give one of the
/// properties, or has gone.
Future<SsPlayerMotion?> ssReadPlayerMotion(SsGetPlayerProperty get) async {
  try {
    final paused = await get('pause');
    final idle = await get('core-idle');
    final waitingForData = await get('paused-for-cache');
    final voConfigured = await get('vo-configured');
    final currentVo = await get('current-vo');
    if ([paused, idle, waitingForData, voConfigured].any((v) => v.isEmpty)) {
      return null;
    }
    return SsPlayerMotion(
      paused: paused == 'yes',
      idle: idle == 'yes',
      waitingForData: waitingForData == 'yes',
      visible: ssIsVisibleOutput(
        voConfigured: voConfigured,
        currentVo: currentVo,
      ),
    );
  } catch (_) {
    // Not there in this player, or the player went: it cannot say.
    return null;
  }
}

/// Starts a periodic timer: [Timer.periodic] outside tests.
typedef SsStartPeriodicTimer =
    Timer Function(Duration every, void Function(Timer timer) tick);

/// Opens the stream again when the player stands on a seek after its first
/// frame (app BUG-246, DEC-669).
///
/// On Android media_kit shows the first frame on an output nobody sees,
/// then switches to the surface of the video and seeks to where it is; mpv
/// makes its decoder again and seeks too. If mpv is reading ahead at that
/// moment and the read waits for a piece the phone does not have, both
/// seeks wait behind it: the screen stays black with the data of the point
/// on the phone, until that piece arrives. Opening again aborts the read,
/// as for a seek while loading (app BUG-83), and the surface is ready by
/// then.
///
/// From [arm], at the first frame, it reads the player every quarter of
/// [after]; when the player has stood on a seek for [after], it calls
/// [onStuck], once. It stops reading once the image is on the screen and
/// the video goes, or after [onStuck], or at [close].
class SsPlayerUnstick {
  SsPlayerUnstick({
    required this.after,
    required Future<SsPlayerMotion?> Function() read,
    required void Function() onStuck,
    Duration Function()? elapsed,
    SsStartPeriodicTimer? startPeriodicTimer,
  }) : _read = read,
       _onStuck = onStuck,
       _elapsed = elapsed ?? ssElapsedFromNow(),
       _startPeriodicTimer = startPeriodicTimer ?? Timer.periodic;

  /// How long the player may stand on a seek before the stream opens again.
  final Duration after;
  final Future<SsPlayerMotion?> Function() _read;
  final void Function() _onStuck;
  final Duration Function() _elapsed;
  final SsStartPeriodicTimer _startPeriodicTimer;

  /// How often the player is read.
  Duration get every => after ~/ 4;

  Timer? _timer;
  bool _over = false;
  bool _reading = false;

  /// Since when the player stands on a seek; null while it does not.
  Duration? _standingSince;

  /// The player showed its first frame.
  void arm() {
    if (_over || _timer != null) return;
    _timer = _startPeriodicTimer(every, (_) => unawaited(_check()));
  }

  /// No more reads of this player.
  void close() {
    _over = true;
    _timer?.cancel();
    _timer = null;
  }

  Future<void> _check() async {
    if (_reading || _over) return;
    _reading = true;
    final SsPlayerMotion? motion;
    try {
      motion = await _read();
    } finally {
      _reading = false;
    }
    if (_over || motion == null) return;
    if (motion.goesOnScreen) return close();
    if (!motion.standsOnASeek) {
      _standingSince = null;
      return;
    }
    final now = _elapsed();
    final since = _standingSince ??= now;
    if (now - since < after) return;
    close();
    _onStuck();
  }
}
