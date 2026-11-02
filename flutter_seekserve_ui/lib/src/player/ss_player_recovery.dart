/// An error of the player that the video gets over (app BUG-279): mpv may
/// say it could not open the hardware decoder, take the software one by
/// itself and play on. The error goes once the video moves on after it.
///
/// The first point said after the error is where the video stands, the
/// point a player opens at included: only a later point past it says that
/// the video goes.
class SsPlayerRecovery {
  bool _failed = false;
  Duration? _from;

  /// The player told an error.
  void failed() {
    _failed = true;
    _from = null;
  }

  /// The player is at [position]: whether this says the video got over the
  /// error, once.
  bool moved(Duration position) {
    if (!_failed) return false;
    final from = _from;
    // A jump back stands the video at a new point.
    if (from == null || position <= from) {
      _from = position;
      return false;
    }
    _failed = false;
    return true;
  }
}
