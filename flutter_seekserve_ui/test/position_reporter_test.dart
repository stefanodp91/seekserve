import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

/// Where playback is, told to the app (app WORK-29, wave P2): every interval
/// of real time while the position moves, and at once when the app goes to
/// the background or the player is closed. Never a point the player was not
/// really at: an app that remembers "23:14" must not get "0:00" because the
/// player was closed while it was still opening there.
void main() {
  const minute = Duration(minutes: 1);
  const film = Duration(minutes: 90);

  late Duration now;
  late List<(Duration, Duration)> told;

  SsPositionReporter reporter({
    Duration interval = const Duration(seconds: 5),
    Duration startAt = Duration.zero,
  }) => SsPositionReporter(
    interval: interval,
    startAt: startAt,
    elapsed: () => now,
    onPosition: (position, duration) => told.add((position, duration)),
  );

  /// One second of playback, as the player's stream says it.
  void play(SsPositionReporter r, Duration from, int seconds) {
    for (var s = 1; s <= seconds; s++) {
      now += const Duration(seconds: 1);
      r.update(position: from + Duration(seconds: s));
    }
  }

  setUp(() {
    now = Duration.zero;
    told = [];
  });

  test('says where playback is once per interval, with the latest point', () {
    final r = reporter()..update(duration: film);
    r.update(position: Duration.zero);
    play(r, Duration.zero, 4);
    expect(told, isEmpty);
    play(r, const Duration(seconds: 4), 1);
    expect(told, [(const Duration(seconds: 5), film)]);
    play(r, const Duration(seconds: 5), 4);
    expect(told, hasLength(1));
    play(r, const Duration(seconds: 9), 1);
    expect(told.last, (const Duration(seconds: 10), film));
    expect(told, hasLength(2));
  });

  test('says nothing until the length of the video is known', () {
    final r = reporter();
    r.update(position: Duration.zero);
    play(r, Duration.zero, 12);
    r.flush();
    expect(told, isEmpty);
    // The length arrives: from here on it speaks.
    r.update(duration: film);
    play(r, const Duration(seconds: 12), 5);
    expect(told, [(const Duration(seconds: 17), film)]);
  });

  test('asked at once (background, close), says the point reached without '
      'waiting for the interval, and not twice the same', () {
    final r = reporter()..update(duration: film);
    r.update(position: Duration.zero);
    play(r, Duration.zero, 2);
    r.flush();
    expect(told, [(const Duration(seconds: 2), film)]);
    // Nothing moved since: closing after the background says nothing new.
    r.flush();
    expect(told, hasLength(1));
  });

  test('asked at once before anything is known, says nothing', () {
    reporter().flush();
    final r = reporter()..update(duration: film);
    r.flush();
    expect(told, isEmpty);
  });

  test('a video that starts from a saved point never says "0:00" while it '
      'is still getting there', () {
    final r = reporter(startAt: minute * 23)..update(duration: film);
    // The player's first word, before it is at the saved point.
    r.update(position: Duration.zero);
    r.flush();
    expect(told, isEmpty);
    now += const Duration(seconds: 30);
    r.update(position: Duration.zero);
    expect(told, isEmpty);
    // There: from here on every point counts, also an earlier one the user
    // went back to.
    r.update(position: minute * 23);
    play(r, minute * 23, 5);
    expect(told.last, (minute * 23 + const Duration(seconds: 5), film));
    r.update(position: Duration.zero);
    r.flush();
    expect(told.last, (Duration.zero, film));
  });

  test('a jump that opens the stream again never says "0:00" while it is '
      'getting there: asked at once, it says the last real point', () {
    final r = reporter()..update(duration: film);
    r.update(position: Duration.zero);
    play(r, Duration.zero, 7);
    expect(told.last, (const Duration(seconds: 5), film));
    // The user jumps to 50:00 while the video waits for data: the player
    // opens the stream again there, and says "0:00" first.
    r.restartAt(minute * 50);
    now += const Duration(seconds: 20);
    r.update(position: Duration.zero);
    expect(told, hasLength(1));
    r.flush();
    expect(told.last, (const Duration(seconds: 7), film));
    // There.
    r.update(position: minute * 50);
    r.flush();
    expect(told.last, (minute * 50, film));
  });

  test('a paused video says nothing more, and a new length goes with the '
      'next point', () {
    final r = reporter()..update(duration: film);
    r.update(position: Duration.zero);
    play(r, Duration.zero, 5);
    expect(told, hasLength(1));
    // Paused: the stream is silent while time passes.
    now += minute * 10;
    expect(told, hasLength(1));
    r.update(duration: film + minute);
    play(r, const Duration(seconds: 5), 1);
    expect(told.last, (const Duration(seconds: 6), film + minute));
  });
}
