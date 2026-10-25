import 'dart:async';

/// The stages of a video player's life, for an app's debug log (app
/// BUG-239, DEC-662): a player that never gets [ready], or whose end never
/// comes, is a player stuck behind another.
enum SsPlayerStage {
  /// The player was made.
  created,

  /// Its engine is ready.
  ready,

  /// Its video output is ready.
  videoOutput,

  /// It opened the stream or the file.
  opened,

  /// It showed the first frame.
  firstFrame,

  /// It started to go.
  disposing,

  /// It is gone.
  disposed,
}

/// A stage reached, with the time since the player was made; [late] when
/// no stage came after it for [SsPlayerLife.lateAfter].
class SsPlayerStageEvent {
  const SsPlayerStageEvent(this.stage, this.elapsed, {this.late = false});

  final SsPlayerStage stage;
  final Duration elapsed;
  final bool late;
}

/// Starts a timer: [Timer.new] outside tests.
typedef SsStartTimer = Timer Function(Duration after, void Function() fire);

/// Follows one player's stages: each once, with its time, and a word when
/// the next one does not come within [lateAfter].
class SsPlayerLife {
  SsPlayerLife({
    required this.lateAfter,
    required void Function(SsPlayerStageEvent event) onEvent,
    Duration Function()? elapsed,
    SsStartTimer? startTimer,
  }) : _onEvent = onEvent,
       _elapsed = elapsed ?? (Stopwatch()..start()).elapsedFunction,
       _startTimer = startTimer ?? Timer.new;

  /// How long a stage may wait for the next one before a word is said.
  final Duration lateAfter;
  final void Function(SsPlayerStageEvent event) _onEvent;
  final Duration Function() _elapsed;
  final SsStartTimer _startTimer;
  final Set<SsPlayerStage> _reached = {};
  Timer? _watch;

  static const _last = {SsPlayerStage.firstFrame, SsPlayerStage.disposed};

  void reach(SsPlayerStage stage) {
    if (!_reached.add(stage)) return;
    _watch?.cancel();
    _watch = null;
    _onEvent(SsPlayerStageEvent(stage, _elapsed()));
    if (_last.contains(stage)) return;
    _watch = _startTimer(lateAfter, () {
      _watch = null;
      _onEvent(SsPlayerStageEvent(stage, _elapsed(), late: true));
    });
  }

  /// No more words about this player.
  void close() {
    _watch?.cancel();
    _watch = null;
  }
}

extension on Stopwatch {
  Duration Function() get elapsedFunction =>
      () => elapsed;
}
