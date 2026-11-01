import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';
import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:media_kit/media_kit.dart';

class _FakePort implements SsSubtitlePort {
  final positions = StreamController<Duration>.broadcast();
  final lines = StreamController<List<String>>.broadcast();
  final selected = <String>[];

  @override
  Stream<Duration> get position => positions.stream;

  @override
  Stream<List<String>> get subtitleLines => lines.stream;

  @override
  List<SsSubtitleTrackInfo> get tracks => const [SsSubtitleTrackInfo(id: '2', language: 'eng', title: 'English')];

  @override
  Future<void> selectTrack(String id) async => selected.add('track:$id');

  @override
  Future<void> selectUri(String uri, {String? title}) async => selected.add('uri:$uri');

  @override
  Future<void> selectNone() async => selected.add('none');
}

void main() {
  group('menu', () {
    test('puts the host choices after the tracks and the files, with their note', () {
      final options = ssSubtitleMenuOptions(
        strings: const SsStrings(),
        embedded: const [SsSubtitleTrackInfo(id: '2', language: 'eng', title: 'English')],
        externalFiles: [const FileInfo(index: 4, path: 'film.en.srt', size: 10)],
        extras: const [
          SsExtraSubtitle(id: 'it-translated', label: 'Italiano (tradotto)', note: 'Preparo la traduzione… 40%'),
          SsExtraSubtitle(id: 'later', label: 'Spenta', enabled: false),
        ],
      );
      expect(options.map((o) => o.id), ['off', 'embedded:2', 'external:4', 'extra:it-translated', 'extra:later']);
      expect(options[3].label, 'Italiano (tradotto)');
      expect(options[3].subtitle, 'Preparo la traduzione… 40%');
      expect(options[3].enabled, isTrue);
      expect(options[4].enabled, isFalse);
    });

    test('a subtitle file not on the phone yet says it is arriving', () {
      final options = ssSubtitleMenuOptions(
        strings: const SsStrings(subtitleArriving: 'Arriva…'),
        embedded: const [],
        externalFiles: [
          const FileInfo(index: 4, path: 'Subs/film.it.srt', size: 10),
          const FileInfo(index: 5, path: 'Subs/film.en.srt', size: 10),
        ],
        extras: const [],
        pending: const {4},
      );
      expect(options.map((o) => (o.id, o.subtitle)), [('off', null), ('external:4', 'Arriva…'), ('external:5', null)]);
    });

    test('lists the player tracks without auto and no', () {
      final infos = ssSubtitleTrackInfos([
        SubtitleTrack.auto(),
        SubtitleTrack.no(),
        const SubtitleTrack('1', 'PGS', 'eng'),
        const SubtitleTrack('2', null, 'ita'),
      ]);
      expect(infos.map((t) => (t.id, t.language, t.title)), [('1', 'eng', 'PGS'), ('2', 'ita', null)]);
    });
  });

  group('the host line', () {
    testWidgets('draws italics and rows, and nothing for null', (tester) async {
      final text = ValueNotifier<String?>('<i>Opens here</i>\nsecond row');
      await tester.pumpWidget(Directionality(
        textDirection: TextDirection.ltr,
        child: SsSubtitleLine(text: text),
      ));
      final rich = tester.widget<RichText>(find.byType(RichText).first);
      final spans = <TextSpan>[];
      rich.text.visitChildren((span) {
        if (span is TextSpan && span.text != null) spans.add(span);
        return true;
      });
      expect(spans.map((s) => s.text).join(), 'Opens here\nsecond row');
      expect(spans.firstWhere((s) => s.text == 'Opens here').style?.fontStyle, FontStyle.italic);
      expect(spans.firstWhere((s) => s.text!.contains('second')).style?.fontStyle, isNot(FontStyle.italic));

      text.value = null;
      await tester.pump();
      expect(find.byType(RichText), findsNothing);
    });
  });

  group('the host', () {
    test('passes the lines of a track it listens to and does not draw them', () async {
      final port = _FakePort();
      final host = SsSubtitleHostController(port);
      final got = <String>[];
      await host.listenToTrack('2', got.add);
      expect(port.selected, ['track:2']);
      port.lines.add(['Hello', 'there']);
      port.lines.add(['', '']);
      await Future<void>.delayed(Duration.zero);
      expect(got, ['Hello\nthere', '']);
      expect(host.line.value, isNull);
    });

    test('showLine sets the drawn line; clear removes it and stops listening', () async {
      final port = _FakePort();
      final host = SsSubtitleHostController(port);
      final got = <String>[];
      await host.listenToTrack('2', got.add);
      host.showLine('Ciao');
      expect(host.line.value, 'Ciao');
      host.clear();
      expect(host.line.value, isNull);
      port.lines.add(['Later']);
      await Future<void>.delayed(Duration.zero);
      expect(got, isEmpty);
    });

    test('showFile and selectEmbedded hand over to a real subtitle', () async {
      final port = _FakePort();
      final host = SsSubtitleHostController(port);
      host.showLine('Ciao');
      await host.showFile('file:///film.it.srt', title: 'film.it.srt');
      expect(host.line.value, isNull);
      await host.selectEmbedded('3');
      expect(port.selected, ['uri:file:///film.it.srt', 'track:3']);
    });

    test('gives the position and the tracks of the player', () async {
      final port = _FakePort();
      final host = SsSubtitleHostController(port);
      final seen = <Duration>[];
      final sub = host.position.listen(seen.add);
      port.positions.add(const Duration(seconds: 3));
      await Future<void>.delayed(Duration.zero);
      expect(seen, [const Duration(seconds: 3)]);
      expect(host.embeddedTracks.single.id, '2');
      await sub.cancel();
    });
  });

  group('a subtitle file that is arriving', () {
    test('a file already there opens at once', () {
      final awaited = SsAwaitedSubtitle();
      expect(awaited.choose(5, const {4}), isTrue);
      expect(awaited.update(const {}), isNull);
    });

    test('a file chosen while arriving opens once it is there', () {
      final awaited = SsAwaitedSubtitle();
      expect(awaited.choose(4, const {4}), isFalse);
      expect(awaited.update(const {4}), isNull);
      expect(awaited.update(const {}), 4);
      expect(awaited.update(const {}), isNull);
    });

    test('another choice drops the file that was arriving', () {
      final awaited = SsAwaitedSubtitle();
      awaited.choose(4, const {4});
      awaited.clear();
      expect(awaited.update(const {}), isNull);
    });

    test('a second file chosen while the first arrives takes its place', () {
      final awaited = SsAwaitedSubtitle();
      awaited.choose(4, const {4, 6});
      expect(awaited.choose(6, const {4, 6}), isFalse);
      expect(awaited.update(const {6}), isNull);
      expect(awaited.update(const {}), 6);
    });
  });
}
