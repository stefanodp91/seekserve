import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('SsThemeData', () {
    test('dark() creates valid dark theme', () {
      final theme = SsThemeData.dark();
      expect(theme.background, const Color(0xFF121212));
      expect(theme.surface, const Color(0xFF1E1E1E));
      expect(theme.primary, const Color(0xFF7C4DFF));
      expect(theme.borderRadius, 8.0);
    });

    test('light() creates valid light theme', () {
      final theme = SsThemeData.light();
      expect(theme.background, const Color(0xFFF5F5F5));
      expect(theme.surface, const Color(0xFFFFFFFF));
      expect(theme.primary, const Color(0xFF6200EA));
    });

    test('copyWith replaces specified fields', () {
      final dark = SsThemeData.dark();
      final custom =
          dark.copyWith(primary: const Color(0xFFFF0000), borderRadius: 16.0);
      expect(custom.primary, const Color(0xFFFF0000));
      expect(custom.borderRadius, 16.0);
      // Unchanged fields preserved.
      expect(custom.surface, dark.surface);
      expect(custom.background, dark.background);
    });

    test('semantic torrent colours are set', () {
      final theme = SsThemeData.dark();
      expect(theme.downloading, isNotNull);
      expect(theme.seeding, isNotNull);
      expect(theme.paused, isNotNull);
      expect(theme.checking, isNotNull);
      expect(theme.buffering, isNotNull);
      expect(theme.completed, isNotNull);
    });
  });

  group('SsTheme InheritedWidget', () {
    testWidgets('of() returns dark fallback when no SsTheme ancestor',
        (tester) async {
      late SsThemeData resolved;
      await tester.pumpWidget(
        Builder(builder: (ctx) {
          resolved = SsTheme.of(ctx);
          return const SizedBox();
        }),
      );
      expect(resolved.background, SsThemeData.dark().background);
    });

    testWidgets('of() returns provided theme data', (tester) async {
      final light = SsThemeData.light();
      late SsThemeData resolved;
      await tester.pumpWidget(
        SsTheme(
          data: light,
          child: Builder(builder: (ctx) {
            resolved = SsTheme.of(ctx);
            return const SizedBox();
          }),
        ),
      );
      expect(resolved.background, light.background);
      expect(resolved.primary, light.primary);
    });
  });
}
