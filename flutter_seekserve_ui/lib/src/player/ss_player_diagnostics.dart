import 'dart:async';

import 'package:media_kit/media_kit.dart';
import 'package:media_kit_video/media_kit_video.dart';

import 'ss_player_life.dart';
import 'ss_player_log.dart';
import 'ss_player_probe.dart';
import 'ss_player_unstick.dart';

/// What an app wants to know of the player of an `SsVideoPlayer`, for its
/// debug log (app BUG-238, BUG-239, DEC-662). Without it the player says no
/// more than before and decodes as before.
class SsPlayerDiagnostics {
  const SsPlayerDiagnostics({
    required this.onLog,
    required this.onStage,
    required this.stageLateAfter,
    this.logLevel = MPVLogLevel.v,
    this.softwareDecoding = false,
    this.probe,
  });

  /// The player's messages and errors, with no address ([ssRedactPlayerLog]).
  /// The app caps them ([SsCappedLog]): its cap is the session's.
  final void Function(String line) onLog;

  /// The stages of each player ([SsPlayerLife]).
  final void Function(SsPlayerStageEvent event) onStage;

  /// How long a stage may wait for the next one before [onStage] says so.
  final Duration stageLateAfter;

  /// How much the player says.
  final MPVLogLevel logLevel;

  /// Decodes in software (mpv's `hwdec=no`) instead of the device's decoder.
  final bool softwareDecoding;

  /// Attached to each player while it has an engine.
  final SsPlayerProbe? probe;

  PlayerConfiguration get playerConfiguration =>
      PlayerConfiguration(logLevel: logLevel);

  VideoControllerConfiguration get videoConfiguration => softwareDecoding
      ? const VideoControllerConfiguration(hwdec: 'no')
      : const VideoControllerConfiguration();
}

/// One player followed for [SsPlayerDiagnostics]: its messages, its errors,
/// its stages and its state, until it is gone.
class SsPlayerWatch {
  SsPlayerWatch(this._diagnostics, Player player, VideoController video)
    : _life = SsPlayerLife(
        lateAfter: _diagnostics.stageLateAfter,
        onEvent: _diagnostics.onStage,
      ) {
    _life.reach(SsPlayerStage.created);
    _logs = player.stream.log.listen(
      (log) => _write('${log.level} ${log.prefix}: ${log.text}'),
    );
    final platform = player.platform;
    if (platform != null) {
      _when(platform.waitForPlayerInitialization, SsPlayerStage.ready);
      _when(
        platform.waitForVideoControllerInitializationIfAttached,
        SsPlayerStage.videoOutput,
      );
    }
    _when(video.waitUntilFirstFrameRendered, SsPlayerStage.firstFrame);
    if (platform is NativePlayer) {
      _diagnostics.probe?.attach(platform.getProperty);
      unawaited(_watchVisible(platform));
    }
  }

  final SsPlayerDiagnostics _diagnostics;
  final SsPlayerLife _life;
  StreamSubscription<PlayerLog>? _logs;

  /// The player opened the stream or the file.
  void opened() => _life.reach(SsPlayerStage.opened);

  /// The player stood on a seek and opened the stream again
  /// ([SsPlayerUnstick]).
  void unstuck() => _life.reach(SsPlayerStage.unstuck);

  /// An error of the player, also one it tries again by itself.
  void error(String message) => _write('error: $message');

  /// Disposes [player], saying when it starts and when it is over: an end
  /// that never comes is a player stuck in its engine (app BUG-239).
  Future<void> dispose(Player player) async {
    _diagnostics.probe?.detach();
    _life.reach(SsPlayerStage.disposing);
    await _logs?.cancel();
    _logs = null;
    await player.dispose();
    _life
      ..reach(SsPlayerStage.disposed)
      ..close();
  }

  void _write(String line) => _diagnostics.onLog(ssRedactPlayerLog(line));

  /// Says when an image is on the screen: on Android after the first frame,
  /// once the surface of the video is ready (app BUG-246).
  Future<void> _watchVisible(NativePlayer platform) async {
    try {
      await platform.observeProperty('vo-configured', (configured) async {
        final currentVo = await platform.getProperty(
          'current-vo',
          waitForInitialization: false,
        );
        if (ssIsVisibleOutput(voConfigured: configured, currentVo: currentVo)) {
          _life.reach(SsPlayerStage.visible);
        }
      });
    } catch (_) {
      // The player went before it was ready: nothing to say.
    }
  }

  void _when(Future<void> done, SsPlayerStage stage) {
    unawaited(done.then((_) => _life.reach(stage), onError: (Object _) {}));
  }
}
