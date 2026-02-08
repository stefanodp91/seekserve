import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A simple spinning arc indicator with no Material dependency.
///
/// Used inside the [Video] widget's `controls` callback to show a
/// buffering indicator without breaking Android texture compositing.
class SsSpinner extends StatefulWidget {
  final double size;
  const SsSpinner({super.key, this.size = 32});

  @override
  State<SsSpinner> createState() => _SsSpinnerState();
}

class _SsSpinnerState extends State<SsSpinner>
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
    final color = SsTheme.of(context).primary;
    return SizedBox(
      width: widget.size,
      height: widget.size,
      child: AnimatedBuilder(
        animation: _ctrl,
        builder: (ctx, _) {
          return CustomPaint(
            painter: _ArcPainter(progress: _ctrl.value, color: color),
          );
        },
      ),
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
