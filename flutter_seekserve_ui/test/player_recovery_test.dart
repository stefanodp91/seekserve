import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

/// An error of the player that the video gets over (app BUG-279): mpv
/// says it could not open the hardware decoder, takes the software one by
/// itself and the video plays; the error goes once the video moves on.
void main() {
  test('without an error nothing is taken away', () {
    final recovery = SsPlayerRecovery();
    expect(recovery.moved(Duration.zero), isFalse);
    expect(recovery.moved(const Duration(seconds: 1)), isFalse);
  });

  test('the video that moves on after the error takes it away', () {
    final recovery = SsPlayerRecovery()..failed();
    expect(recovery.moved(Duration.zero), isFalse);
    expect(recovery.moved(const Duration(milliseconds: 500)), isTrue);
    // Once is enough.
    expect(recovery.moved(const Duration(seconds: 1)), isFalse);
  });

  test('the first point after the error is where the video starts from, '
      'not a sign that it goes', () {
    // A player opened at the point the person left: the first point said
    // is that one, also when nothing plays.
    final recovery = SsPlayerRecovery()..failed();
    expect(recovery.moved(const Duration(seconds: 2764)), isFalse);
    expect(recovery.moved(const Duration(seconds: 2764)), isFalse);
    expect(recovery.moved(const Duration(seconds: 2765)), isTrue);
  });

  test('a video that stands keeps the error', () {
    final recovery = SsPlayerRecovery()..failed();
    for (var i = 0; i < 5; i++) {
      expect(recovery.moved(const Duration(seconds: 10)), isFalse);
    }
  });

  test('a new error waits for the video to move on again', () {
    final recovery = SsPlayerRecovery()..failed();
    recovery.moved(Duration.zero);
    expect(recovery.moved(const Duration(seconds: 1)), isTrue);

    recovery.failed();
    expect(recovery.moved(const Duration(seconds: 5)), isFalse);
    expect(recovery.moved(const Duration(seconds: 6)), isTrue);
  });

  test('a jump back after the error stands the video at the new point', () {
    final recovery = SsPlayerRecovery()..failed();
    expect(recovery.moved(const Duration(seconds: 100)), isFalse);
    expect(recovery.moved(const Duration(seconds: 40)), isFalse);
    expect(recovery.moved(const Duration(seconds: 41)), isTrue);
  });
}
