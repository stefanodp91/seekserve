import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// Semi-transparent overlay shown while the player is buffering.
class SsBufferingOverlay extends StatelessWidget {
  final bool isBuffering;

  const SsBufferingOverlay({super.key, required this.isBuffering});

  @override
  Widget build(BuildContext context) {
    if (!isBuffering) return const SizedBox.shrink();

    final theme = SsTheme.of(context);

    return Positioned.fill(
      child: Container(
        color: const Color(0x80000000),
        alignment: Alignment.center,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            SizedBox(
              width: 32,
              height: 32,
              child: _SpinningIndicator(color: theme.primary),
            ),
            const SizedBox(height: 8),
            Text(
              'Buffering...',
              style: TextStyle(
                fontSize: 13,
                color: theme.onSurface.withValues(alpha: 0.8),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// A simple spinning arc indicator with no Material dependency.
class _SpinningIndicator extends StatefulWidget {
  final Color color;
  const _SpinningIndicator({required this.color});

  @override
  State<_SpinningIndicator> createState() => _SpinningIndicatorState();
}

class _SpinningIndicatorState extends State<_SpinningIndicator>
    with SingleTickerProviderStateMixin {
  late final AnimationController _ctrl;

  @override
  void initState() {
    super.initState();
    _ctrl = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 800),
    )..repeat();
  }

  @override
  void dispose() {
    _ctrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _ctrl,
      builder: (ctx, _) {
        return CustomPaint(
          painter: _ArcPainter(
            progress: _ctrl.value,
            color: widget.color,
          ),
        );
      },
    );
  }
}

class _ArcPainter extends CustomPainter {
  final double progress;
  final Color color;

  _ArcPainter({required this.progress, required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..strokeCap = StrokeCap.round;

    final rect = Offset.zero & size;
    const sweepAngle = 4.0; // radians (~230 degrees)
    final startAngle = progress * 6.2831853; // 2*pi

    canvas.drawArc(rect.deflate(2), startAngle, sweepAngle, false, paint);
  }

  @override
  bool shouldRepaint(_ArcPainter old) => old.progress != progress;
}
