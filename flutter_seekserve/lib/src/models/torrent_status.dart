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
  final int? selectedFile;
  final String? streamMode;
  final int? playheadPiece;
  final int? activeDeadlines;

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
  };
}
