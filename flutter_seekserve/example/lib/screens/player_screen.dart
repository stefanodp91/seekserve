import 'package:flutter/material.dart';
import 'package:media_kit/media_kit.dart';
import 'package:media_kit_video/media_kit_video.dart';
import 'package:provider/provider.dart';

import '../providers/seekserve_provider.dart';
import '../widgets/status_bar.dart';

class PlayerScreen extends StatefulWidget {
  final String streamUrl;
  final String torrentId;
  final String fileName;

  const PlayerScreen({
    super.key,
    required this.streamUrl,
    required this.torrentId,
    required this.fileName,
  });

  @override
  State<PlayerScreen> createState() => _PlayerScreenState();
}

class _PlayerScreenState extends State<PlayerScreen> {
  late final Player _player;
  late final VideoController _videoController;

  @override
  void initState() {
    super.initState();
    _player = Player();
    _videoController = VideoController(_player);
    _player.open(Media(widget.streamUrl));
  }

  @override
  void dispose() {
    _player.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final provider = context.watch<SeekServeProvider>();
    final status = provider.getStatus(widget.torrentId);

    return Scaffold(
      appBar: AppBar(title: Text(widget.fileName)),
      body: Column(
        children: [
          // Video player
          Expanded(
            child: Video(
              controller: _videoController,
            ),
          ),

          // Torrent status bar
          if (status != null)
            Padding(
              padding: const EdgeInsets.all(12),
              child: StatusBar(status: status),
            ),
        ],
      ),
    );
  }
}
