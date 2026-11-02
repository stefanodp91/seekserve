import 'package:media_kit/media_kit.dart';

import 'ss_extra_subtitles.dart';

/// A subtitle chosen in the player's menu: told to the host
/// ([SsVideoPlayer.onSubtitleChosen]) and put back when the player opens
/// again ([SsVideoPlayer.initialSubtitle], app DEC-818).
sealed class SsSubtitleChoice {
  const SsSubtitleChoice();
}

/// No subtitle.
class SsSubtitleNone extends SsSubtitleChoice {
  const SsSubtitleNone();

  @override
  bool operator ==(Object other) => other is SsSubtitleNone;

  @override
  int get hashCode => (SsSubtitleNone).hashCode;
}

/// A subtitle track inside the video, by its player id.
class SsSubtitleEmbedded extends SsSubtitleChoice {
  const SsSubtitleEmbedded({required this.id, this.language});

  final String id;
  final String? language;

  @override
  bool operator ==(Object other) => other is SsSubtitleEmbedded && other.id == id && other.language == language;

  @override
  int get hashCode => Object.hash(id, language);
}

/// A subtitle file of the torrent, by its file index.
class SsSubtitleFile extends SsSubtitleChoice {
  const SsSubtitleFile({required this.fileIndex});

  final int fileIndex;

  @override
  bool operator ==(Object other) => other is SsSubtitleFile && other.fileIndex == fileIndex;

  @override
  int get hashCode => fileIndex.hashCode;
}

/// One of the host's choices ([SsExtraSubtitle.id]).
class SsSubtitleExtra extends SsSubtitleChoice {
  const SsSubtitleExtra({required this.id});

  final String id;

  @override
  bool operator ==(Object other) => other is SsSubtitleExtra && other.id == id;

  @override
  int get hashCode => id.hashCode;
}

/// The choice to put back in a player with [tracks] and the subtitle files
/// [fileIndexes]; null when its track or file is not there any more, and the
/// file's default stays.
SsSubtitleChoice? ssSubtitleToRestore(
  SsSubtitleChoice choice,
  List<SsSubtitleTrackInfo> tracks,
  List<int> fileIndexes,
) =>
    switch (choice) {
      SsSubtitleEmbedded(:final id) => tracks.any((t) => t.id == id) ? choice : null,
      SsSubtitleFile(:final fileIndex) => fileIndexes.contains(fileIndex) ? choice : null,
      SsSubtitleNone() || SsSubtitleExtra() => choice,
    };

/// The choice behind a menu id of [ssSubtitleMenuOptions], an embedded one
/// with the language of its track in [tracks]; null for an id the menu
/// does not make.
SsSubtitleChoice? ssSubtitleChoiceOfMenuId(String id, List<SsSubtitleTrackInfo> tracks) {
  final colon = id.indexOf(':');
  final value = colon < 0 ? '' : id.substring(colon + 1);
  switch (colon < 0 ? id : id.substring(0, colon)) {
    case 'off':
      return const SsSubtitleNone();
    case 'embedded':
      final track = tracks.where((t) => t.id == value).firstOrNull;
      return track == null ? null : SsSubtitleEmbedded(id: track.id, language: track.language);
    case 'external':
      final index = int.tryParse(value);
      return index == null ? null : SsSubtitleFile(fileIndex: index);
    case 'extra':
      return SsSubtitleExtra(id: value);
  }
  return null;
}

/// Whether the player has loaded the file: a real video or audio track, so
/// its subtitle tracks are the file's.
bool ssTracksLoaded(Tracks tracks) =>
    tracks.video.any((t) => t.id != 'auto' && t.id != 'no') ||
    tracks.audio.any((t) => t.id != 'auto' && t.id != 'no');
