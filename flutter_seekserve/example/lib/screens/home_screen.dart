import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../providers/seekserve_provider.dart';
import '../widgets/torrent_card.dart';
import 'file_selection_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  final _urlController = TextEditingController();

  @override
  void dispose() {
    _urlController.dispose();
    super.dispose();
  }

  void _addTorrent() {
    final url = _urlController.text.trim();
    if (url.isEmpty) return;
    context.read<SeekServeProvider>().addTorrent(url);
    _urlController.clear();
  }

  Future<void> _pasteFromClipboard() async {
    final data = await Clipboard.getData(Clipboard.kTextPlain);
    if (data?.text != null && data!.text!.isNotEmpty) {
      _urlController.text = data.text!;
    }
  }

  @override
  Widget build(BuildContext context) {
    final provider = context.watch<SeekServeProvider>();

    return Scaffold(
      appBar: AppBar(
        title: const Text('SeekServe Demo'),
        actions: [
          if (provider.serverPort != null)
            Padding(
              padding: const EdgeInsets.only(right: 16),
              child: Center(
                child: Text(
                  'Port ${provider.serverPort}',
                  style: Theme.of(context).textTheme.bodySmall,
                ),
              ),
            ),
        ],
      ),
      body: Column(
        children: [
          // Error banner
          if (provider.errorMessage != null)
            MaterialBanner(
              content: Text(provider.errorMessage!),
              backgroundColor: Theme.of(context).colorScheme.errorContainer,
              actions: [
                TextButton(
                  onPressed: () {},
                  child: const Text('DISMISS'),
                ),
              ],
            ),

          // URL input
          Padding(
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _urlController,
                    decoration: const InputDecoration(
                      hintText: 'Paste magnet link or .torrent path...',
                      border: OutlineInputBorder(),
                      isDense: true,
                    ),
                    onSubmitted: (_) => _addTorrent(),
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  onPressed: _pasteFromClipboard,
                  icon: const Icon(Icons.content_paste),
                  tooltip: 'Paste from clipboard',
                ),
                const SizedBox(width: 4),
                FilledButton(
                  onPressed: _addTorrent,
                  child: const Text('Add'),
                ),
              ],
            ),
          ),

          const Divider(height: 1),

          // Torrent list
          Expanded(
            child: provider.torrents.isEmpty
                ? const Center(
                    child: Text(
                      'No torrents added yet.\nPaste a magnet link above.',
                      textAlign: TextAlign.center,
                    ),
                  )
                : ListView.builder(
                    itemCount: provider.torrents.length,
                    itemBuilder: (context, index) {
                      final entry = provider.torrents[index];
                      return Dismissible(
                        key: ValueKey(entry.torrentId),
                        direction: DismissDirection.endToStart,
                        background: Container(
                          color: Colors.red,
                          alignment: Alignment.centerRight,
                          padding: const EdgeInsets.only(right: 24),
                          child: const Icon(Icons.delete, color: Colors.white),
                        ),
                        onDismissed: (_) {
                          provider.removeTorrent(entry.torrentId);
                        },
                        child: TorrentCard(
                          entry: entry,
                          onTap: () {
                            Navigator.push(
                              context,
                              MaterialPageRoute(
                                builder: (_) => FileSelectionScreen(
                                  torrentId: entry.torrentId,
                                  torrentName:
                                      entry.status?.name ?? entry.torrentId,
                                ),
                              ),
                            );
                          },
                        ),
                      );
                    },
                  ),
          ),
        ],
      ),
    );
  }
}
