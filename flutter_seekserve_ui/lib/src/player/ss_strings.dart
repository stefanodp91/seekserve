/// The words the player shows. An app gives its own, in its language;
/// without them the player says what it always said, in English.
///
/// The ones with a detail (an HTTP status, the text of an exception, the
/// player's own error, the id of a track) are builders, so the app chooses
/// whether to say the detail: the text of an exception and the player's own
/// error can carry the address of the stream.
class SsStrings {
  const SsStrings({
    this.connecting = 'Connecting...',
    this.buffering = 'Buffering...',
    this.audio = 'Audio',
    this.subtitles = 'Subtitles',
    this.subtitlesOff = 'Off',
    this.noVideoFrames =
        'Video rendering not available on iOS Simulator.\n'
        'Audio and seek work — test on a real device.',
    this.serverErrorBuilder,
    this.cannotReachBuilder,
    this.playerErrorBuilder,
    this.trackBuilder,
  });

  /// Before the stream answers.
  final String connecting;

  /// Under the spinner, while the player waits for data.
  final String buffering;

  /// The title of the audio track selector.
  final String audio;

  /// The title of the subtitle selector.
  final String subtitles;

  /// The entry of the subtitle selector that turns them off.
  final String subtitlesOff;

  /// When the player gets no video frames.
  final String noVideoFrames;

  final String Function(int statusCode)? serverErrorBuilder;
  final String Function(Object error)? cannotReachBuilder;
  final String Function(String message)? playerErrorBuilder;
  final String Function(String id)? trackBuilder;

  /// The stream server answered with an error status.
  String serverError(int statusCode) =>
      serverErrorBuilder?.call(statusCode) ??
      'Stream server returned HTTP $statusCode';

  /// The stream server did not answer.
  String cannotReach(Object error) =>
      cannotReachBuilder?.call(error) ?? 'Cannot reach stream: $error';

  /// The player's own error, as it words it: in English, and sometimes with
  /// the address it could not open.
  String playerError(String message) =>
      playerErrorBuilder?.call(message) ?? message;

  /// The name of a track that has neither a title nor a language.
  String track(String id) => trackBuilder?.call(id) ?? 'Track $id';
}

/// True for the errors the player retries by itself: the torrent has not
/// buffered enough data yet for the format to be recognized.
bool ssIsFormatNotReady(String error) =>
    error.contains('recognize file format') ||
    error.contains('Invalid data found');
