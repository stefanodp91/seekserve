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
  final calls = <String>[];
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
    calls.add('play');
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
  Future<void> selectNone() async => calls.add('track off');
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

  // App DEC-988, DEC-1012: with the subtitles on the video never goes on
  // without the lines of its point. The button during a hold is a real pause.
  test('a pause during a hold keeps the video paused after the hold', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    host.hold(hold);
    await pumpEventQueue();
    expect(host.holdPaused.value, isFalse);
    host.toggleHoldPause();
    expect(host.holdPaused.value, isTrue);
    expect(host.held.value, hold, reason: 'still waiting for the lines');
    host.hold(null);
    await pumpEventQueue();
    expect(port.plays, 0, reason: 'the person paused it');
    expect(host.holdPaused.value, isFalse);
  });

  test('a second tap goes back to waiting, and the video goes on at the end', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    host.hold(hold);
    await pumpEventQueue();
    host
      ..toggleHoldPause()
      ..toggleHoldPause();
    expect(host.holdPaused.value, isFalse);
    expect(port.plays, 0, reason: 'the video waits for its lines');
    host.hold(null);
    await pumpEventQueue();
    expect(port.plays, 1);
  });

  test('a video paused before the hold waits paused, and a tap makes it wait to go on', () async {
    final port = _PlayingPort(isPlaying: false);
    final host = SsSubtitleHostController(port);
    host.hold(hold);
    expect(host.holdPaused.value, isTrue);
    host.toggleHoldPause();
    host.hold(null);
    await pumpEventQueue();
    expect(port.plays, 1);
  });

  test('the video started from elsewhere during a hold stops again and keeps waiting', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port);
    host.hold(hold);
    await pumpEventQueue();
    host.toggleHoldPause();
    port.userPlays(true);
    await pumpEventQueue();
    expect(host.held.value, hold);
    expect(port.isPlaying, isFalse);
    expect(port.pauses, 2);
    expect(host.holdPaused.value, isFalse, reason: 'a play is a wish to go on: back to waiting');
    host.hold(null);
    await pumpEventQueue();
    expect(port.plays, 1);
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
        body: SsHoldOverlay(
          hold: held,
          paused: ValueNotifier(false),
          onTogglePause: () {},
          strings: SsStrings(holdRemainingBuilder: (d) => 'circa ${d.inSeconds} secondi'),
        ),
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
      home: Scaffold(
        body: SsHoldOverlay(hold: held, paused: ValueNotifier(false), onTogglePause: () {}, strings: SsStrings()),
      ),
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

  testWidgets('the panel says what the video waits for and the host\'s exits take a tap', (tester) async {
    var withoutSubtitles = 0;
    final held = ValueNotifier<SsHold?>(SsHold(
      message: 'Preparo i sottotitoli…',
      detail: 'Il film riparte da solo quando sono pronti',
      progress: 0.3,
      actions: [SsHoldAction(label: 'Guarda senza sottotitoli', onPressed: () => withoutSubtitles++, emphasized: true)],
    ));
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SsHoldOverlay(hold: held, paused: ValueNotifier(false), onTogglePause: () {}, strings: SsStrings()),
      ),
    ));
    expect(find.text('Il film riparte da solo quando sono pronti'), findsOneWidget);
    await tester.tap(find.text('Guarda senza sottotitoli'));
    expect(withoutSubtitles, 1);
  });

  testWidgets('waiting, the button is a pause with a ring; paused, a play without it', (tester) async {
    final paused = ValueNotifier(false);
    var toggles = 0;
    final held = ValueNotifier<SsHold?>(const SsHold(message: 'Preparo i sottotitoli…'));
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SsHoldOverlay(
          hold: held,
          paused: paused,
          onTogglePause: () => toggles++,
          strings: const SsStrings(holdWaitingLabel: 'In attesa, tocca per mettere in pausa', holdPausedLabel: 'In pausa'),
        ),
      ),
    ));
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(find.bySemanticsLabel('In attesa, tocca per mettere in pausa'), findsOneWidget);
    await tester.tap(find.byType(SsHoldButton));
    expect(toggles, 1);

    paused.value = true;
    await tester.pump();
    expect(find.byType(CircularProgressIndicator), findsNothing);
    expect(find.bySemanticsLabel('In pausa'), findsOneWidget);
  });

  testWidgets('a hold without the panel draws only the button with its ring', (tester) async {
    final held = ValueNotifier<SsHold?>(const SsHold(message: 'Preparo i sottotitoli…', panel: false));
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SsHoldOverlay(hold: held, paused: ValueNotifier(false), onTogglePause: () {}, strings: SsStrings()),
      ),
    ));
    expect(find.byType(SsHoldButton), findsOneWidget);
    expect(find.text('Preparo i sottotitoli…'), findsNothing);
    expect(find.byType(LinearProgressIndicator), findsNothing);
  });

  test('a hold has its panel unless the host says otherwise', () {
    expect(const SsHold(message: 'm').panel, isTrue);
    expect(const SsHold(message: 'm').actions, isEmpty);
  });

  // App DEC-1015: «Guarda senza sottotitoli» is «None» without the choice.
  test('turning off from the host takes the track off first, then tells it, and the video goes on even paused', () async {
    final port = _PlayingPort();
    final host = SsSubtitleHostController(port, onTurnedOff: () => port.calls.add('told'));
    host.hold(hold);
    host.toggleHoldPause();
    host.showLine('Ciao');
    await host.turnOff();
    expect(port.calls, ['track off', 'play', 'told']);
    expect(host.held.value, isNull);
    expect(host.holdPaused.value, isFalse);
    expect(host.line.value, isNull);
  });

  testWidgets('a stopped hold has no ring and no bar, and the button says so', (tester) async {
    final held = ValueNotifier<SsHold?>(
      const SsHold(message: 'I sottotitoli si sono fermati', progress: 0.4, remaining: Duration(seconds: 10), stopped: true),
    );
    await tester.pumpWidget(MaterialApp(
      home: Scaffold(
        body: SsHoldOverlay(
          hold: held,
          paused: ValueNotifier(false),
          onTogglePause: () {},
          strings: SsStrings(holdStoppedLabel: 'Fermo', holdRemainingBuilder: (left) => '${left.inSeconds} s'),
        ),
      ),
    ));
    expect(find.text('I sottotitoli si sono fermati'), findsOneWidget);
    expect(find.byType(CircularProgressIndicator), findsNothing);
    expect(find.byType(LinearProgressIndicator), findsNothing);
    expect(find.text('10 s'), findsNothing);
    expect(find.bySemanticsLabel('Fermo'), findsOneWidget);
  });
}
