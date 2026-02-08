import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('formatBytes', () {
    test('formats bytes', () => expect(formatBytes(512), '512 B'));
    test('formats KB', () => expect(formatBytes(1536), '1.5 KB'));
    test('formats MB', () => expect(formatBytes(73 * 1024 * 1024), '73.0 MB'));
    test('formats GB',
        () => expect(formatBytes(2 * 1024 * 1024 * 1024), '2.00 GB'));
    test('handles zero', () => expect(formatBytes(0), '0 B'));
  });

  group('formatRate', () {
    test('formats B/s', () => expect(formatRate(500), '500 B/s'));
    test('formats KB/s', () => expect(formatRate(2048), '2.0 KB/s'));
    test('formats MB/s',
        () => expect(formatRate(5 * 1024 * 1024), '5.0 MB/s'));
  });

  group('formatDuration', () {
    test('mm:ss', () {
      expect(formatDuration(const Duration(minutes: 3, seconds: 7)), '03:07');
    });
    test('h:mm:ss', () {
      expect(
        formatDuration(
            const Duration(hours: 1, minutes: 23, seconds: 45)),
        '1:23:45',
      );
    });
    test('zero', () => expect(formatDuration(Duration.zero), '00:00'));
  });

  group('fileCategoryFromExtension', () {
    test('video', () {
      expect(fileCategoryFromExtension('mp4'), FileCategory.video);
      expect(fileCategoryFromExtension('mkv'), FileCategory.video);
    });
    test('audio', () {
      expect(fileCategoryFromExtension('mp3'), FileCategory.audio);
    });
    test('subtitle', () {
      expect(fileCategoryFromExtension('srt'), FileCategory.subtitle);
    });
    test('image', () {
      expect(fileCategoryFromExtension('png'), FileCategory.image);
    });
    test('document', () {
      expect(fileCategoryFromExtension('txt'), FileCategory.document);
    });
    test('other', () {
      expect(fileCategoryFromExtension('xyz'), FileCategory.other);
    });
  });
}
