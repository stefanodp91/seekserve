import 'dart:async';

import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

/// The player that stands on a seek after its first frame opens the stream
/// again (app BUG-246, DEC-669): on Android media_kit switches the video
/// output after the first frame and seeks; mpv queues that seek behind a
/// read ahead that waits for a piece the phone does not have, and the video
/// stands with its data on the phone until the piece arrives.
void main() {
  const going = SsPlayerMotion(
    paused: false,
    idle: false,
    waitingForData: false,
    visible: true,
  );
  const standing = SsPlayerMotion(
    paused: false,
    idle: true,
    waitingForData: false,
    visible: false,
  );
  const paused = SsPlayerMotion(
    paused: true,
    idle: true,
    waitingForData: false,
    visible: false,
  );
  const waitingForData = SsPlayerMotion(
    paused: false,
    idle: true,
    waitingForData: true,
    visible: false,
  );

  group('the unstick', () {
    late Duration now;
    late SsPlayerMotion? motion;
    late int reads;
    late int stuck;
    late List<Duration> periods;
    late List<void Function()> ticks;
    late List<_FakeTimer> timers;
    late SsPlayerUnstick unstick;

    Timer fakePeriodic(Duration every, void Function(Timer) tick) {
      final timer = _FakeTimer();
      periods.add(every);
      timers.add(timer);
      ticks.add(() {
        if (timer.isActive) tick(timer);
      });
      return timer;
    }

    /// One read of the player [after] the one before.
    Future<void> tickAfter(Duration after, SsPlayerMotion? seen) async {
      now += after;
      motion = seen;
      for (final tick in List.of(ticks)) {
        tick();
      }
      await Future<void>.delayed(Duration.zero);
    }

    setUp(() {
      now = Duration.zero;
      motion = null;
      reads = 0;
      stuck = 0;
      periods = [];
      ticks = [];
      timers = [];
      unstick = SsPlayerUnstick(
        after: const Duration(seconds: 2),
        read: () async {
          reads++;
          return motion;
        },
        onStuck: () => stuck++,
        elapsed: () => now,
        startPeriodicTimer: fakePeriodic,
      );
    });

    test('reads the player only from its first frame, a quarter of the time '
        'each', () async {
      expect(ticks, isEmpty);

      unstick.arm();

      expect(periods, [const Duration(milliseconds: 500)]);
    });

    test(
      'a player that stands on a seek for the whole time is unstuck once',
      () async {
        unstick.arm();
        for (var i = 0; i < 4; i++) {
          await tickAfter(const Duration(milliseconds: 500), standing);
        }
        expect(stuck, 0, reason: 'standing since the first read, 1.5 s ago');

        await tickAfter(const Duration(milliseconds: 500), standing);
        expect(stuck, 1);

        await tickAfter(const Duration(milliseconds: 500), standing);
        await tickAfter(const Duration(milliseconds: 500), standing);
        expect(stuck, 1, reason: 'once for each player');
        expect(timers.single.isActive, isFalse);
      },
    );

    test('a player that goes in between starts the count again', () async {
      unstick.arm();
      await tickAfter(const Duration(milliseconds: 500), standing);
      await tickAfter(const Duration(milliseconds: 500), standing);
      await tickAfter(const Duration(milliseconds: 500), standing);
      await tickAfter(
        const Duration(milliseconds: 500),
        going.copyWithVisible(false),
      );
      await tickAfter(const Duration(milliseconds: 500), standing);
      await tickAfter(const Duration(milliseconds: 500), standing);
      await tickAfter(const Duration(milliseconds: 500), standing);

      expect(stuck, 0);
    });

    test('a paused player is not stuck', () async {
      unstick.arm();
      for (var i = 0; i < 8; i++) {
        await tickAfter(const Duration(milliseconds: 500), paused);
      }

      expect(stuck, 0);
    });

    test(
      'a player that waits for data is not stuck: the data is missing',
      () async {
        unstick.arm();
        for (var i = 0; i < 8; i++) {
          await tickAfter(const Duration(milliseconds: 500), waitingForData);
        }

        expect(stuck, 0);
      },
    );

    test('a player that cannot say how it stands is left alone', () async {
      unstick.arm();
      for (var i = 0; i < 8; i++) {
        await tickAfter(const Duration(milliseconds: 500), null);
      }

      expect(stuck, 0);
    });

    test('once the image is on the screen and going, the player is not read '
        'any more', () async {
      unstick.arm();
      await tickAfter(const Duration(milliseconds: 500), going);
      final readsSoFar = reads;

      await tickAfter(const Duration(milliseconds: 500), standing);
      for (var i = 0; i < 8; i++) {
        await tickAfter(const Duration(milliseconds: 500), standing);
      }

      expect(timers.single.isActive, isFalse);
      expect(reads, readsSoFar);
      expect(stuck, 0);
    });

    test('armed twice, it reads once', () async {
      unstick
        ..arm()
        ..arm();

      expect(timers, hasLength(1));
    });

    test('closed, it reads no more and is never armed again', () async {
      unstick
        ..arm()
        ..close();
      expect(timers.single.isActive, isFalse);

      unstick.arm();
      expect(timers, hasLength(1));
    });
  });

  group('how the player stands, from its properties', () {
    Future<SsPlayerMotion?> read(Map<String, String> properties) =>
        ssReadPlayerMotion((name) async {
          final value = properties[name];
          if (value == null) throw StateError('no such property: $name');
          return value;
        });

    test('standing on a seek behind the output just switched', () async {
      final motion = await read({
        'pause': 'no',
        'core-idle': 'yes',
        'paused-for-cache': 'no',
        'vo-configured': 'no',
        'current-vo': 'gpu',
      });

      expect(motion?.standsOnASeek, isTrue);
      expect(motion?.visible, isFalse);
    });

    test('going on the screen', () async {
      final motion = await read({
        'pause': 'no',
        'core-idle': 'no',
        'paused-for-cache': 'no',
        'vo-configured': 'yes',
        'current-vo': 'gpu',
      });

      expect(motion?.standsOnASeek, isFalse);
      expect(motion?.visible, isTrue);
    });

    test('the first output, on Android, is not seen', () async {
      final motion = await read({
        'pause': 'no',
        'core-idle': 'no',
        'paused-for-cache': 'no',
        'vo-configured': 'yes',
        'current-vo': 'null',
      });

      expect(motion?.visible, isFalse);
    });

    test('waiting for data is not standing on a seek', () async {
      final motion = await read({
        'pause': 'no',
        'core-idle': 'yes',
        'paused-for-cache': 'yes',
        'vo-configured': 'yes',
        'current-vo': 'gpu',
      });

      expect(motion?.standsOnASeek, isFalse);
    });

    test('a property the player does not give: it cannot say', () async {
      expect(await read({'pause': 'no'}), isNull);
    });

    test(
      'an image is on the screen with an output that is not the empty one',
      () {
        expect(
          ssIsVisibleOutput(voConfigured: 'yes', currentVo: 'gpu'),
          isTrue,
        );
        expect(
          ssIsVisibleOutput(voConfigured: 'yes', currentVo: 'libmpv'),
          isTrue,
        );
        expect(
          ssIsVisibleOutput(voConfigured: 'no', currentVo: 'gpu'),
          isFalse,
        );
        expect(
          ssIsVisibleOutput(voConfigured: 'yes', currentVo: 'null'),
          isFalse,
        );
        expect(ssIsVisibleOutput(voConfigured: 'yes', currentVo: ''), isFalse);
      },
    );
  });
}

extension on SsPlayerMotion {
  SsPlayerMotion copyWithVisible(bool visible) => SsPlayerMotion(
    paused: paused,
    idle: idle,
    waitingForData: waitingForData,
    visible: visible,
  );
}

class _FakeTimer implements Timer {
  bool _active = true;

  @override
  void cancel() => _active = false;

  @override
  bool get isActive => _active;

  @override
  int get tick => 0;
}
