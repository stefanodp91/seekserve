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

/// What the video waits for while the host holds it (app WORK-47, DEC-988):
/// a [message], what to expect in [detail] («the film goes on by itself
/// when…»), the [progress] from 0 to 1 (null: not measured), the time
/// [remaining] once the host can tell, and the host's ways out ([actions]).
/// Without [panel] the player shows only its waiting button, as in the first
/// moments of a wait (app DEC-1012). Not [visible]: the video stays held and
/// the player draws nothing, while the host shows its own card in its place
/// (app BUG-336). A [stopped] hold waits for nothing but the person's choice
/// (app DEC-1015): no ring, no bar, no time left.
@immutable
class SsHold {
  const SsHold({
    required this.message,
    this.detail,
    this.progress,
    this.remaining,
    this.visible = true,
    this.panel = true,
    this.stopped = false,
    this.actions = const [],
  });

  final String message;
  final String? detail;
  final double? progress;
  final Duration? remaining;
  final bool visible;
  final bool panel;
  final bool stopped;
  final List<SsHoldAction> actions;
}

/// A way out of a hold, chosen by the host: «Guarda senza sottotitoli»,
/// «Riprova». [emphasized] for the one the host suggests.
class SsHoldAction {
  const SsHoldAction({required this.label, required this.onPressed, this.emphasized = false});

  final String label;
  final VoidCallback onPressed;
  final bool emphasized;
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

  /// Whether the video is playing now.
  bool get playing;

  /// Stops the video with [hold]'s panel until the host says otherwise; a
  /// new hold only updates it. Null ends it, and the video goes on unless
  /// the person paused it ([holdPaused]). The video never goes on during a
  /// hold: with the subtitles on it never runs without its lines (app
  /// DEC-988, DEC-1012).
  void hold(SsHold? hold);

  /// Whether the person paused the video during the hold, or before it: at
  /// its end the video then stays paused.
  ValueListenable<bool> get holdPaused;

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

  /// Turns the subtitles off as «None» in the menu does, without telling it
  /// as the person's choice of the film (app DEC-1015).
  Future<void> turnOff();

  /// Ends the hold and the video goes on, even if the person paused it: a
  /// way out that says «go on» (app DEC-1017).
  void goOn();
}

/// What the host controller needs from a player.
abstract interface class SsSubtitlePort {
  Stream<Duration> get position;
  bool get playing;
  Stream<bool> get playingChanges;
  Future<void> play();
  Future<void> pause();
  Stream<List<String>> get subtitleLines;
  List<SsSubtitleTrackInfo> get tracks;
  Future<void> selectTrack(String id);
  Future<void> selectUri(String uri, {String? title});
  Future<void> selectNone();
}

/// The host of a new player (app DEC-1018 step 2b.2), told at once to
/// [onHost], so that the app can go on with a choice of its own before any
/// tap; its turn off is told to [onTurnedOff] alone, never as a choice.
SsSubtitleHostController ssNewSubtitleHost(
  SsSubtitlePort port, {
  VoidCallback? onTurnedOff,
  void Function(SsSubtitleHost host)? onHost,
}) {
  final host = SsSubtitleHostController(port, onTurnedOff: onTurnedOff);
  onHost?.call(host);
  return host;
}

/// The [SsSubtitleHost] the player gives its host, over an [SsSubtitlePort].
class SsSubtitleHostController implements SsSubtitleHost {
  SsSubtitleHostController(this._port, {this.onTurnedOff}) {
    _playing = _port.playingChanges.listen(_onPlaying);
  }

  final SsSubtitlePort _port;

  /// Told once [turnOff] turned the subtitles off.
  final VoidCallback? onTurnedOff;

  /// The line drawn over the video.
  final ValueNotifier<String?> line = ValueNotifier(null);

  /// The hold on screen, null when the video is not held.
  final ValueNotifier<SsHold?> held = ValueNotifier(null);

  @override
  final ValueNotifier<bool> holdPaused = ValueNotifier(false);

  /// The person's ⏸ or ▶ during a hold: a real pause, or back to waiting.
  /// The video stays held either way.
  void toggleHoldPause() {
    if (held.value == null) return;
    holdPaused.value = !holdPaused.value;
  }

  StreamSubscription<List<String>>? _listening;
  late final StreamSubscription<bool> _playing;

  @override
  bool get playing => _port.playing;

  @override
  void hold(SsHold? hold) {
    if (hold == null) return _release(resume: true);
    if (held.value == null) {
      holdPaused.value = !_port.playing;
      if (_port.playing) unawaited(_port.pause());
    }
    held.value = hold;
  }

