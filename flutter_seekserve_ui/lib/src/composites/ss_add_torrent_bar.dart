import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

import '../atoms/ss_icon_button.dart';
import '../theme/ss_theme.dart';

/// Input bar for adding a torrent by magnet URI or .torrent path.
class SsAddTorrentBar extends StatefulWidget {
  final void Function(String uri) onAdd;

  const SsAddTorrentBar({super.key, required this.onAdd});

  @override
  State<SsAddTorrentBar> createState() => _SsAddTorrentBarState();
}

class _SsAddTorrentBarState extends State<SsAddTorrentBar> {
  final _controller = TextEditingController();
  final _focusNode = FocusNode();

  void _submit() {
    final text = _controller.text.trim();
    if (text.isEmpty) return;
    widget.onAdd(text);
    _controller.clear();
  }

  Future<void> _paste() async {
    final data = await Clipboard.getData(Clipboard.kTextPlain);
    if (data?.text != null && data!.text!.isNotEmpty) {
      _controller.text = data.text!;
      _controller.selection = TextSelection.fromPosition(
        TextPosition(offset: _controller.text.length),
      );
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    const pasteIcon = IconData(0xe192, fontFamily: 'MaterialIcons');
    const addIcon = IconData(0xe047, fontFamily: 'MaterialIcons');

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      decoration: BoxDecoration(
        color: theme.surface,
        border: Border(
          bottom: BorderSide(color: theme.onSurface.withValues(alpha: 0.1)),
        ),
      ),
      child: Row(
        children: [
          SsIconButton(
            icon: pasteIcon,
            onPressed: _paste,
            color: theme.onSurface.withValues(alpha: 0.6),
          ),
          Expanded(
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 10),
              decoration: BoxDecoration(
                color: theme.background,
                borderRadius: BorderRadius.circular(theme.borderRadius),
                border: Border.all(
                  color: theme.onSurface.withValues(alpha: 0.15),
                ),
              ),
              child: EditableText(
                controller: _controller,
                focusNode: _focusNode,
                style: theme.bodyStyle.copyWith(fontSize: 13),
                cursorColor: theme.primary,
                backgroundCursorColor: theme.surface,
                onSubmitted: (_) => _submit(),
                maxLines: 1,
              ),
            ),
          ),
          SsIconButton(
            icon: addIcon,
            onPressed: _submit,
            color: theme.primary,
          ),
        ],
      ),
    );
  }
}
