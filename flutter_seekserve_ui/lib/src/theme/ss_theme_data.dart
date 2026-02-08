import 'package:flutter/widgets.dart';

/// Complete theme data for SeekServe UI widgets.
///
/// Provides colours, text styles and spacing that every Ss* widget reads
/// either explicitly or via [SsTheme.of].
class SsThemeData {
  // ── Base colours ──────────────────────────────────────────────────────
  final Color primary;
  final Color onPrimary;
  final Color surface;
  final Color onSurface;
  final Color background;
  final Color onBackground;
  final Color error;
  final Color onError;

  // ── Semantic torrent colours ──────────────────────────────────────────
  final Color downloading;
  final Color seeding;
  final Color paused;
  final Color checking;
  final Color buffering;
  final Color completed;

  // ── Text styles ───────────────────────────────────────────────────────
  final TextStyle headingStyle;
  final TextStyle bodyStyle;
  final TextStyle captionStyle;
  final TextStyle monoStyle;

  // ── Dimensions ────────────────────────────────────────────────────────
  final double borderRadius;
  final EdgeInsets cardPadding;
  final double iconSize;

  const SsThemeData({
    required this.primary,
    required this.onPrimary,
    required this.surface,
    required this.onSurface,
    required this.background,
    required this.onBackground,
    required this.error,
    required this.onError,
    required this.downloading,
    required this.seeding,
    required this.paused,
    required this.checking,
    required this.buffering,
    required this.completed,
    required this.headingStyle,
    required this.bodyStyle,
    required this.captionStyle,
    required this.monoStyle,
    this.borderRadius = 8.0,
    this.cardPadding = const EdgeInsets.all(12),
    this.iconSize = 24.0,
  });

  /// Dark theme preset.
  factory SsThemeData.dark() {
    const bg = Color(0xFF121212);
    const surface = Color(0xFF1E1E1E);
    const onSurface = Color(0xFFE0E0E0);
    const onBg = Color(0xFFE0E0E0);
    const primary = Color(0xFF7C4DFF);
    const onPrimary = Color(0xFFFFFFFF);

    return SsThemeData(
      primary: primary,
      onPrimary: onPrimary,
      surface: surface,
      onSurface: onSurface,
      background: bg,
      onBackground: onBg,
      error: const Color(0xFFEF5350),
      onError: const Color(0xFFFFFFFF),
      downloading: const Color(0xFF42A5F5),
      seeding: const Color(0xFF66BB6A),
      paused: const Color(0xFF9E9E9E),
      checking: const Color(0xFFFFCA28),
      buffering: const Color(0xFFFFA726),
      completed: const Color(0xFF2E7D32),
      headingStyle: const TextStyle(
        fontSize: 18,
        fontWeight: FontWeight.w600,
        color: onSurface,
      ),
      bodyStyle: const TextStyle(fontSize: 14, color: onSurface),
      captionStyle: const TextStyle(fontSize: 11, color: Color(0xFF9E9E9E)),
      monoStyle: const TextStyle(
        fontSize: 12,
        fontFamily: 'monospace',
        color: Color(0xFF9E9E9E),
      ),
    );
  }

  /// Light theme preset.
  factory SsThemeData.light() {
    const bg = Color(0xFFF5F5F5);
    const surface = Color(0xFFFFFFFF);
    const onSurface = Color(0xFF212121);
    const onBg = Color(0xFF212121);
    const primary = Color(0xFF6200EA);
    const onPrimary = Color(0xFFFFFFFF);

    return SsThemeData(
      primary: primary,
      onPrimary: onPrimary,
      surface: surface,
      onSurface: onSurface,
      background: bg,
      onBackground: onBg,
      error: const Color(0xFFD32F2F),
      onError: const Color(0xFFFFFFFF),
      downloading: const Color(0xFF1E88E5),
      seeding: const Color(0xFF43A047),
      paused: const Color(0xFF757575),
      checking: const Color(0xFFF9A825),
      buffering: const Color(0xFFFB8C00),
      completed: const Color(0xFF1B5E20),
      headingStyle: const TextStyle(
        fontSize: 18,
        fontWeight: FontWeight.w600,
        color: onSurface,
      ),
      bodyStyle: const TextStyle(fontSize: 14, color: onSurface),
      captionStyle: const TextStyle(fontSize: 11, color: Color(0xFF757575)),
      monoStyle: const TextStyle(
        fontSize: 12,
        fontFamily: 'monospace',
        color: Color(0xFF757575),
      ),
    );
  }

  /// Returns a copy with the given fields replaced.
  SsThemeData copyWith({
    Color? primary,
    Color? onPrimary,
    Color? surface,
    Color? onSurface,
    Color? background,
    Color? onBackground,
    Color? error,
    Color? onError,
    Color? downloading,
    Color? seeding,
    Color? paused,
    Color? checking,
    Color? buffering,
    Color? completed,
    TextStyle? headingStyle,
    TextStyle? bodyStyle,
    TextStyle? captionStyle,
    TextStyle? monoStyle,
    double? borderRadius,
    EdgeInsets? cardPadding,
    double? iconSize,
  }) {
    return SsThemeData(
      primary: primary ?? this.primary,
      onPrimary: onPrimary ?? this.onPrimary,
      surface: surface ?? this.surface,
      onSurface: onSurface ?? this.onSurface,
      background: background ?? this.background,
      onBackground: onBackground ?? this.onBackground,
      error: error ?? this.error,
      onError: onError ?? this.onError,
      downloading: downloading ?? this.downloading,
      seeding: seeding ?? this.seeding,
      paused: paused ?? this.paused,
      checking: checking ?? this.checking,
      buffering: buffering ?? this.buffering,
      completed: completed ?? this.completed,
      headingStyle: headingStyle ?? this.headingStyle,
      bodyStyle: bodyStyle ?? this.bodyStyle,
      captionStyle: captionStyle ?? this.captionStyle,
      monoStyle: monoStyle ?? this.monoStyle,
      borderRadius: borderRadius ?? this.borderRadius,
      cardPadding: cardPadding ?? this.cardPadding,
      iconSize: iconSize ?? this.iconSize,
    );
  }
}