  // The video started during a hold from elsewhere (the system's media
  // keys, the player itself): the hold wins, and a play is a wish to go on
  // once the lines are there. Paused after the event that told it.
  void _onPlaying(bool playing) {
    if (!playing || held.value == null) return;
    holdPaused.value = false;
    scheduleMicrotask(() {
      if (held.value != null) unawaited(_port.pause());
    });
  }

  void _release({required bool resume}) {
    if (held.value == null) return;
    held.value = null;
    final goOn = resume && !holdPaused.value;
    holdPaused.value = false;
    if (goOn) unawaited(_port.play());
  }

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

  /// The player's own track off first, so it never shows once the host's
  /// lines go; then the video goes on, even if the person paused it, as
  /// «Guarda» asks, and [onTurnedOff] tells the player.
  @override
  Future<void> turnOff() async {
    await _port.selectNone();
    line.value = null;
    await _stopListening();
    holdPaused.value = false;
    _release(resume: true);
    onTurnedOff?.call();
  }

  @override
  void goOn() {
    holdPaused.value = false;
    _release(resume: true);
  }

  /// Removes the drawn line, ends a hold and stops passing track lines.
  void clear() {
    line.value = null;
    _release(resume: true);
    unawaited(_stopListening());
  }

  Future<void> _stopListening() async {
    final listening = _listening;
    _listening = null;
    await listening?.cancel();
  }

  void dispose() {
    _release(resume: false);
    line.value = null;
    unawaited(_stopListening());
    unawaited(_playing.cancel());
    line.dispose();
    held.dispose();
    holdPaused.dispose();
  }
}

/// An audio track of the video: its player id, language and title, and
/// whether the file marks it as the default one.
class SsAudioTrackInfo {
  const SsAudioTrackInfo({required this.id, this.language, this.title, this.isDefault = false});

  final String id;
  final String? language;
  final String? title;
  final bool isDefault;
}

/// The player's audio tracks as [SsAudioTrackInfo], without `auto` and `no`
/// (app DEC-1007: the host learns the language it would hear).
List<SsAudioTrackInfo> ssAudioTrackInfos(List<AudioTrack> tracks) => [
      for (final t in tracks)
        if (t.id != 'auto' && t.id != 'no')
          SsAudioTrackInfo(id: t.id, language: t.language, title: t.title, isDefault: t.isDefault ?? false),
    ];

/// The player's tracks as [SsSubtitleTrackInfo], without `auto` and `no`.
List<SsSubtitleTrackInfo> ssSubtitleTrackInfos(List<SubtitleTrack> tracks) => [
      for (final t in tracks)
        if (t.id != 'auto' && t.id != 'no') SsSubtitleTrackInfo(id: t.id, language: t.language, title: t.title),
    ];

/// Whether the player shows its subtitles button: a track inside the video,
/// a subtitle file of the torrent, or a choice of the host (app BUG-785).
bool ssHasSubtitleChoices(
  List<SubtitleTrack> tracks, {
  required bool hasExternalFiles,
  required List<SsExtraSubtitle> extras,
}) =>
    tracks.any((t) => t.id != 'auto' && t.id != 'no') || hasExternalFiles || extras.isNotEmpty;

/// The subtitle menu: off, the host's choices, then the tracks inside the
/// video and the files of the torrent (ids `off`, `extra:<id>`,
/// `embedded:<id>`, `external:<index>`); the host's come first, or a
/// Blu-ray's forty tracks hid them (app DEC-968). A file of the torrent not on the
/// phone yet ([pending]) shows dimmed with [SsStrings.subtitlePending]: it
/// opens nothing until it arrives (app BUG-278).
List<SsTrackOption> ssSubtitleMenuOptions({
  required SsStrings strings,
  required List<SsSubtitleTrackInfo> embedded,
  required List<FileInfo> externalFiles,
  required List<SsExtraSubtitle> extras,
  Set<int> pending = const {},
}) =>
    [
      SsTrackOption(id: 'off', label: strings.subtitlesOff),
      for (final e in extras) SsTrackOption(id: 'extra:${e.id}', label: e.label, subtitle: e.note, enabled: e.enabled),
      for (final t in embedded)
        SsTrackOption(id: 'embedded:${t.id}', label: t.title ?? t.language ?? strings.track(t.id), subtitle: t.language),
      for (final f in externalFiles)
        pending.contains(f.index)
            ? SsTrackOption(id: 'external:${f.index}', label: f.name, subtitle: strings.subtitlePending, enabled: false)
            : SsTrackOption(id: 'external:${f.index}', label: f.name),
    ];

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
