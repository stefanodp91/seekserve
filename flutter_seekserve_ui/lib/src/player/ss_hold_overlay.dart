import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import 'ss_extra_subtitles.dart';
import 'ss_hold_button.dart';
import 'ss_strings.dart';

/// The host's hold over the video (app WORK-47, DEC-988, DEC-1012): the
/// waiting button and, with the panel, what the video waits for, what to
/// expect, the bar, the time left once known and the host's ways out. Nothing
/// without a hold, or with a hold that is not visible (app BUG-336). Only the
/// button and the ways out take a tap: elsewhere a tap still reaches the
/// player, which shows its controls.
class SsHoldOverlay extends StatelessWidget {
  const SsHoldOverlay({
    super.key,
    required this.hold,
    required this.paused,
    required this.onTogglePause,
    required this.strings,
  });

  final ValueListenable<SsHold?> hold;

  /// Whether the person paused the video during the hold.
  final ValueListenable<bool> paused;

  /// The person's tap on the waiting button.
  final VoidCallback onTogglePause;
  final SsStrings strings;

  static const _text = TextStyle(fontSize: 15, color: Color(0xFFFFFFFF));
  static const _small = TextStyle(fontSize: 13, color: Color(0xCCFFFFFF));

  @override
  Widget build(BuildContext context) => ValueListenableBuilder<SsHold?>(
        valueListenable: hold,
        builder: (context, value, _) {
          if (value == null || !value.visible) return const SizedBox.shrink();
          return Center(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                ValueListenableBuilder<bool>(
                  valueListenable: paused,
                  builder: (context, isPaused, _) => SsHoldButton(
                    paused: isPaused,
                    onPressed: onTogglePause,
                    label: isPaused ? strings.holdPausedLabel : strings.holdWaitingLabel,
                  ),
                ),
                if (value.panel) ...[
                  const SizedBox(height: 12),
                  _panel(value),
                ],
              ],
            ),
          );
        },
      );

  Widget _panel(SsHold value) {
    final remaining = value.remaining;
    final detail = value.detail;
    return Container(
      constraints: const BoxConstraints(maxWidth: 320),
      margin: const EdgeInsets.symmetric(horizontal: 24),
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(color: const Color(0xCC000000), borderRadius: BorderRadius.circular(12)),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          IgnorePointer(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Semantics(
                  liveRegion: true,
                  child: Text(value.message, style: _text, textAlign: TextAlign.center),
                ),
                if (detail != null) ...[
                  const SizedBox(height: 4),
                  Text(detail, style: _small, textAlign: TextAlign.center),
                ],
                const SizedBox(height: 12),
                LinearProgressIndicator(value: value.progress),
                if (remaining != null) ...[
                  const SizedBox(height: 8),
                  Text(strings.holdRemaining(remaining), style: _small),
                ],
              ],
            ),
          ),
          if (value.actions.isNotEmpty) ...[
            const SizedBox(height: 8),
            Wrap(
              alignment: WrapAlignment.center,
              spacing: 8,
              children: [for (final action in value.actions) _action(action)],
            ),
          ],
        ],
      ),
    );
  }

  static Widget _action(SsHoldAction action) => action.emphasized
      ? FilledButton(onPressed: action.onPressed, child: Text(action.label))
      : TextButton(
          onPressed: action.onPressed,
          child: Text(action.label, style: const TextStyle(color: Color(0xFFFFFFFF))),
        );
}
