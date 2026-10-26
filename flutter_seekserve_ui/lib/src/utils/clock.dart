/// The time passed since this call, read when wanted: the clock of the
/// player's watches outside tests.
Duration Function() ssElapsedFromNow() {
  final watch = Stopwatch()..start();
  return () => watch.elapsed;
}
