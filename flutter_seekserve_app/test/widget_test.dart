import 'package:flutter_test/flutter_test.dart';

import 'package:flutter_seekserve_app/main.dart';

void main() {
  testWidgets('App builds without error', (WidgetTester tester) async {
    await tester.pumpWidget(const SeekServeApp());
    // Just verify the app starts building without crashing.
    expect(find.text('Starting engine...'), findsOneWidget);
  });
}
