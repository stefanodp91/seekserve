import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_seekserve/seekserve.dart';

/// Holds a torrent entry with its latest status.
class TorrentEntry {
  final String torrentId;
  TorrentStatus? status;
  List<FileInfo>? files;
  bool metadataReceived;

  TorrentEntry({
    required this.torrentId,
    this.status,
    this.files,
    this.metadataReceived = false,
  });
}

/// State management for the SeekServe engine.
///
/// Wraps [SeekServeClient] and exposes reactive state via [ChangeNotifier].
class SeekServeProvider extends ChangeNotifier {
  SeekServeClient? _client;
  int? _serverPort;
  String? _errorMessage;
  StreamSubscription<SeekServeEvent>? _eventSub;
  Timer? _pollTimer;

  final Map<String, TorrentEntry> _torrents = {};

  /// All active torrent entries.
  List<TorrentEntry> get torrents => _torrents.values.toList();

  /// Server port (null if not started).
  int? get serverPort => _serverPort;

  /// Last error message.
  String? get errorMessage => _errorMessage;

  /// Whether the engine is initialised.
  bool get isReady => _client != null && _serverPort != null;

  /// Initialises the engine with default config based on [savePath].
  void init(String savePath) {
    if (_client != null) return;

    try {
      _client = SeekServeClient(
        config: SeekServeConfig(
          savePath: savePath,
          streamPort: 0,
          controlPort: 0,
          logLevel: 'debug',
        ),
      );

      _serverPort = _client!.startServer();
      debugPrint('SERVER: startServer returned port=$_serverPort');

      _eventSub = _client!.events.listen(_onEvent, onError: _onEventError);

      // Poll torrent status every second.
      _pollTimer = Timer.periodic(
        const Duration(seconds: 1),
        (_) => _pollStatus(),
      );

      _errorMessage = null;
      notifyListeners();
    } catch (e) {
      _errorMessage = e.toString();
      notifyListeners();
    }
  }

  /// Adds a torrent by magnet URI or .torrent file path.
  void addTorrent(String uri) {
    if (_client == null) return;

    try {
      final id = _client!.addTorrent(uri);
      _torrents[id] = TorrentEntry(torrentId: id);
      _errorMessage = null;
      notifyListeners();
    } catch (e) {
      _errorMessage = e.toString();
      notifyListeners();
    }
  }

  /// Removes a torrent.
  void removeTorrent(String torrentId, {bool deleteFiles = false}) {
    if (_client == null) return;

    try {
      _client!.removeTorrent(torrentId, deleteFiles: deleteFiles);
      _torrents.remove(torrentId);
      _errorMessage = null;
      notifyListeners();
    } catch (e) {
      _errorMessage = e.toString();
      notifyListeners();
    }
  }

  /// Lists files for a torrent.
  List<FileInfo> listFiles(String torrentId) {
    if (_client == null) return [];

    final entry = _torrents[torrentId];
    if (entry != null && entry.files != null) return entry.files!;

    try {
      final files = _client!.listFiles(torrentId);
      if (entry != null) {
        entry.files = files;
      }
      return files;
    } catch (e) {
      _errorMessage = e.toString();
      notifyListeners();
      return [];
    }
  }

  /// Selects a file for streaming and returns its stream URL.
  String? selectAndStream(String torrentId, int fileIndex) {
    if (_client == null) return null;

    try {
      debugPrint('STREAM: selectFile($torrentId, $fileIndex)');
      _client!.selectFile(torrentId, fileIndex);
      final url = _client!.getStreamUrl(torrentId, fileIndex);
      debugPrint('STREAM: getStreamUrl → $url');
      _errorMessage = null;
      notifyListeners();
      return url;
    } catch (e) {
      _errorMessage = e.toString();
      notifyListeners();
      return null;
    }
  }

  /// Gets current status for a torrent.
  TorrentStatus? getStatus(String torrentId) {
    return _torrents[torrentId]?.status;
  }

  void _onEvent(SeekServeEvent event) {
    switch (event) {
      case MetadataReceived(:final torrentId):
        final entry = _torrents[torrentId];
        if (entry != null) {
          entry.metadataReceived = true;
          try {
            entry.files = _client!.listFiles(torrentId);
          } catch (_) {}
        }
        notifyListeners();
      case FileCompleted(:final torrentId, :final fileIndex):
        debugPrint('File completed: $torrentId file $fileIndex');
        notifyListeners();
      case TorrentError(:final torrentId, :final message):
        _errorMessage = 'Torrent $torrentId: $message';
        notifyListeners();
      case UnknownEvent():
        break;
    }
  }

  void _onEventError(Object error) {
    _errorMessage = error.toString();
    notifyListeners();
  }

  void _pollStatus() {
    if (_client == null) return;

    for (final entry in _torrents.values) {
      try {
        entry.status = _client!.getStatus(entry.torrentId);
        // Update metadataReceived from status polling (in case the event
        // callback failed or was delayed).
        if (entry.status?.hasMetadata == true && !entry.metadataReceived) {
          entry.metadataReceived = true;
          entry.files ??= _tryListFiles(entry.torrentId);
        }
      } catch (_) {
        // Ignore polling errors (torrent may have been removed).
      }
    }
    notifyListeners();
  }

  List<FileInfo>? _tryListFiles(String torrentId) {
    try {
      return _client!.listFiles(torrentId);
    } catch (_) {
      return null;
    }
  }

  @override
  void dispose() {
    _pollTimer?.cancel();
    _eventSub?.cancel();
    _client?.dispose();
    _client = null;
    super.dispose();
  }
}
