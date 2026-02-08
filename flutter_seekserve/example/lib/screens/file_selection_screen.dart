import 'package:flutter/material.dart';
import 'package:flutter_seekserve/seekserve.dart';
import 'package:provider/provider.dart';

import '../providers/seekserve_provider.dart';
import 'player_screen.dart';

class FileSelectionScreen extends StatelessWidget {
  final String torrentId;
  final String torrentName;

  const FileSelectionScreen({
    super.key,
    required this.torrentId,
    required this.torrentName,
  });

  @override
  Widget build(BuildContext context) {
    final provider = context.watch<SeekServeProvider>();
    final files = provider.listFiles(torrentId);

    return Scaffold(
      appBar: AppBar(title: Text(torrentName)),
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
    final url = provider.selectAndStream(torrentId, file.index);
    if (url == null) return;

    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => PlayerScreen(
          streamUrl: url,
          torrentId: torrentId,
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
