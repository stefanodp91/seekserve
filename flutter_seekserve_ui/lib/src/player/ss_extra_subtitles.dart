import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';
import 'package:media_kit/media_kit.dart';

import 'ss_strings.dart';
import 'ss_track_selector.dart';

/// A subtitle choice the host adds to the player's menu. [label] and [note]
/// come already in the user's language; a disabled choice shows but does not
/// respond.
class SsExtraSubtitle {
  const SsExtraSubtitle({required this.id, required this.label, this.note, this.enabled = true});

  final String id;
  final String label;
  final String? note;
  final bool enabled;
}

/// A subtitle track the player knows: its player id, language and title.
class SsSubtitleTrackInfo {
  const SsSubtitleTrackInfo({required this.id, this.language, this.title});

  final String id;
  final String? language;
  final String? title;
}

/// What a host can do once one of its subtitle choices is selected.
abstract interface class SsSubtitleHost {
  /// The playback position.
  Stream<Duration> get position;

  /// The subtitle tracks inside the video.
  List<SsSubtitleTrackInfo> get embeddedTracks;

  /// Draws [text] over the video (`<i>…</i>` in italics, `\n` between rows);
  /// null removes it.
  void showLine(String? text);

  /// Turns on track [trackId] without drawing it and passes each of its lines
  /// (empty when none is on screen) to [onLine].
  Future<void> listenToTrack(String trackId, void Function(String line) onLine);

  /// Hands over to a real subtitle file.
  Future<void> showFile(String uri, {String? title});

  /// Hands over to a real subtitle track inside the video.
  Future<void> selectEmbedded(String trackId);
}

/// What the host controller needs from a player.
abstract interface class SsSubtitlePort {
  Stream<Duration> get position;
  Stream<List<String>> get subtitleLines;
  List<SsSubtitleTrackInfo> get tracks;
  Future<void> selectTrack(String id);
  Future<void> selectUri(String uri, {String? title});
  Future<void> selectNone();
}

/// The [SsSubtitleHost] the player gives its host, over an [SsSubtitlePort].
class SsSubtitleHostController implements SsSubtitleHost {
  SsSubtitleHostController(this._port);

  final SsSubtitlePort _port;

  /// The line drawn over the video.
  final ValueNotifier<String?> line = ValueNotifier(null);
  StreamSubscription<List<String>>? _listening;

  @override
  Stream<Duration> get position => _port.position;

  @override
  List<SsSubtitleTrackInfo> get embeddedTracks => _port.tracks;

  @override
  void showLine(String? text) => line.value = text;

  @override
  Future<void> listenToTrack(String trackId, void Function(String line) onLine) async {
    await _stopListening();
    await _port.selectTrack(trackId);
    _listening = _port.subtitleLines.listen((lines) => onLine(lines.where((l) => l.isNotEmpty).join('\n')));
  }

  @override
  Future<void> showFile(String uri, {String? title}) async {
    clear();
    await _port.selectUri(uri, title: title);
  }

  @override
  Future<void> selectEmbedded(String trackId) async {
    clear();
    await _port.selectTrack(trackId);
  }

  /// Removes the drawn line and stops passing track lines.
  void clear() {
    line.value = null;
    unawaited(_stopListening());
  }

  Future<void> _stopListening() async {
    final listening = _listening;
    _listening = null;
    await listening?.cancel();
  }

  void dispose() {
    clear();
    line.dispose();
  }
}

/// The player's tracks as [SsSubtitleTrackInfo], without `auto` and `no`.
List<SsSubtitleTrackInfo> ssSubtitleTrackInfos(List<SubtitleTrack> tracks) => [
      for (final t in tracks)
        if (t.id != 'auto' && t.id != 'no') SsSubtitleTrackInfo(id: t.id, language: t.language, title: t.title),
    ];

/// The subtitle menu: off, the tracks inside the video, the files of the
/// torrent, then the host's choices (ids `off`, `embedded:<id>`,
/// `external:<index>`, `extra:<id>`).
List<SsTrackOption> ssSubtitleMenuOptions({
  required SsStrings strings,
  required List<SsSubtitleTrackInfo> embedded,
  required List<FileInfo> externalFiles,
  required List<SsExtraSubtitle> extras,
  Set<int> pending = const {},
}) =>
    [
      SsTrackOption(id: 'off', label: strings.subtitlesOff),
      for (final t in embedded)
        SsTrackOption(id: 'embedded:${t.id}', label: t.title ?? t.language ?? strings.track(t.id), subtitle: t.language),
      for (final f in externalFiles)
        SsTrackOption(
          id: 'external:${f.index}',
          label: f.name,
          subtitle: pending.contains(f.index) ? strings.subtitleArriving : null,
        ),
      for (final e in extras) SsTrackOption(id: 'extra:${e.id}', label: e.label, subtitle: e.note, enabled: e.enabled),
    ];

/// The subtitle file of the torrent chosen before it was on the device: it
/// opens when it gets there, unless another choice came in between.
class SsAwaitedSubtitle {
  int? _fileIndex;

  /// Whether file [fileIndex] can open now; otherwise it is awaited, in place
  /// of any file awaited before. [pending] are the files not there yet.
  bool choose(int fileIndex, Set<int> pending) {
    if (!pending.contains(fileIndex)) {
      _fileIndex = null;
      return true;
    }
    _fileIndex = fileIndex;
    return false;
  }

  /// The awaited file, once, when it is no longer in [pending].
  int? update(Set<int> pending) {
    final awaited = _fileIndex;
    if (awaited == null || pending.contains(awaited)) return null;
    _fileIndex = null;
    return awaited;
  }

  /// Another choice, or another video: nothing is awaited any more.
  void clear() => _fileIndex = null;
}

/// The host's line over the video, styled like the player's own subtitles.
class SsSubtitleLine extends StatelessWidget {
  const SsSubtitleLine({super.key, required this.text});

  final ValueListenable<String?> text;

  static const _style = TextStyle(
    height: 1.4,
    fontSize: 24,
    color: Color(0xFFFFFFFF),
    backgroundColor: Color(0xAA000000),
  );

  @override
  Widget build(BuildContext context) => ValueListenableBuilder<String?>(
        valueListenable: text,
        builder: (context, value, _) {
          if (value == null || value.trim().isEmpty) return const SizedBox.shrink();
          return Align(
            alignment: Alignment.bottomCenter,
            child: Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 24),
              child: RichText(textAlign: TextAlign.center, text: TextSpan(style: _style, children: _spans(value))),
            ),
          );
        },
      );

  /// `<i>…</i>` as italic spans; any other tag is dropped.
  static List<TextSpan> _spans(String text) {
    final spans = <TextSpan>[];
    var italic = false;
    var at = 0;
    for (final m in RegExp(r'<(/?)i>|<[^>]*>', caseSensitive: false).allMatches(text)) {
      if (m.start > at) {
        spans.add(TextSpan(text: text.substring(at, m.start), style: italic ? const TextStyle(fontStyle: FontStyle.italic) : null));
      }
      if (m.group(0)!.toLowerCase() == '<i>') italic = true;
      if (m.group(0)!.toLowerCase() == '</i>') italic = false;
      at = m.end;
    }
    if (at < text.length) {
      spans.add(TextSpan(text: text.substring(at), style: italic ? const TextStyle(fontStyle: FontStyle.italic) : null));
    }
    return spans;
  }
}
