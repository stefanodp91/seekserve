/// Tells the app where playback is: every [interval] of real time while the
/// position moves, and at once when asked ([flush]: the app went to the
/// background, the player is closed).
///
/// It never says a point the player was not really at:
///
/// - nothing until the length of the video is known, so a player closed
///   while it was still opening says nothing;
/// - with a [startAt], the "0:00" a player reports before it gets to its
///   starting point does not count until a point after it arrives. An app
///   that remembers "23:14" would otherwise get "0:00" back from a player
///   closed too early.
///
/// The first word comes an [interval] after a point and the length are both
/// known, not at once: opening a video is not a reason to write anything.
class SsPositionReporter {
  SsPositionReporter({
    required this.interval,
    required this.onPosition,
    this.startAt = Duration.zero,
    Duration Function()? elapsed,
  }) : _elapsed = elapsed ?? _stopwatchOf();

  /// The least real time between two words. Zero or less: every point.
  final Duration interval;

  /// Where the video was asked to start; zero from the beginning.
  final Duration startAt;

  final void Function(Duration position, Duration duration) onPosition;

  /// A clock that never goes back.
  final Duration Function() _elapsed;

  Duration? _position;
  Duration _duration = Duration.zero;

  /// False while the player, asked to start from [startAt], has not said a
  /// point after the beginning yet.
  late bool _arrived = startAt <= Duration.zero;

  Duration? _toldPosition;
  Duration? _countingSince;

  static Duration Function() _stopwatchOf() {
    final watch = Stopwatch()..start();
    return () => watch.elapsed;
  }

  /// What the player's streams say: a new [position], a new [duration], or
  /// both.
  void update({Duration? position, Duration? duration}) {
    if (duration != null) _duration = duration;
    if (position != null) {
      if (!_arrived) {
        if (position <= Duration.zero) return;
        _arrived = true;
      }
      _position = position;
    }
    if (_position == null || _duration <= Duration.zero) return;
    // The interval counts from when both are known. Only a new point is a
    // reason to speak: a new length goes with the next one.
    final now = _elapsed();
    final since = _countingSince ??= now;
    if (position != null && now - since >= interval) _tell(now);
  }

  /// The player opens the stream again at [target] (a jump while playback
  /// waits for data): as at the start, the "0:00" it may say first does not
  /// count until it is there. The last real point stays the one to say.
  void restartAt(Duration target) {
    _arrived = target <= Duration.zero;
  }

  /// Says the point reached at once, if there is one the app does not have
  /// yet.
  void flush() {
    if (_position == null || _duration <= Duration.zero) return;
    if (_position == _toldPosition) return;
    _tell(_elapsed());
  }

  void _tell(Duration now) {
    final position = _position!;
    _toldPosition = position;
    _countingSince = now;
    onPosition(position, _duration);
  }
}
