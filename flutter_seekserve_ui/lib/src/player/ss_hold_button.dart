import 'package:flutter/material.dart';

/// The player's button while the host holds the video (app DEC-988,
/// DEC-1012): waiting, a ⏸ inside a turning ring, as a video that loads;
/// [paused] by the person, a ▶ without the ring; [stopped], when the hold
/// waits only for the person's choice, a ⏸ without the ring (app DEC-1015).
/// A tap is a real pause, or back to waiting: the video never goes on before
/// the host lets it. The screen reader hears [label].
class SsHoldButton extends StatelessWidget {
  const SsHoldButton({
    super.key,
    required this.paused,
    required this.onPressed,
    required this.label,
    this.stopped = false,
    this.size = 64,
  });

  final bool paused;
  final bool stopped;
  final VoidCallback onPressed;
  final String label;
  final double size;

  static const _playArrow = IconData(0xe4cb, fontFamily: 'MaterialIcons');
  static const _pause = IconData(0xe47c, fontFamily: 'MaterialIcons');
  static const _white = Color(0xFFFFFFFF);

  @override
  Widget build(BuildContext context) => Semantics(
        button: true,
        label: label,
        excludeSemantics: true,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: onPressed,
          child: SizedBox.square(
            dimension: size,
            child: Stack(
              alignment: Alignment.center,
              children: [
                DecoratedBox(
                  decoration: const BoxDecoration(color: Color(0x99000000), shape: BoxShape.circle),
                  child: SizedBox.square(dimension: size - 8),
                ),
                if (!paused && !stopped)
                  SizedBox.square(
                    dimension: size,
                    child: const CircularProgressIndicator(strokeWidth: 3, color: _white),
                  ),
                Icon(paused ? _playArrow : _pause, size: size * 0.55, color: _white),
              ],
            ),
          ),
        ),
      );
}
