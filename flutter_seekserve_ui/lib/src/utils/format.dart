/// Formats a byte count into a human-readable string (B, KB, MB, GB).
String formatBytes(int bytes) {
  if (bytes < 1024) return '$bytes B';
  if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
  if (bytes < 1024 * 1024 * 1024) {
    return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
  }
  return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(2)} GB';
}

/// Formats a transfer rate (bytes/sec) into a human-readable string.
String formatRate(double bytesPerSec) {
  if (bytesPerSec < 1024) return '${bytesPerSec.toStringAsFixed(0)} B/s';
  if (bytesPerSec < 1024 * 1024) {
    return '${(bytesPerSec / 1024).toStringAsFixed(1)} KB/s';
  }
  return '${(bytesPerSec / (1024 * 1024)).toStringAsFixed(1)} MB/s';
}

/// Formats a [Duration] as mm:ss or h:mm:ss.
String formatDuration(Duration d) {
  final h = d.inHours;
  final m = d.inMinutes.remainder(60).toString().padLeft(2, '0');
  final s = d.inSeconds.remainder(60).toString().padLeft(2, '0');
  return h > 0 ? '$h:$m:$s' : '$m:$s';
}

/// Returns an icon-compatible file type category.
enum FileCategory { video, audio, subtitle, image, document, other }

/// Determines the [FileCategory] from a file extension.
FileCategory fileCategoryFromExtension(String ext) {
  switch (ext.toLowerCase()) {
    case 'mp4':
    case 'mkv':
    case 'avi':
    case 'webm':
    case 'ogv':
    case 'mov':
    case 'wmv':
    case 'flv':
      return FileCategory.video;
    case 'mp3':
    case 'flac':
    case 'ogg':
    case 'wav':
    case 'aac':
      return FileCategory.audio;
    case 'srt':
    case 'vtt':
    case 'ass':
    case 'ssa':
      return FileCategory.subtitle;
    case 'jpg':
    case 'jpeg':
    case 'png':
    case 'gif':
    case 'bmp':
    case 'webp':
      return FileCategory.image;
    case 'txt':
    case 'nfo':
    case 'pdf':
    case 'doc':
      return FileCategory.document;
    default:
      return FileCategory.other;
  }
}
