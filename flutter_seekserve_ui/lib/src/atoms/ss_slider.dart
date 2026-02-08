import 'package:flutter/widgets.dart';

import '../theme/ss_theme.dart';

/// A horizontal slider with no Material dependency.
class SsSlider extends StatefulWidget {
  final double value;
  final double min;
  final double max;
  final ValueChanged<double>? onChanged;
  final ValueChanged<double>? onChangeEnd;
  final Color? activeColor;
  final Color? trackColor;
  final double height;

  const SsSlider({
    super.key,
    required this.value,
    this.min = 0.0,
    this.max = 1.0,
    this.onChanged,
    this.onChangeEnd,
    this.activeColor,
    this.trackColor,
    this.height = 32.0,
  });

  @override
  State<SsSlider> createState() => _SsSliderState();
}

class _SsSliderState extends State<SsSlider> {
  bool _dragging = false;
  double _dragValue = 0;

  double get _currentValue => _dragging ? _dragValue : widget.value;

  double _clamp(double v) => v.clamp(widget.min, widget.max);

  double _positionToValue(double dx, double width) {
    if (width <= 0) return widget.min;
    final ratio = (dx / width).clamp(0.0, 1.0);
    return widget.min + ratio * (widget.max - widget.min);
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final active = widget.activeColor ?? theme.primary;
    final track = widget.trackColor ?? theme.onSurface.withValues(alpha: 0.15);
    final enabled = widget.onChanged != null;

    return GestureDetector(
      onHorizontalDragStart: enabled
          ? (d) {
              setState(() {
                _dragging = true;
                _dragValue = _clamp(
                  _positionToValue(d.localPosition.dx, context.size!.width),
                );
              });
              widget.onChanged?.call(_dragValue);
            }
          : null,
      onHorizontalDragUpdate: enabled
          ? (d) {
              setState(() {
                _dragValue = _clamp(
                  _positionToValue(d.localPosition.dx, context.size!.width),
                );
              });
              widget.onChanged?.call(_dragValue);
            }
          : null,
      onHorizontalDragEnd: enabled
          ? (d) {
              widget.onChangeEnd?.call(_dragValue);
              setState(() => _dragging = false);
            }
          : null,
      onTapUp: enabled
          ? (d) {
              final v = _clamp(
                _positionToValue(d.localPosition.dx, context.size!.width),
              );
              widget.onChanged?.call(v);
              widget.onChangeEnd?.call(v);
            }
          : null,
      child: SizedBox(
        height: widget.height,
        child: CustomPaint(
          painter: _SliderPainter(
            value: _currentValue,
            min: widget.min,
            max: widget.max,
            activeColor: active,
            trackColor: track,
            thumbRadius: 7,
          ),
          size: Size.infinite,
        ),
      ),
    );
  }
}

class _SliderPainter extends CustomPainter {
  final double value;
  final double min;
  final double max;
  final Color activeColor;
  final Color trackColor;
  final double thumbRadius;

  _SliderPainter({
    required this.value,
    required this.min,
    required this.max,
    required this.activeColor,
    required this.trackColor,
    required this.thumbRadius,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final trackY = size.height / 2;
    const trackH = 3.0;
    final range = max - min;
    final ratio = range > 0 ? ((value - min) / range).clamp(0.0, 1.0) : 0.0;
    final thumbX = ratio * size.width;

    // Track background
    final trackPaint = Paint()..color = trackColor;
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromCenter(
          center: Offset(size.width / 2, trackY),
          width: size.width,
          height: trackH,
        ),
        const Radius.circular(1.5),
      ),
      trackPaint,
    );

    // Active track
    if (ratio > 0) {
      final activePaint = Paint()..color = activeColor;
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(0, trackY - trackH / 2, thumbX, trackH),
          const Radius.circular(1.5),
        ),
        activePaint,
      );
    }

    // Thumb
    final thumbPaint = Paint()..color = activeColor;
    canvas.drawCircle(Offset(thumbX, trackY), thumbRadius, thumbPaint);
  }

  @override
  bool shouldRepaint(_SliderPainter old) =>
      old.value != value ||
      old.activeColor != activeColor ||
      old.trackColor != trackColor;
}
