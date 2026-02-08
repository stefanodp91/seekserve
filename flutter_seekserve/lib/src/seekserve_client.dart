import 'dart:async';
import 'dart:convert';
import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'bindings_generated.dart';
import 'native_library.dart';
import 'seekserve_exception.dart';
import 'models/file_info.dart';
import 'models/torrent_status.dart';
import 'models/seekserve_config.dart';
import 'models/seekserve_event.dart';

/// High-level Dart API for the SeekServe torrent streaming engine.
///
/// Wraps the C API via dart:ffi with proper memory management,
/// JSON parsing, and async event delivery via Dart [Stream]s.
class SeekServeClient {
  final SeekServeBindings _bindings;
  late final Pointer<SeekServeEngine> _engine;
  bool _disposed = false;

  final StreamController<SeekServeEvent> _eventController =
      StreamController<SeekServeEvent>.broadcast();

  NativeCallable<ss_event_callback_tFunction>? _nativeCallback;

  /// Creates a new SeekServe engine instance.
  ///
  /// Pass [config] to customise save path, ports, auth token, etc.
  /// If null, the engine uses defaults.
  SeekServeClient({SeekServeConfig? config})
      : _bindings = nativeBindings {
    final configJson = config?.toJsonString() ?? '{}';
    final configPtr = configJson.toNativeUtf8().cast<Char>();
    try {
      _engine = _bindings.ss_engine_create(configPtr);
      if (_engine == nullptr) {
        throw const SeekServeException(-1, 'Failed to create engine');
      }
    } finally {
      calloc.free(configPtr);
    }
    _setupEventCallback();
  }

  /// Stream of events from the native engine (metadata, file completions, errors).
  Stream<SeekServeEvent> get events => _eventController.stream;

  // ---------------------------------------------------------------------------
  // Server lifecycle
  // ---------------------------------------------------------------------------

  /// Starts the HTTP streaming server.
  ///
  /// Pass [port] = 0 to let the OS pick a free port.
  /// Returns the assigned port number.
  int startServer({int port = 0}) {
    _ensureNotDisposed();
    final outPort = calloc<Uint16>();
    try {
      final err = _bindings.ss_start_server(_engine, port, outPort);
      checkError(err);
      return outPort.value;
    } finally {
      calloc.free(outPort);
    }
  }

  /// Stops the HTTP streaming server.
  void stopServer() {
    _ensureNotDisposed();
    final err = _bindings.ss_stop_server(_engine);
    checkError(err);
  }

  // ---------------------------------------------------------------------------
  // Torrent management
  // ---------------------------------------------------------------------------

  /// Adds a torrent by magnet URI or `.torrent` file path.
  ///
  /// Returns the 40-character hex torrent ID.
  String addTorrent(String uri) {
    _ensureNotDisposed();
    final uriPtr = uri.toNativeUtf8().cast<Char>();
    // Buffer for 40-char hex ID + null terminator
    final outId = calloc<Char>(64);
    try {
      final err = _bindings.ss_add_torrent(_engine, uriPtr, outId, 64);
      checkError(err);
      return outId.cast<Utf8>().toDartString();
    } finally {
      calloc.free(uriPtr);
      calloc.free(outId);
    }
  }

  /// Removes a torrent. If [deleteFiles] is true, also deletes downloaded data.
  void removeTorrent(String torrentId, {bool deleteFiles = false}) {
    _ensureNotDisposed();
    final idPtr = torrentId.toNativeUtf8().cast<Char>();
    try {
      final err = _bindings.ss_remove_torrent(_engine, idPtr, deleteFiles);
      checkError(err);
    } finally {
      calloc.free(idPtr);
    }
  }

  // ---------------------------------------------------------------------------
  // File operations
  // ---------------------------------------------------------------------------

