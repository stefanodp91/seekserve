import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

/// The words of the player (app WORK-29, wave P2): an app gives its own, in
/// its language; without them the player says what it always said.
void main() {
  test('without words of its own the player says what it said before', () {
    const strings = SsStrings();
    expect(strings.connecting, 'Connecting...');
    expect(strings.buffering, 'Buffering...');
    expect(strings.audio, 'Audio');
    expect(strings.subtitles, 'Subtitles');
    expect(strings.subtitlesOff, 'Off');
    expect(strings.serverError(503), 'Stream server returned HTTP 503');
    expect(strings.cannotReach('refused'), 'Cannot reach stream: refused');
    expect(strings.track('3'), 'Track 3');
    expect(strings.noVideoFrames, contains('iOS Simulator'));
    expect(strings.playerError('Failed to open x'), 'Failed to open x');
  });

  test('the entry that turns the audio off has a name, not the player\'s '
      'inner word for it ("Track no")', () {
    expect(const SsStrings().audioOff, 'Off');
    expect(const SsStrings(audioOff: 'Nessuno').audioOff, 'Nessuno');
    // The names of the tracks in the selector.
    expect(ssAudioTrackLabel(const SsStrings(), id: 'no'), 'Off');
    expect(ssAudioTrackLabel(const SsStrings(), id: '1'), 'Track 1');
    expect(
      ssAudioTrackLabel(const SsStrings(), id: '1', language: 'ita'),
      'ita',
    );
    expect(
      ssAudioTrackLabel(
        const SsStrings(),
        id: '2',
        title: 'Commento',
        language: 'ita',
      ),
      'Commento',
    );
    // "No audio" stays "Off" whatever the file calls it.
    expect(
      ssAudioTrackLabel(const SsStrings(), id: 'no', title: 'x'),
      'Off',
    );
  });

  test('the player\'s own error, which can name the stream\'s address, is '
      'worded by the app too', () {
    final strings = SsStrings(
      playerErrorBuilder: (message) => 'Il video non si apre.',
    );
    expect(
      strings.playerError('Failed to open http://127.0.0.1:41234/stream/0'),
      'Il video non si apre.',
    );
  });

  test('an app gives its own words, also for what carries a detail: it '
      'chooses whether to say the detail', () {
    final strings = SsStrings(
      connecting: 'Mi collego…',
      buffering: 'Carico…',
      audio: 'Audio',
      subtitles: 'Sottotitoli',
      subtitlesOff: 'Nessuno',
      noVideoFrames: 'Il video non si vede su questo dispositivo.',
      serverErrorBuilder: (status) => 'Il video non è disponibile.',
      cannotReachBuilder: (error) => 'Il video non risponde.',
      trackBuilder: (id) => 'Traccia $id',
    );
    expect(strings.connecting, 'Mi collego…');
    expect(strings.buffering, 'Carico…');
    expect(strings.subtitles, 'Sottotitoli');
    expect(strings.subtitlesOff, 'Nessuno');
    expect(strings.serverError(503), 'Il video non è disponibile.');
    // No address, no port, no text of the exception.
    expect(
      strings.cannotReach('SocketException: 127.0.0.1:41234'),
      'Il video non risponde.',
    );
    expect(strings.track('3'), 'Traccia 3');
  });

  test('the errors the player tries again by itself are not errors to tell',
      () {
    expect(ssIsFormatNotReady('Failed to recognize file format.'), isTrue);
    expect(
      ssIsFormatNotReady('Invalid data found when processing input'),
      isTrue,
    );
    expect(ssIsFormatNotReady('Failed to open http://127.0.0.1/x'), isFalse);
    expect(ssIsFormatNotReady(''), isFalse);
  });
}
