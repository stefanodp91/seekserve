import 'package:flutter/widgets.dart';

import '../atoms/ss_button.dart';
import '../atoms/ss_dialog.dart';
import '../theme/ss_theme.dart';

/// A confirmation dialog for removing a torrent.
///
/// Returns `true` for "remove torrent only", `false` for "remove + delete files",
/// or `null` if cancelled.
class SsDeleteConfirm {
  SsDeleteConfirm._();

  /// Shows the confirmation dialog.
  ///
  /// [torrentName] is displayed in the dialog body.
  /// Returns `true` if the user chose "torrent only" (keep files),
  /// `false` if "torrent + files", `null` if cancelled.
  static Future<bool?> show(
    BuildContext context, {
    required String torrentName,
  }) {
    final theme = SsTheme.of(context);

    return SsDialog.show<bool>(
      context,
      title: 'Remove torrent?',
      content: Text(
        torrentName,
        style: theme.bodyStyle,
        maxLines: 3,
        overflow: TextOverflow.ellipsis,
      ),
      actions: [
        SsDialogAction(
          label: 'Cancel',
          variant: SsButtonVariant.secondary,
          onPressed: () => Navigator.of(context).pop<bool>(null),
        ),
        SsDialogAction(
          label: 'Keep files',
          variant: SsButtonVariant.primary,
          onPressed: () => Navigator.of(context).pop<bool>(true),
        ),
        SsDialogAction(
          label: 'Delete all',
          variant: SsButtonVariant.danger,
          onPressed: () => Navigator.of(context).pop<bool>(false),
        ),
      ],
    );
  }
}
