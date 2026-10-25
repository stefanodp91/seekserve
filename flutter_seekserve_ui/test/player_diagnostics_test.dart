import 'dart:async';

import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

/// What the video player says of itself, for the app's debug log (app
/// BUG-238, BUG-239, DEC-662): its messages with no address, at most so
/// many, the stages of its life with the time each took, and its state.
void main() {
  group('the messages carry no address', () {
    test('a stream address, with its token, becomes <url>', () {
      expect(
        ssRedactPlayerLog(
          'Opening http://127.0.0.1:41234/stream/abc123/0?token=secret done',
        ),
        'Opening <url> done',
      );
    });

    test('a file on the phone becomes <file>', () {
      expect(
        ssRedactPlayerLog(
          'Opening /data/user/0/it.app/files/Show.S01E01.mkv for reading',
        ),
        'Opening <file> for reading',
      );
      expect(ssRedactPlayerLog('file:///storage/emulated/0/a.mp4'), '<url>');
    });

    test('a network address becomes <address>', () {
      expect(
        ssRedactPlayerLog('connected to 192.168.1.20:8080 now'),
        'connected to <address> now',
      );
    });

    test('the rest stays as it is', () {
      expect(
        ssRedactPlayerLog('h264_mediacodec: queueInputBuffer failed: -22'),
        'h264_mediacodec: queueInputBuffer failed: -22',
      );
    });
  });

  test('at most so many lines, then one that says the rest is left out', () {
    final lines = <String>[];
    final log = SsCappedLog(max: 2, write: lines.add);

    log
      ..add('one')
      ..add('two')
      ..add('three')
      ..add('four');

    expect(lines, ['one', 'two', 'more lines left out (at most 2)']);
  });

  group('the stages of a player', () {
    late Duration now;
    late List<SsPlayerStageEvent> events;
    late List<void Function()> timers;
    late SsPlayerLife life;

    Timer fakeTimer(Duration after, void Function() fire) {
      timers.add(fire);
      return _FakeTimer();
    }

    setUp(() {
      now = Duration.zero;
      events = [];
      timers = [];
      life = SsPlayerLife(
        lateAfter: const Duration(seconds: 10),
        onEvent: events.add,
        elapsed: () => now,
        startTimer: fakeTimer,
      );
    });

    test('each with the time since the player was made', () {
      life.reach(SsPlayerStage.created);
      now = const Duration(milliseconds: 40);
      life.reach(SsPlayerStage.ready);
      now = const Duration(milliseconds: 900);
      life.reach(SsPlayerStage.firstFrame);

      expect(events.map((e) => (e.stage, e.elapsed.inMilliseconds, e.late)), [
        (SsPlayerStage.created, 0, false),
        (SsPlayerStage.ready, 40, false),
        (SsPlayerStage.firstFrame, 900, false),
      ]);
    });

    test(
      'a stage that does not come in time says where the player stopped',
      () {
        life.reach(SsPlayerStage.created);
        now = const Duration(seconds: 10);
        timers.last();

        expect(events.last.stage, SsPlayerStage.created);
        expect(events.last.late, isTrue);
        expect(events.last.elapsed, const Duration(seconds: 10));
      },
    );

    test('the first frame and the end are not waited after', () {
      life.reach(SsPlayerStage.firstFrame);
      life.reach(SsPlayerStage.disposed);

      expect(timers, isEmpty);
    });

    test('a stage reached again is said once', () {
      life.reach(SsPlayerStage.created);
      life.reach(SsPlayerStage.created);

      expect(events, hasLength(1));
    });
  });

  group('the state of a player', () {
    test('read from its properties', () async {
      final probe = SsPlayerProbe()
        ..attach(
          (name) async => switch (name) {
            'hwdec-current' => 'mediacodec',
            'video-format' => 'h264',
            'demuxer-cache-duration' => '12.500000',
            'paused-for-cache' => 'yes',
            'core-idle' => 'no',
            'decoder-frame-drop-count' => '3',
            _ => '',
          },
        );

      final state = await probe.read();

      expect(state?.decoder, 'mediacodec');
      expect(state?.format, 'h264');
      expect(state?.cacheSeconds, 12.5);
      expect(state?.pausedForCache, isTrue);
      expect(state?.idle, isFalse);
      expect(state?.drops, 3);
    });

    test('what the player does not say stays unknown', () async {
      final probe = SsPlayerProbe()
        ..attach((name) async => throw StateError('no such property'));

      final state = await probe.read();

      expect(state?.decoder, isNull);
      expect(state?.cacheSeconds, isNull);
      expect(state?.pausedForCache, isNull);
      expect(state?.drops, isNull);
    });

    test('no player, no state', () async {
      final probe = SsPlayerProbe();
      expect(await probe.read(), isNull);
      probe
        ..attach((name) async => 'x')
        ..detach();
      expect(await probe.read(), isNull);
    });
  });
}

class _FakeTimer implements Timer {
  @override
  void cancel() {}

  @override
  bool get isActive => false;

  @override
  int get tick => 0;
}
