import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:media_kit/media_kit.dart';

/// The subtitle chosen in the player's menu, told to the host and put back
/// when the player opens again (app DEC-818).
void main() {
  const tracks = [
    SsSubtitleTrackInfo(id: '1', language: 'eng', title: 'English'),
    SsSubtitleTrackInfo(id: '2', language: 'fre'),
  ];
  const files = [4, 7];

  group('the choice to put back', () {
    test('an embedded choice whose track is there selects it', () {
      expect(
        ssSubtitleToRestore(const SsSubtitleEmbedded(id: '2', language: 'fre'), tracks, files),
        const SsSubtitleEmbedded(id: '2', language: 'fre'),
      );
    });

    test('an embedded choice whose track is gone keeps the default', () {
      expect(ssSubtitleToRestore(const SsSubtitleEmbedded(id: '3'), tracks, files), isNull);
    });

    test('a file choice selects the file when it is among the files', () {
      expect(ssSubtitleToRestore(const SsSubtitleFile(fileIndex: 7), tracks, files), const SsSubtitleFile(fileIndex: 7));
      expect(ssSubtitleToRestore(const SsSubtitleFile(fileIndex: 5), tracks, files), isNull);
    });

    test('a file choice not on the phone yet keeps the default, as in the menu (app BUG-278)', () {
      expect(ssSubtitleToRestore(const SsSubtitleFile(fileIndex: 7), tracks, files, pending: {7}), isNull);
      expect(
        ssSubtitleToRestore(const SsSubtitleFile(fileIndex: 7), tracks, files, pending: {4}),
        const SsSubtitleFile(fileIndex: 7),
      );
    });

    test('none selects none', () {
      expect(ssSubtitleToRestore(const SsSubtitleNone(), const [], const []), const SsSubtitleNone());
    });

    test('an extra choice selects the extra', () {
      expect(
        ssSubtitleToRestore(const SsSubtitleExtra(id: 'translated'), const [], const []),
        const SsSubtitleExtra(id: 'translated'),
      );
    });
  });

  group('the choice made in the menu', () {
    test('each menu id gives its choice, an embedded one with its language', () {
      expect(ssSubtitleChoiceOfMenuId('off', tracks), const SsSubtitleNone());
      expect(ssSubtitleChoiceOfMenuId('embedded:1', tracks), const SsSubtitleEmbedded(id: '1', language: 'eng'));
      expect(ssSubtitleChoiceOfMenuId('external:4', tracks), const SsSubtitleFile(fileIndex: 4));
      expect(ssSubtitleChoiceOfMenuId('extra:translated', tracks), const SsSubtitleExtra(id: 'translated'));
    });

    test('an id the menu does not make gives no choice', () {
      expect(ssSubtitleChoiceOfMenuId('embedded:9', tracks), isNull);
      expect(ssSubtitleChoiceOfMenuId('external:x', tracks), isNull);
      expect(ssSubtitleChoiceOfMenuId('other', tracks), isNull);
    });
  });

  test('choices are equal by value', () {
    expect(const SsSubtitleEmbedded(id: '1', language: 'eng'), isNot(const SsSubtitleEmbedded(id: '1')));
    expect(const SsSubtitleFile(fileIndex: 1).hashCode, const SsSubtitleFile(fileIndex: 1).hashCode);
    expect(const SsSubtitleExtra(id: 'a'), isNot(const SsSubtitleExtra(id: 'b')));
  });

  test('the tracks are known once the file has a real video or audio track', () {
    expect(ssTracksLoaded(const Tracks()), isFalse);
    expect(ssTracksLoaded(Tracks(video: [VideoTrack.auto(), VideoTrack.no(), const VideoTrack('1', null, null)])), isTrue);
    expect(ssTracksLoaded(Tracks(audio: [AudioTrack.auto(), AudioTrack.no(), const AudioTrack('1', null, null)])), isTrue);
  });
}
