import 'package:flutter/material.dart';
import 'package:flutter_seekserve/seekserve.dart';
import 'package:provider/provider.dart';

import '../providers/seekserve_provider.dart';
import 'player_screen.dart';

class FileSelectionScreen extends StatefulWidget {
  final String torrentId;
  final String torrentName;
  final bool autoPlay;

  const FileSelectionScreen({
    super.key,
    required this.torrentId,
    required this.torrentName,
    this.autoPlay = false,
  });

  @override
  State<FileSelectionScreen> createState() => _FileSelectionScreenState();
}

class _FileSelectionScreenState extends State<FileSelectionScreen> {
  bool _autoPlayed = false;

  @override
  Widget build(BuildContext context) {
    final provider = context.watch<SeekServeProvider>();
    final files = provider.listFiles(widget.torrentId);

    // DEV: Auto-play smallest MP4 video file (ideal for testing).
    if (widget.autoPlay && !_autoPlayed && files.isNotEmpty) {
      final videoFiles = files.where((f) => f.isVideo).toList();
      if (videoFiles.isNotEmpty) {
        // Prefer smallest .mp4 for quick test
        final mp4Files =
            videoFiles.where((f) => f.name.endsWith('.mp4')).toList();
        final target = mp4Files.isNotEmpty
            ? (mp4Files..sort((a, b) => a.size.compareTo(b.size))).first
            : videoFiles.first;
        _autoPlayed = true;
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (!mounted) return;
          _playFile(context, provider, target);
        });
      }
    }

    return Scaffold(
      appBar: AppBar(title: Text(widget.torrentName)),
      body: files.isEmpty
          ? const Center(child: Text('Waiting for metadata...'))
          : ListView.builder(
              itemCount: files.length,
              itemBuilder: (context, index) {
                final file = files[index];
                return _FileTile(
                  file: file,
                  onTap: file.isVideo
                      ? () => _playFile(context, provider, file)
                      : null,
                );
              },
            ),
    );
  }

  void _playFile(
      BuildContext context, SeekServeProvider provider, FileInfo file) {
    final url = provider.selectAndStream(widget.torrentId, file.index);
    if (url == null) return;

    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => PlayerScreen(
          streamUrl: url,
          torrentId: widget.torrentId,
          fileName: file.name,
        ),
      ),
    );
  }
}

class _FileTile extends StatelessWidget {
  final FileInfo file;
  final VoidCallback? onTap;

  const _FileTile({required this.file, this.onTap});

  IconData _iconForExtension(String ext) {
    switch (ext) {
      case 'mp4':
      case 'mkv':
      case 'avi':
      case 'webm':
      case 'ogv':
      case 'mov':
        return Icons.movie;
      case 'mp3':
      case 'flac':
      case 'ogg':
      case 'wav':
        return Icons.audiotrack;
      case 'srt':
      case 'vtt':
      case 'ass':
        return Icons.subtitles;
      case 'jpg':
      case 'jpeg':
      case 'png':
      case 'gif':
        return Icons.image;
      case 'txt':
      case 'nfo':
        return Icons.description;
      default:
        return Icons.insert_drive_file;
    }
  }

  String _formatSize(int bytes) {
    if (bytes < 1024) return '$bytes B';
    if (bytes < 1024 * 1024) return '${(bytes / 1024).toStringAsFixed(1)} KB';
    if (bytes < 1024 * 1024 * 1024) {
      return '${(bytes / (1024 * 1024)).toStringAsFixed(1)} MB';
    }
    return '${(bytes / (1024 * 1024 * 1024)).toStringAsFixed(1)} GB';
  }

  @override
  Widget build(BuildContext context) {
    final enabled = onTap != null;
    return ListTile(
      leading: Icon(
        _iconForExtension(file.extension),
        color: enabled
            ? Theme.of(context).colorScheme.primary
            : Theme.of(context).disabledColor,
      ),
      title: Text(
        file.name,
        style: TextStyle(
          color: enabled ? null : Theme.of(context).disabledColor,
        ),
      ),
      subtitle: Text(_formatSize(file.size)),
      trailing: enabled ? const Icon(Icons.play_arrow) : null,
      enabled: enabled,
      onTap: onTap,
    );
  }
}
