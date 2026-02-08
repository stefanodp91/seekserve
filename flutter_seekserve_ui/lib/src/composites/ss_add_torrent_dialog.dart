import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

import '../atoms/ss_button.dart';
import '../theme/ss_theme.dart';

/// Full-screen dialog for adding a torrent via magnet URI or .torrent path.
///
/// Returns the entered URI string, or null if dismissed.
class SsAddTorrentDialog extends StatefulWidget {
  const SsAddTorrentDialog({super.key});

  /// Shows the dialog as a transparent overlay and returns the URI on submit.
  static Future<String?> show(BuildContext context) {
    return Navigator.of(context).push<String>(
      PageRouteBuilder(
        opaque: false,
        barrierDismissible: true,
        barrierColor: const Color(0x80000000),
        pageBuilder: (ctx, anim2, anim3) => const SsAddTorrentDialog(),
        transitionsBuilder: (ctx, anim, anim2, child) {
          return FadeTransition(opacity: anim, child: child);
        },
      ),
    );
  }

  @override
  State<SsAddTorrentDialog> createState() => _SsAddTorrentDialogState();
}

class _SsAddTorrentDialogState extends State<SsAddTorrentDialog> {
  final _controller = TextEditingController();
  final _focusNode = FocusNode();

  @override
  void initState() {
    super.initState();
    // Auto-focus after the route transition.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _focusNode.requestFocus();
    });
  }

  @override
  void dispose() {
    _controller.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  void _submit() {
    final text = _controller.text.trim();
    if (text.isEmpty) return;
    Navigator.of(context).pop(text);
  }

  Future<void> _paste() async {
    final data = await Clipboard.getData(Clipboard.kTextPlain);
    if (data?.text != null && data!.text!.isNotEmpty) {
      _controller.text = data.text!;
      _controller.selection = TextSelection.fromPosition(
        TextPosition(offset: _controller.text.length),
      );
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);

    return Center(
      child: Container(
        constraints: const BoxConstraints(maxWidth: 380),
        margin: const EdgeInsets.all(24),
        padding: const EdgeInsets.all(20),
        decoration: BoxDecoration(
          color: theme.surface,
          borderRadius: BorderRadius.circular(theme.borderRadius * 2),
          border: Border.all(color: theme.onSurface.withValues(alpha: 0.1)),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Add Torrent', style: theme.headingStyle),
            const SizedBox(height: 6),
            Text(
              'Paste a magnet link or .torrent URL',
              style: theme.captionStyle,
            ),
            const SizedBox(height: 16),
            // Text input
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              decoration: BoxDecoration(
                color: theme.background,
                borderRadius: BorderRadius.circular(theme.borderRadius),
                border: Border.all(
                  color: theme.onSurface.withValues(alpha: 0.2),
                ),
              ),
              child: EditableText(
                controller: _controller,
                focusNode: _focusNode,
                style: theme.monoStyle.copyWith(fontSize: 14),
                cursorColor: theme.primary,
                backgroundCursorColor: theme.surface,
                onSubmitted: (_) => _submit(),
                maxLines: 3,
                minLines: 2,
              ),
            ),
            const SizedBox(height: 16),
            // Paste from clipboard
            GestureDetector(
              onTap: _paste,
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(
                    const IconData(0xe192, fontFamily: 'MaterialIcons'),
                    color: theme.primary,
                    size: 18,
                  ),
                  const SizedBox(width: 6),
                  Text(
                    'Paste from clipboard',
                    style: theme.bodyStyle.copyWith(color: theme.primary),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 20),
            // Action buttons
            Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                SsButton(
                  label: 'Cancel',
                  variant: SsButtonVariant.secondary,
                  onPressed: () => Navigator.of(context).pop(),
                ),
                const SizedBox(width: 8),
                SsButton(
                  label: 'Add',
                  variant: SsButtonVariant.primary,
                  onPressed: _submit,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
