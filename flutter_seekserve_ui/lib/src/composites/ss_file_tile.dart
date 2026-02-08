import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';

import '../theme/ss_theme.dart';
import '../utils/format.dart';

/// Icon code points from CupertinoIcons / MaterialIcons (font-independent).
/// We use Unicode box-drawing as fallback since we avoid Material imports.
const _kIconVideo = 0xe40d;    // movie (MaterialIcons)
const _kIconAudio = 0xe0b6;    // audiotrack
const _kIconSubtitle = 0xe619; // subtitles
const _kIconImage = 0xe332;    // image
const _kIconDocument = 0xe1bf; // description
const _kIconFile = 0xe342;     // insert_drive_file
const _kIconPlay = 0xe4cb;     // play_arrow

IconData _iconForCategory(FileCategory cat) {
  switch (cat) {
    case FileCategory.video:
      return const IconData(_kIconVideo, fontFamily: 'MaterialIcons');
    case FileCategory.audio:
      return const IconData(_kIconAudio, fontFamily: 'MaterialIcons');
    case FileCategory.subtitle:
      return const IconData(_kIconSubtitle, fontFamily: 'MaterialIcons');
    case FileCategory.image:
      return const IconData(_kIconImage, fontFamily: 'MaterialIcons');
    case FileCategory.document:
      return const IconData(_kIconDocument, fontFamily: 'MaterialIcons');
    case FileCategory.other:
      return const IconData(_kIconFile, fontFamily: 'MaterialIcons');
  }
}

/// A row representing a file inside a torrent.
class SsFileTile extends StatelessWidget {
  final FileInfo file;
  final VoidCallback? onTap;
  final bool enabled;

  const SsFileTile({
    super.key,
    required this.file,
    this.onTap,
    this.enabled = true,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    final cat = fileCategoryFromExtension(file.extension);
    final canPlay = enabled && onTap != null;
    final opacity = canPlay ? 1.0 : 0.5;

    return GestureDetector(
      onTap: canPlay ? onTap : null,
      child: Opacity(
        opacity: opacity,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          decoration: BoxDecoration(
            border: Border(
              bottom: BorderSide(
                color: theme.onSurface.withValues(alpha: 0.06),
              ),
            ),
          ),
          child: Row(
            children: [
              Icon(
                _iconForCategory(cat),
                size: 22,
                color: canPlay ? theme.primary : theme.onSurface,
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      file.name,
                      style: theme.bodyStyle,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                    ),
                    Text(formatBytes(file.size), style: theme.captionStyle),
                  ],
                ),
              ),
              if (canPlay)
                Icon(
                  const IconData(_kIconPlay, fontFamily: 'MaterialIcons'),
                  size: 20,
                  color: theme.primary,
                ),
            ],
          ),
        ),
      ),
    );
  }
}
