import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'ss_extra_subtitles.dart';
import 'ss_strings.dart';

/// The host's hold over the video (app WORK-47): the message, the bar and,
/// once known, the time left; nothing without a hold, or with a hold that is
/// not visible (app BUG-336). It never takes a tap: the player's ▶ stays
/// reachable (app DEC-945).
class SsHoldOverlay extends StatelessWidget {
  const SsHoldOverlay({super.key, required this.hold, required this.strings});

  final ValueListenable<SsHold?> hold;
  final SsStrings strings;

  static const _text = TextStyle(fontSize: 15, color: Color(0xFFFFFFFF));
  static const _small = TextStyle(fontSize: 13, color: Color(0xCCFFFFFF));

  @override
  Widget build(BuildContext context) => ValueListenableBuilder<SsHold?>(
        valueListenable: hold,
        builder: (context, value, _) {
          if (value == null || !value.visible) return const SizedBox.shrink();
          final remaining = value.remaining;
          return IgnorePointer(
            child: Center(
              child: Container(
                constraints: const BoxConstraints(maxWidth: 320),
                margin: const EdgeInsets.symmetric(horizontal: 24),
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(color: const Color(0xCC000000), borderRadius: BorderRadius.circular(12)),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text(value.message, style: _text, textAlign: TextAlign.center),
                    const SizedBox(height: 12),
                    LinearProgressIndicator(value: value.progress),
                    if (remaining != null) ...[
                      const SizedBox(height: 8),
                      Text(strings.holdRemaining(remaining), style: _small),
                    ],
                  ],
                ),
              ),
            ),
          );
        },
      );
}
