import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve_ui/flutter_seekserve_ui.dart';
import 'package:flutter_test/flutter_test.dart';

/// Wraps a widget with SsTheme for testing.
Widget _themed(Widget child) {
  return SsTheme(
    data: SsThemeData.dark(),
    child: Directionality(
      textDirection: TextDirection.ltr,
      child: child,
    ),
  );
}

void main() {
  group('SsButton', () {
    testWidgets('renders label', (tester) async {
      await tester.pumpWidget(_themed(
        const SsButton(label: 'Add Torrent'),
      ));
      expect(find.text('Add Torrent'), findsOneWidget);
    });

    testWidgets('calls onPressed on tap', (tester) async {
      var pressed = false;
      await tester.pumpWidget(_themed(
        SsButton(
          label: 'Click',
          onPressed: () => pressed = true,
        ),
      ));
      await tester.tap(find.text('Click'));
      expect(pressed, isTrue);
    });

    testWidgets('disabled when onPressed is null', (tester) async {
      await tester.pumpWidget(_themed(
        const SsButton(label: 'Disabled'),
      ));
      // Opacity should be 0.4 for disabled.
      final opacity = tester.widget<Opacity>(find.byType(Opacity));
      expect(opacity.opacity, 0.4);
    });
  });

  group('SsBadge', () {
    testWidgets('renders uppercase label', (tester) async {
      await tester.pumpWidget(_themed(
        const SsBadge(label: 'downloading'),
      ));
      expect(find.text('DOWNLOADING'), findsOneWidget);
    });
  });

  group('SsProgressBar', () {
    testWidgets('renders without error', (tester) async {
      await tester.pumpWidget(_themed(
        const SizedBox(
          width: 200,
          child: SsProgressBar(value: 0.5),
        ),
      ));
      expect(find.byType(SsProgressBar), findsOneWidget);
    });

    testWidgets('clamps value to 0-1', (tester) async {
      // Should not throw.
      await tester.pumpWidget(_themed(
        const SizedBox(
          width: 200,
          child: SsProgressBar(value: 1.5),
        ),
      ));
      expect(find.byType(SsProgressBar), findsOneWidget);
    });
  });

  group('SsCard', () {
    testWidgets('renders child', (tester) async {
      await tester.pumpWidget(_themed(
        const SsCard(child: Text('Card content')),
      ));
      expect(find.text('Card content'), findsOneWidget);
    });

    testWidgets('calls onTap', (tester) async {
      var tapped = false;
      await tester.pumpWidget(_themed(
        SsCard(
          onTap: () => tapped = true,
          child: const Text('Tap me'),
        ),
      ));
      await tester.tap(find.text('Tap me'));
      expect(tapped, isTrue);
    });
  });

  group('SsChip', () {
    testWidgets('renders label', (tester) async {
      await tester.pumpWidget(_themed(
        const SsChip(label: '5 peers'),
      ));
      expect(find.text('5 peers'), findsOneWidget);
    });
  });
}
