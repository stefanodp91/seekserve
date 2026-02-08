import 'dart:io';

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
  Player? _player;
  VideoController? _videoController;
  String _probeStatus = 'Probing server...';
  String _playerError = '';
  String _activeUrl = '';

  /// Fallback public video URL to verify media_kit works independently.
  static const _kFallbackVideoUrl =
      'https://archive.org/download/Sintel/sintel-2048-stereo_512kb.mp4';

  @override
  void initState() {
    super.initState();
    debugPrint('PlayerScreen: will probe URL: ${widget.streamUrl}');
    _probeAndPlay();
  }

  Future<void> _probeAndPlay() async {
    try {
      final uri = Uri.parse(widget.streamUrl);
      debugPrint('PlayerScreen: probing ${uri.host}:${uri.port}${uri.path}');

      final client = HttpClient();
      client.connectionTimeout = const Duration(seconds: 5);
      final request = await client.openUrl('HEAD', uri);
      final response = await request.close();

      final probeResult =
          'HTTP ${response.statusCode} ${response.reasonPhrase}';
      debugPrint('PlayerScreen: probe result: $probeResult');
      debugPrint(
          'PlayerScreen: Content-Length=${response.headers.contentLength}, '
          'Content-Type=${response.headers.contentType}');

      client.close();

      if (!mounted) return;
      setState(() => _probeStatus = probeResult);

      // Server is reachable — start the player with the stream URL.
      _startPlayer(widget.streamUrl);
    } catch (e) {
      debugPrint('PlayerScreen: probe FAILED: $e');
      if (!mounted) return;
      setState(() => _probeStatus = 'UNREACHABLE: $e');
    }
  }

  void _startPlayer(String url) {
    // Dispose previous player if any.
    _player?.dispose();

    debugPrint('PlayerScreen: starting media_kit player with: $url');
    _activeUrl = url;
    _playerError = '';
    _player = Player();
    _videoController = VideoController(_player!);
    _player!.stream.error.listen((error) {
      debugPrint('PlayerScreen: player error: $error');
      if (mounted) setState(() => _playerError = error);
    });
    _player!.stream.log.listen((log) {
      debugPrint('PlayerScreen: mpv log: ${log.text}');
    });
    _player!.stream.playing.listen((playing) {
      debugPrint('PlayerScreen: playing=$playing');
    });
    _player!.open(Media(url));
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    _player?.dispose();
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
          // Video player (or placeholder)
          Expanded(
            child: _videoController != null
                ? Video(controller: _videoController!)
                : const Center(child: CircularProgressIndicator()),
          ),

          // Debug info panel
          Container(
            color: Colors.black87,
            width: double.infinity,
            padding: const EdgeInsets.all(8),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Probe: $_probeStatus',
                    style: TextStyle(
                      fontSize: 11,
                      color: _probeStatus.startsWith('HTTP 2')
                          ? Colors.greenAccent
                          : _probeStatus.startsWith('UNREACHABLE')
                              ? Colors.redAccent
                              : Colors.yellowAccent,
                    )),
                if (_playerError.isNotEmpty)
                  Text('Player: $_playerError',
                      style:
                          const TextStyle(fontSize: 11, color: Colors.red)),
                SelectableText(
                  _activeUrl.isEmpty ? widget.streamUrl : _activeUrl,
                  style: const TextStyle(fontSize: 9, color: Colors.white54),
                  maxLines: 2,
                ),
                const SizedBox(height: 4),
                // Debug buttons
                Row(
                  children: [
                    _DebugButton(
                      label: 'Test Public URL',
                      onPressed: () => _startPlayer(_kFallbackVideoUrl),
                    ),
                    const SizedBox(width: 8),
                    _DebugButton(
                      label: 'Retry Stream',
                      onPressed: () => _startPlayer(widget.streamUrl),
                    ),
                  ],
                ),
              ],
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

class _DebugButton extends StatelessWidget {
  final String label;
  final VoidCallback onPressed;

  const _DebugButton({required this.label, required this.onPressed});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 28,
      child: OutlinedButton(
        onPressed: onPressed,
        style: OutlinedButton.styleFrom(
          padding: const EdgeInsets.symmetric(horizontal: 8),
          textStyle: const TextStyle(fontSize: 10),
          side: BorderSide(color: Colors.white38),
        ),
        child: Text(label),
      ),
    );
  }
}
