import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

/// A port whose playing state the test drives, as the user's ▶ would.
class _PlayingPort implements SsSubtitlePort {
  _PlayingPort({this.isPlaying = true});

  bool isPlaying;
  var pauses = 0;
  var plays = 0;
  final _playing = StreamController<bool>.broadcast(sync: true);

  /// The user pressed ▶ or ⏸ on the player.
  void userPlays(bool value) {
    isPlaying = value;
    _playing.add(value);
  }

  @override
  bool get playing => isPlaying;

  @override
  Stream<bool> get playingChanges => _playing.stream;

  @override
  Future<void> pause() async {
    pauses++;
    isPlaying = false;
    _playing.add(false);
  }

  @override
  Future<void> play() async {
    plays++;
    isPlaying = true;
    _playing.add(true);
  }

  @override
  Stream<Duration> get position => const Stream.empty();

  @override
  Stream<List<String>> get subtitleLines => const Stream.empty();

  @override
  List<SsSubtitleTrackInfo> get tracks => const [];

  @override
  Future<void> selectTrack(String id) async {}

  @override
  Future<void> selectUri(String uri, {String? title}) async {}

  @override
  Future<void> selectNone() async {}
}

void main() {
  const hold = SsHold(message: 'Preparo i sottotitoli…', progress: 0.2);

  test('a hold pauses, and releasing resumes only what was playing', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    host.hold(hold);
    await pumpEventQueue();
    expect(port.pauses, 1);
    expect(host.held.value, hold);
    host.hold(null);
    await pumpEventQueue();
    expect(port.plays, 1);
    expect(host.held.value, isNull);

    final stopped = _PlayingPort(isPlaying: false);
    final other = SsSubtitleHostController(stopped);
    other.hold(hold);
    other.hold(null);
    await pumpEventQueue();
    expect(stopped.pauses, 0, reason: 'a film paused by the user stays as it was');
    expect(stopped.plays, 0);
  });

  test('a new hold updates without a second pause', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    host.hold(hold);
    await pumpEventQueue();
    host.hold(const SsHold(message: 'Preparo i sottotitoli…', progress: 0.7, remaining: Duration(seconds: 4)));
    await pumpEventQueue();
    expect(port.pauses, 1);
    expect(host.held.value!.progress, 0.7);
  });

  test('play during a hold tells the host and lets the video go (DEC-945)', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    var overridden = 0;
    host.onHoldOverridden = () => overridden++;
    host.hold(hold);
    await pumpEventQueue();
    port.userPlays(true);
    await pumpEventQueue();
    expect(host.held.value, isNull);
    expect(overridden, 1);
    host.hold(null);
    await pumpEventQueue();
    expect(port.plays, 0, reason: 'the user already let it go');
  });

  test('handing over to a real subtitle ends the hold', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    host.hold(hold);
    await pumpEventQueue();
    await host.selectEmbedded('2');
    await pumpEventQueue();
    expect(host.held.value, isNull);
    expect(port.plays, 1);
  });

  testWidgets('a hold shows the message with the bar, and the time left once known', (tester) async {
    final held = ValueNotifier<SsHold?>(
      const SsHold(message: 'Preparo i sottotitoli…', progress: 0.4, remaining: Duration(seconds: 12)),
    );
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SsHoldOverlay(hold: held, strings: SsStrings(holdRemainingBuilder: (d) => 'circa ${d.inSeconds} secondi')),
      ),
    ));
    expect(find.text('Preparo i sottotitoli…'), findsOneWidget);
    expect(tester.widget<LinearProgressIndicator>(find.byType(LinearProgressIndicator)).value, 0.4);
    expect(find.text('circa 12 secondi'), findsOneWidget);

    held.value = const SsHold(message: 'Preparo i sottotitoli…');
    await tester.pump();
    expect(tester.widget<LinearProgressIndicator>(find.byType(LinearProgressIndicator)).value, isNull);
    expect(find.textContaining('secondi'), findsNothing);

    held.value = null;
    await tester.pump();
    expect(find.byType(LinearProgressIndicator), findsNothing);
  });

  testWidgets('a hidden hold keeps the video held and draws nothing (app BUG-336)', (tester) async {
    final held = ValueNotifier<SsHold?>(const SsHold(message: 'Carico… 26 persone collegate', visible: false));
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(body: SsHoldOverlay(hold: held, strings: SsStrings())),
    ));
    expect(find.text('Carico… 26 persone collegate'), findsNothing);
    expect(find.byType(LinearProgressIndicator), findsNothing);

    held.value = const SsHold(message: 'Carico… 26 persone collegate');
    await tester.pump();
    expect(find.text('Carico… 26 persone collegate'), findsOneWidget);
  });

  test('a hidden hold still holds the video (app BUG-336)', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    host.hold(const SsHold(message: 'm', visible: false));
    await pumpEventQueue();
    expect(port.pauses, 1);
    host.hold(const SsHold(message: 'm'));
    await pumpEventQueue();
    expect(port.pauses, 1, reason: 'shown again, still the same hold');
    expect(port.plays, 0);
  });

  test('a hold is visible unless the host says otherwise', () {
    expect(const SsHold(message: 'm').visible, isTrue);
  });
}
