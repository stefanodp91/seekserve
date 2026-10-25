import 'stream_reads.dart';

/// What the engine was asked a file for.
enum WantedFileRole {
  /// The file a player reads: it has the stream and the deadlines, and with
  /// a file behind it its pieces come first.
  reader,

  /// The file that downloads behind the reader's, or with no reader at all.
  background,
}

/// What the status says of one file the torrent downloads.
class WantedFile {
  final int index;
  final int size;

  /// The bytes of the file that are there, the blocks of unfinished pieces
  /// included.
  final int downloaded;

  /// Every piece the file has a byte in is checked.
  final bool isComplete;

  /// Null for the subtitles that ride along, and for a file no call named
  /// (every file is wanted until one is selected).
  final WantedFileRole? role;

  const WantedFile({
    required this.index,
    required this.size,
    required this.downloaded,
    required this.isComplete,
    this.role,
  });

  factory WantedFile.fromJson(Map<String, dynamic> json) {
    return WantedFile(
      index: json['index'] as int? ?? -1,
      size: json['size'] as int? ?? 0,
      downloaded: json['downloaded'] as int? ?? 0,
      isComplete: json['complete'] as bool? ?? false,
      role: _parseRole(json['role']),
    );
  }

  static WantedFileRole? _parseRole(Object? value) {
    for (final role in WantedFileRole.values) {
      if (role.name == value) return role;
    }
    return null;
  }

  Map<String, dynamic> toJson() => {
    'index': index,
    'size': size,
    'downloaded': downloaded,
    'complete': isComplete,
    if (role != null) 'role': role!.name,
  };
}

/// Status information for an active torrent.
class TorrentStatus {
  final String torrentId;
  final String name;
  final double progress;
  final double downloadRate;
  final double uploadRate;
  final int numPeers;
  final int numSeeds;
  final String state;
  final bool isPaused;
  final bool hasMetadata;
  /// The reader's file, or the one that downloads when no player reads.
  final int? selectedFile;
  final String? streamMode;
  final int? playheadPiece;
  final int? activeDeadlines;

  /// The bytes of each piece of the torrent: the reader gets its first
  /// bytes a whole piece at a time. Null before the metadata or from an
  /// older library.
  final int? pieceLength;

  /// Whether the start of a stream with priority still holds the other
  /// pieces back; null from an older library.
  final bool? startupFocus;

  /// The pieces the reader got through libtorrent, and those it found in
  /// the engine's memory of pieces; null with no reader.
  final int? pieceReads;
  final int? pieceCacheHits;

  /// What the stream's reads did (app BUG-238); null from an older library.
  final StreamReads? reads;

  /// One entry for each file the torrent downloads; null when the engine
  /// does not say (an older library, no metadata yet). [progress] counts
  /// the wanted pieces of the whole torrent: with two wanted files it is
  /// neither file's own number, and each file's is here.
  final List<WantedFile>? files;

  const TorrentStatus({
    required this.torrentId,
    required this.name,
    required this.progress,
    required this.downloadRate,
    required this.uploadRate,
    required this.numPeers,
    required this.numSeeds,
    required this.state,
    required this.isPaused,
    required this.hasMetadata,
    this.selectedFile,
    this.streamMode,
    this.playheadPiece,
    this.activeDeadlines,
    this.pieceLength,
    this.startupFocus,
    this.pieceReads,
    this.pieceCacheHits,
    this.reads,
    this.files,
  });

  factory TorrentStatus.fromJson(Map<String, dynamic> json) {
    return TorrentStatus(
      torrentId: json['torrent_id'] as String? ?? '',
      name: json['name'] as String? ?? '',
      progress: (json['progress'] as num?)?.toDouble() ?? 0.0,
      downloadRate: (json['download_rate'] as num?)?.toDouble() ?? 0.0,
      uploadRate: (json['upload_rate'] as num?)?.toDouble() ?? 0.0,
      numPeers: json['num_peers'] as int? ?? 0,
      numSeeds: json['num_seeds'] as int? ?? 0,
      state: _parseState(json['state']),
      isPaused: json['paused'] as bool? ?? false,
      hasMetadata: json['has_metadata'] as bool? ?? false,
      selectedFile: json['selected_file'] as int?,
      streamMode: _parseStreamMode(json['stream_mode']),
      playheadPiece: json['playhead_piece'] as int?,
      activeDeadlines: json['active_deadlines'] as int?,
      pieceLength: json['piece_length'] as int?,
      startupFocus: json['startup_focus'] as bool?,
      pieceReads: json['piece_reads'] as int?,
      pieceCacheHits: json['piece_cache_hits'] as int?,
      reads: StreamReads.fromJson(json),
      files: (json['files'] as List<dynamic>?)
          ?.map((file) => WantedFile.fromJson(file as Map<String, dynamic>))
          .toList(),
    );
  }

  /// Maps libtorrent state_t int values to human-readable strings.
  ///
  /// libtorrent's `state_t` enum is 1-based:
  ///   checking_files=1, downloading_metadata=2, downloading=3,
  ///   finished=4, seeding=5, (allocating=6 deprecated), checking_resume_data=7
  static String _parseState(Object? value) {
    if (value is String) return value;
    if (value is int) {
      const states = {
        1: 'checking_files',
        2: 'downloading_metadata',
        3: 'downloading',
        4: 'finished',
        5: 'seeding',
        7: 'checking_resume_data',
      };
      return states[value] ?? 'unknown';
    }
    return 'unknown';
  }

  /// Maps StreamMode int to string.
  static String? _parseStreamMode(Object? value) {
    if (value is String) return value;
    if (value is int) {
      const modes = ['streaming_first', 'download_assist', 'download_first'];
      return (value >= 0 && value < modes.length) ? modes[value] : 'mode_$value';
    }
    return null;
  }

  Map<String, dynamic> toJson() => {
    'torrent_id': torrentId,
    'name': name,
    'progress': progress,
    'download_rate': downloadRate,
    'upload_rate': uploadRate,
    'num_peers': numPeers,
    'num_seeds': numSeeds,
    'state': state,
    'paused': isPaused,
    'has_metadata': hasMetadata,
    if (selectedFile != null) 'selected_file': selectedFile,
    if (streamMode != null) 'stream_mode': streamMode,
    if (playheadPiece != null) 'playhead_piece': playheadPiece,
    if (activeDeadlines != null) 'active_deadlines': activeDeadlines,
    if (pieceLength != null) 'piece_length': pieceLength,
    if (startupFocus != null) 'startup_focus': startupFocus,
    if (pieceReads != null) 'piece_reads': pieceReads,
    if (pieceCacheHits != null) 'piece_cache_hits': pieceCacheHits,
    ...?reads?.toJson(),
    if (files != null) 'files': files!.map((file) => file.toJson()).toList(),
  };
}
