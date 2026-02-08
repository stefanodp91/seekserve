import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A linear progress bar with no Material dependency.
class SsProgressBar extends StatelessWidget {
  /// Progress value between 0.0 and 1.0.
  final double value;
  final Color? color;
  final Color? trackColor;
  final double height;

  const SsProgressBar({
    super.key,
    required this.value,
    this.color,
    this.trackColor,
    this.height = 4.0,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final barColor = color ?? theme.primary;
    final track = trackColor ?? theme.onSurface.withValues(alpha: 0.12);
    final clamped = value.clamp(0.0, 1.0);

    return ClipRRect(
      borderRadius: BorderRadius.circular(height / 2),
      child: SizedBox(
        height: height,
        child: CustomPaint(
          painter: _ProgressPainter(
            progress: clamped,
            barColor: barColor,
            trackColor: track,
          ),
          size: Size.infinite,
        ),
      ),
    );
  }
}

class _ProgressPainter extends CustomPainter {
  final double progress;
  final Color barColor;
  final Color trackColor;

  _ProgressPainter({
    required this.progress,
    required this.barColor,
    required this.trackColor,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final trackPaint = Paint()..color = trackColor;
    canvas.drawRect(Offset.zero & size, trackPaint);

    if (progress > 0) {
      final barPaint = Paint()..color = barColor;
      canvas.drawRect(
        Offset.zero & Size(size.width * progress, size.height),
        barPaint,
      );
    }
  }

  @override
  bool shouldRepaint(_ProgressPainter old) =>
      old.progress != progress ||
      old.barColor != barColor ||
      old.trackColor != trackColor;
}
