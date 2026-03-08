/// Represents a file inside a torrent.
class FileInfo {
  final int index;
  final String path;
  final int size;

  const FileInfo({
    required this.index,
    required this.path,
    required this.size,
  });

  /// File name extracted from the full path.
  String get name {
    final idx = path.lastIndexOf('/');
    return idx >= 0 ? path.substring(idx + 1) : path;
  }

  /// File extension (lowercase, without dot).
  String get extension {
    final n = name;
    final idx = n.lastIndexOf('.');
    return idx >= 0 ? n.substring(idx + 1).toLowerCase() : '';
  }

  /// Whether this file is a video format.
  bool get isVideo {
    const videoExts = {'mp4', 'mkv', 'avi', 'webm', 'ogv', 'mov', 'wmv', 'flv'};
    return videoExts.contains(extension);
  }

  /// Whether this file is a subtitle format.
  bool get isSubtitle {
    const subtitleExts = {'srt', 'vtt', 'ass', 'ssa'};
    return subtitleExts.contains(extension);
  }

  /// Whether this file is an audio format.
  bool get isAudio {
    const audioExts = {'mp3', 'flac', 'ogg', 'wav', 'aac'};
    return audioExts.contains(extension);
  }

  factory FileInfo.fromJson(Map<String, dynamic> json) {
    return FileInfo(
      index: json['index'] as int,
      path: json['path'] as String,
      size: json['size'] as int,
    );
  }

  Map<String, dynamic> toJson() => {
    'index': index,
    'path': path,
    'size': size,
  };

  @override
  String toString() => 'FileInfo($index: $name, ${_formatSize(size)})';

  static String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    if (bytes < 1024 * 1024 * 1024) {
      return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
  }
}
