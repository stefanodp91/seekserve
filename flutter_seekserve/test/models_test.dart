import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_seekserve/seekserve.dart';

void main() {
  group('FileInfo', () {
    test('fromJson parses correctly', () {
      final file = FileInfo.fromJson({
        'index': 3,
        'path': 'Sintel/Sintel.2010.720p.mkv',
        'size': 282322000,
      });
      expect(file.index, 3);
      expect(file.path, 'Sintel/Sintel.2010.720p.mkv');
      expect(file.size, 282322000);
    });

    test('name extracts filename from path', () {
      final file = FileInfo(index: 0, path: 'Sintel/Sintel.mp4', size: 100);
      expect(file.name, 'Sintel.mp4');
    });

    test('name returns path if no slash', () {
      final file = FileInfo(index: 0, path: 'video.mp4', size: 100);
      expect(file.name, 'video.mp4');
    });

    test('extension returns lowercase extension', () {
      final file = FileInfo(index: 0, path: 'Video.MP4', size: 100);
      expect(file.extension, 'mp4');
    });

    test('isVideo detects video extensions', () {
      expect(FileInfo(index: 0, path: 'a.mp4', size: 1).isVideo, true);
      expect(FileInfo(index: 0, path: 'a.mkv', size: 1).isVideo, true);
      expect(FileInfo(index: 0, path: 'a.avi', size: 1).isVideo, true);
      expect(FileInfo(index: 0, path: 'a.srt', size: 1).isVideo, false);
      expect(FileInfo(index: 0, path: 'a.txt', size: 1).isVideo, false);
    });

    test('toJson roundtrips', () {
      final file = FileInfo(index: 5, path: 'dir/file.mkv', size: 999);
      final json = file.toJson();
      final restored = FileInfo.fromJson(json);
      expect(restored.index, file.index);
      expect(restored.path, file.path);
      expect(restored.size, file.size);
    });
  });

  group('TorrentStatus', () {
    test('fromJson parses all fields', () {
      final status = TorrentStatus.fromJson({
        'torrent_id': 'abc123',
        'name': 'Sintel',
        'progress': 0.75,
        'download_rate': 1024000.0,
        'upload_rate': 512000.0,
        'num_peers': 12,
        'num_seeds': 5,
        'state': 'downloading',
        'has_metadata': true,
        'selected_file': 8,
        'stream_mode': 'streaming_first',
        'playhead_piece': 42,
        'active_deadlines': 10,
        'piece_length': 4194304,
        'startup_focus': true,
        'piece_reads': 7,
        'piece_cache_hits': 120,
      });

      expect(status.torrentId, 'abc123');
      expect(status.name, 'Sintel');
      expect(status.progress, 0.75);
      expect(status.downloadRate, 1024000.0);
      expect(status.uploadRate, 512000.0);
      expect(status.numPeers, 12);
      expect(status.numSeeds, 5);
      expect(status.state, 'downloading');
      expect(status.hasMetadata, true);
      expect(status.selectedFile, 8);
      expect(status.streamMode, 'streaming_first');
      expect(status.playheadPiece, 42);
      expect(status.activeDeadlines, 10);
      expect(status.pieceLength, 4194304);
      expect(status.startupFocus, true);
      expect(status.pieceReads, 7);
      expect(status.pieceCacheHits, 120);
    });

    test('fromJson maps integer state_t values (1-based libtorrent enum)', () {
      // libtorrent state_t: checking_files=1, downloading_metadata=2,
      // downloading=3, finished=4, seeding=5, checking_resume_data=7
      expect(
        TorrentStatus.fromJson({'torrent_id': '', 'state': 1}).state,
        'checking_files',
      );
      expect(
        TorrentStatus.fromJson({'torrent_id': '', 'state': 2}).state,
        'downloading_metadata',
      );
      expect(
        TorrentStatus.fromJson({'torrent_id': '', 'state': 3}).state,
        'downloading',
      );
      expect(
        TorrentStatus.fromJson({'torrent_id': '', 'state': 4}).state,
        'finished',
      );
      expect(
        TorrentStatus.fromJson({'torrent_id': '', 'state': 5}).state,
        'seeding',
      );
      expect(
        TorrentStatus.fromJson({'torrent_id': '', 'state': 7}).state,
        'checking_resume_data',
      );
      expect(
        TorrentStatus.fromJson({'torrent_id': '', 'state': 99}).state,
        'unknown',
      );
    });

    test('fromJson handles missing optional fields', () {
      final status = TorrentStatus.fromJson({
        'torrent_id': 'def456',
        'name': 'Test',
        'progress': 0.0,
        'num_peers': 0,
        'state': 'checking',
      });

      expect(status.torrentId, 'def456');
      expect(status.selectedFile, isNull);
      expect(status.streamMode, isNull);
      expect(status.playheadPiece, isNull);
      expect(status.pieceLength, isNull);
      expect(status.startupFocus, isNull);
      expect(status.pieceReads, isNull);
      expect(status.pieceCacheHits, isNull);
      expect(status.isPaused, false);
    });

    test('fromJson parses paused true', () {
      final status = TorrentStatus.fromJson({
        'torrent_id': 'abc',
        'paused': true,
      });
      expect(status.isPaused, true);
    });

    test('fromJson defaults isPaused to false when absent', () {
      final status = TorrentStatus.fromJson({
        'torrent_id': 'abc',
      });
      expect(status.isPaused, false);
    });

    // What each wanted file has, and what it is wanted for (the engine's
    // status since the app's phase P4).
    test('fromJson parses the wanted files and their roles', () {
      final status = TorrentStatus.fromJson({
        'torrent_id': 'abc',
        'selected_file': 4,
        'files': [
          {'index': 4, 'size': 1000, 'downloaded': 250, 'complete': false, 'role': 'reader'},
          {'index': 5, 'size': 2000, 'downloaded': 2000, 'complete': true, 'role': 'background'},
          {'index': 6, 'size': 30, 'downloaded': 30, 'complete': true},
        ],
      });

      final files = status.files!;
      expect(files, hasLength(3));
      expect(files[0].index, 4);
      expect(files[0].size, 1000);
      expect(files[0].downloaded, 250);
      expect(files[0].isComplete, false);
      expect(files[0].role, WantedFileRole.reader);
      expect(files[1].index, 5);
      expect(files[1].isComplete, true);
      expect(files[1].role, WantedFileRole.background);
      // Subtitles ride along: wanted, with no role of their own.
      expect(files[2].role, isNull);
    });

    test('fromJson leaves files null when the engine does not say', () {
      // An older library, a torrent without metadata, one just removed.
      expect(TorrentStatus.fromJson({'torrent_id': 'abc'}).files, isNull);
    });

    test('fromJson keeps an empty list of files apart from none', () {
      // Nothing wanted is not the same as not said.
      expect(TorrentStatus.fromJson({'torrent_id': 'abc', 'files': []}).files, isEmpty);
    });

    test('fromJson takes a role it does not know for no role', () {
      final status = TorrentStatus.fromJson({
        'torrent_id': 'abc',
        'files': [
          {'index': 1, 'size': 10, 'downloaded': 0, 'complete': false, 'role': 'later'},
        ],
      });
      expect(status.files!.single.role, isNull);
    });

    test('toJson writes the wanted files back', () {
      final json = {
        'torrent_id': 'abc',
        'files': [
          {'index': 4, 'size': 1000, 'downloaded': 250, 'complete': false, 'role': 'reader'},
          {'index': 6, 'size': 30, 'downloaded': 30, 'complete': true},
        ],
      };
      expect(TorrentStatus.fromJson(json).toJson()['files'], json['files']);
      expect(TorrentStatus.fromJson({'torrent_id': 'abc'}).toJson().containsKey('files'), false);
    });

    test('toJson writes the start numbers back', () {
      final json = {
        'piece_length': 524288,
        'startup_focus': false,
        'piece_reads': 3,
        'piece_cache_hits': 40,
      };
      final written = TorrentStatus.fromJson({'torrent_id': 'abc', ...json}).toJson();
      for (final key in json.keys) {
        expect(written[key], json[key], reason: key);
      }
      final empty = TorrentStatus.fromJson({'torrent_id': 'abc'}).toJson();
      expect(json.keys.where(empty.containsKey), isEmpty);
    });
  });

  group('SeekServeConfig', () {
    test('toJsonString includes only non-null fields', () {
      final config = SeekServeConfig(
        savePath: '/tmp/test',
        streamPort: 8080,
      );
      final json = config.toJsonString();
      expect(json, contains('"save_path":"/tmp/test"'));
      expect(json, contains('"stream_port":8080'));
      expect(json, isNot(contains('auth_token')));
    });

    test('empty config produces empty JSON', () {
      const config = SeekServeConfig();
      expect(config.toJsonString(), '{}');
    });
  });

  group('SeekServeEvent', () {
    test('parses metadata_received', () {
      final event = SeekServeEvent.fromJson({
        'type': 'metadata_received',
        'data': {'torrent_id': 'abc'},
      });
      expect(event, isA<MetadataReceived>());
      expect((event as MetadataReceived).torrentId, 'abc');
    });

    test('parses file_completed', () {
      final event = SeekServeEvent.fromJson({
        'type': 'file_completed',
        'data': {'torrent_id': 'abc', 'file_index': 3},
      });
      expect(event, isA<FileCompleted>());
      final fc = event as FileCompleted;
      expect(fc.torrentId, 'abc');
      expect(fc.fileIndex, 3);
    });

    test('parses error', () {
      final event = SeekServeEvent.fromJson({
        'type': 'error',
        'data': {'torrent_id': 'abc', 'message': 'timeout'},
      });
      expect(event, isA<TorrentError>());
      expect((event as TorrentError).message, 'timeout');
    });

    test('parses torrent_paused', () {
      final event = SeekServeEvent.fromJson({
        'type': 'torrent_paused',
        'data': {'torrent_id': 'abc'},
      });
      expect(event, isA<TorrentPaused>());
      expect((event as TorrentPaused).torrentId, 'abc');
    });

    test('parses torrent_resumed', () {
      final event = SeekServeEvent.fromJson({
        'type': 'torrent_resumed',
        'data': {'torrent_id': 'abc'},
      });
      expect(event, isA<TorrentResumed>());
      expect((event as TorrentResumed).torrentId, 'abc');
    });

    test('unknown type returns UnknownEvent', () {
      final event = SeekServeEvent.fromJson({
        'type': 'future_event',
        'data': {'foo': 'bar'},
      });
      expect(event, isA<UnknownEvent>());
      expect((event as UnknownEvent).type, 'future_event');
    });
  });
}
