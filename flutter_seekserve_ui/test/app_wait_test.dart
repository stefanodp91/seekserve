import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

class _Port implements SsSubtitlePort {
  bool isPlaying = true;
  final _playing = StreamController<bool>.broadcast(sync: true);

  @override
  bool get playing => isPlaying;

  @override
  Stream<bool> get playingChanges => _playing.stream;

  @override
  Future<void> pause() async {
    isPlaying = false;
    _playing.add(false);
  }

  @override
  Future<void> play() async {
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

const _strings = SsStrings(holdWaitingLabel: 'In attesa', holdPausedLabel: 'In pausa', holdStoppedLabel: 'Fermo');

Widget _app(Widget child) => MaterialApp(home: Scaffold(body: child));

/// The app's own wait over the video (app DEC-1016): the same panel as a
/// hold of the host, but the video is not paused for it.
void main() {
  testWidgets('the app\'s wait shows its panel with the ring, and its button plays or pauses the video', (tester) async {
    final wait = ValueNotifier<SsHold?>(const SsHold(message: 'Carico il video…'));
    final playing = ValueNotifier(true);
    var taps = 0;
    await tester.pumpWidget(_app(SsAppWaitOverlay(
      wait: wait,
      playing: playing,
      onPlayOrPause: () => taps++,
      strings: _strings,
    )));
    expect(find.text('Carico il video…'), findsOneWidget);
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(find.bySemanticsLabel('In attesa'), findsOneWidget);
    await tester.tap(find.byType(SsHoldButton));
    expect(taps, 1);
    playing.value = false;
    await tester.pump();
    expect(find.bySemanticsLabel('In pausa'), findsOneWidget);
    wait.value = null;
    await tester.pump();
    expect(find.text('Carico il video…'), findsNothing);
  });

  testWidgets('over a hold of the host its button is the hold\'s: a real pause, never the video let go', (tester) async {
    final port = _Port();
    final host = SsSubtitleHostController(port)..hold(const SsHold(message: 'Preparo i sottotitoli…', visible: false));
    var taps = 0;
    await tester.pumpWidget(_app(SsAppWaitOverlay(
      wait: ValueNotifier(const SsHold(message: 'Carico il video…')),
      playing: ValueNotifier(false),
      onPlayOrPause: () => taps++,
      strings: _strings,
      host: host,
    )));
    expect(find.bySemanticsLabel('In attesa'), findsOneWidget);
    await tester.tap(find.byType(SsHoldButton));
    await tester.pump();
    expect(taps, 0);
    expect(host.holdPaused.value, isTrue);
    expect(find.bySemanticsLabel('In pausa'), findsOneWidget);
  });

  testWidgets('a stopped wait of the app has no ring', (tester) async {
    await tester.pumpWidget(_app(SsAppWaitOverlay(
      wait: ValueNotifier(const SsHold(message: 'Tor non è partito', stopped: true)),
      playing: ValueNotifier(true),
      onPlayOrPause: () {},
      strings: _strings,
    )));
    expect(find.byType(CircularProgressIndicator), findsNothing);
    expect(find.bySemanticsLabel('Fermo'), findsOneWidget);
  });

  testWidgets('the ▶/⏸ of the controls has the ring during a wait of the app, plain without', (tester) async {
    final wait = ValueNotifier<SsHold?>(const SsHold(message: 'Carico il video…'));
    var taps = 0;
    await tester.pumpWidget(_app(SsWaitPlayButton(
      plain: const Text('plain'),
      size: 32,
      playing: ValueNotifier(true),
      onPlayOrPause: () => taps++,
      strings: _strings,
      wait: wait,
    )));
    expect(find.byType(SsHoldButton), findsOneWidget);
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    await tester.tap(find.byType(SsHoldButton));
    expect(taps, 1);
    wait.value = null;
    await tester.pump();
    expect(find.text('plain'), findsOneWidget);
  });

  testWidgets('in the centre the button gives way to the panel\'s own', (tester) async {
    await tester.pumpWidget(_app(SsWaitPlayButton(
      plain: const Text('plain'),
      size: 52,
      center: true,
      playing: ValueNotifier(true),
      onPlayOrPause: () {},
      strings: _strings,
      wait: ValueNotifier(const SsHold(message: 'Carico il video…')),
    )));
    expect(find.byType(SsHoldButton), findsNothing);
    expect(find.text('plain'), findsNothing);
  });

  testWidgets('the panel scrolls and keeps its words within 130 %, in landscape with two ways out', (tester) async {
    tester.view.physicalSize = const Size(640, 300);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final held = ValueNotifier<SsHold?>(SsHold(
      message: 'Il video carica lentamente',
      detail: 'Il pezzo che serve arriva piano: circa 4 minuti.\nIl film riparte da solo quando arriva il video',
      actions: [
        SsHoldAction(label: 'Prova un\'altra versione', onPressed: () {}),
        SsHoldAction(label: 'Continua ad aspettare', onPressed: () {}),
      ],
    ));
    await tester.pumpWidget(MediaQuery(
      data: const MediaQueryData(size: Size(640, 300), textScaler: TextScaler.linear(2)),
      child: _app(SsHoldOverlay(hold: held, paused: ValueNotifier(false), onTogglePause: () {}, strings: _strings)),
    ));
    expect(tester.takeException(), isNull);
    final text = tester.widget<Text>(find.text('Il video carica lentamente'));
    expect(MediaQuery.textScalerOf(tester.element(find.byWidget(text))).scale(10), lessThanOrEqualTo(13));
  });

  // App BUG-806, DEC-1023: a film the person paused loads with no circle.
  group('SsLoadingCircle', () {
    Future<void> pump(WidgetTester tester, {required bool buffering, required bool playing, SsHold? wait}) =>
        tester.pumpWidget(Directionality(
          textDirection: TextDirection.ltr,
          child: SsLoadingCircle(
            buffering: Stream<bool>.value(buffering),
            bufferingNow: buffering,
            playing: ValueNotifier(playing),
            appWait: ValueNotifier<SsHold?>(wait),
            circle: const Text('circle'),
          ),
        ));

    testWidgets('a film that wants to go and loads shows the circle', (tester) async {
      await pump(tester, buffering: true, playing: true);
      expect(find.text('circle'), findsOneWidget);
    });

    testWidgets('a film the person paused shows no circle while it loads', (tester) async {
      await pump(tester, buffering: true, playing: false);
      expect(find.text('circle'), findsNothing);
    });

    testWidgets('the app\'s own wait says it instead of the circle', (tester) async {
      await pump(tester, buffering: true, playing: true, wait: const SsHold(message: 'Carico il video…'));
      expect(find.text('circle'), findsNothing);
    });

    testWidgets('no loading, no circle', (tester) async {
      await pump(tester, buffering: false, playing: true);
      expect(find.text('circle'), findsNothing);
    });
  });
}
