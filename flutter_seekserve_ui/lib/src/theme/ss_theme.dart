import 'package:flutter/widgets.dart';

import 'ss_theme_data.dart';
export 'ss_theme_data.dart';

/// Provides [SsThemeData] to the widget tree via an [InheritedWidget].
///
/// Wrap your app (or a subtree) with [SsTheme] so that every Ss* widget
/// can look up theme colours and styles with `SsTheme.of(context)`.
class SsTheme extends InheritedWidget {
  final SsThemeData data;

  const SsTheme({
    super.key,
    required this.data,
    required super.child,
  });

  /// Returns the nearest [SsThemeData] up the tree, falling back to dark.
  static SsThemeData of(BuildContext context) {
    final widget = context.dependOnInheritedWidgetOfExactType<SsTheme>();
    return widget?.data ?? SsThemeData.dark();
  }

  @override
  bool updateShouldNotify(SsTheme oldWidget) => data != oldWidget.data;
}