  /// Lists all files in the torrent. Requires metadata to be available.
  List<FileInfo> listFiles(String torrentId) {
    _ensureNotDisposed();
    final idPtr = torrentId.toNativeUtf8().cast<Char>();
    final outJson = calloc<Pointer<Char>>();
    try {
      final err = _bindings.ss_list_files(_engine, idPtr, outJson);
      checkError(err);
      final jsonStr = outJson.value.cast<Utf8>().toDartString();
      final list = jsonDecode(jsonStr) as List<dynamic>;
      return list
          .map((e) => FileInfo.fromJson(e as Map<String, dynamic>))
          .toList();
    } finally {
      if (outJson.value != nullptr) {
        _bindings.ss_free_string(outJson.value);
      }
      calloc.free(outJson);
      calloc.free(idPtr);
    }
  }

  /// Selects a file for streaming. Sets priority for that file and deprioritises others.
  void selectFile(String torrentId, int fileIndex) {
    _ensureNotDisposed();
    final idPtr = torrentId.toNativeUtf8().cast<Char>();
    try {
      final err = _bindings.ss_select_file(_engine, idPtr, fileIndex);
      checkError(err);
    } finally {
      calloc.free(idPtr);
    }
  }

  /// Returns the HTTP stream URL for a specific file in a torrent.
  ///
  /// The URL points to the local HTTP Range server and includes the auth token.
  String getStreamUrl(String torrentId, int fileIndex) {
    _ensureNotDisposed();
    final idPtr = torrentId.toNativeUtf8().cast<Char>();
    final outUrl = calloc<Pointer<Char>>();
    try {
      final err =
          _bindings.ss_get_stream_url(_engine, idPtr, fileIndex, outUrl);
      checkError(err);
      return outUrl.value.cast<Utf8>().toDartString();
    } finally {
      if (outUrl.value != nullptr) {
        _bindings.ss_free_string(outUrl.value);
      }
      calloc.free(outUrl);
      calloc.free(idPtr);
    }
  }

  // ---------------------------------------------------------------------------
  // Status
  // ---------------------------------------------------------------------------

  /// Gets the current status of a torrent (progress, rates, peers, etc.).
  TorrentStatus getStatus(String torrentId) {
    _ensureNotDisposed();
    final idPtr = torrentId.toNativeUtf8().cast<Char>();
    final outJson = calloc<Pointer<Char>>();
    try {
      final err = _bindings.ss_get_status(_engine, idPtr, outJson);
      checkError(err);
      final jsonStr = outJson.value.cast<Utf8>().toDartString();
      final map = jsonDecode(jsonStr) as Map<String, dynamic>;
      return TorrentStatus.fromJson(map);
    } finally {
      if (outJson.value != nullptr) {
        _bindings.ss_free_string(outJson.value);
      }
      calloc.free(outJson);
      calloc.free(idPtr);
    }
  }

  // ---------------------------------------------------------------------------
  // Lifecycle
  // ---------------------------------------------------------------------------

  /// Releases all native resources. Must be called when done.
  void dispose() {
    if (_disposed) return;
    _disposed = true;

    _nativeCallback?.close();
    _nativeCallback = null;
    _eventController.close();
    _bindings.ss_engine_destroy(_engine);
  }

  // ---------------------------------------------------------------------------
  // Internals
  // ---------------------------------------------------------------------------

  void _setupEventCallback() {
    // NativeCallable.listener dispatches calls from native threads to
    // the Dart event loop. The closure captures `this` so we can add
    // events to the stream controller.
    _nativeCallback = NativeCallable<ss_event_callback_tFunction>.listener(
      _handleNativeEvent,
    );

    final err = _bindings.ss_set_event_callback(
      _engine,
      _nativeCallback!.nativeFunction,
      nullptr,
    );
    checkError(err);
  }

  void _handleNativeEvent(Pointer<Char> eventJsonPtr, Pointer<Void> userData) {
    if (_disposed) return;
    try {
      final jsonStr = eventJsonPtr.cast<Utf8>().toDartString();
      final map = jsonDecode(jsonStr) as Map<String, dynamic>;
      final event = SeekServeEvent.fromJson(map);
      _eventController.add(event);
    } catch (e) {
      _eventController.addError(e);
    }
  }

  void _ensureNotDisposed() {
    if (_disposed) {
      throw StateError('SeekServeClient has been disposed');
    }
  }
}
