import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';

import '../theme/ss_theme.dart';
import 'ss_file_tile.dart';

/// Node in the virtual file tree.
class _TreeNode {
  final Map<String, _TreeNode> children = {};
  final List<FileInfo> files = [];
}

/// Builds a navigable tree of files grouped by directory.
///
/// Flat file lists are shown directly; nested structures get expandable folders.
class SsFileTree extends StatefulWidget {
  final List<FileInfo> files;
  final void Function(FileInfo file)? onFileTap;

  const SsFileTree({
    super.key,
    required this.files,
    this.onFileTap,
  });

  @override
  State<SsFileTree> createState() => _SsFileTreeState();
}

class _SsFileTreeState extends State<SsFileTree> {
  final Set<String> _expanded = {};

  _TreeNode _buildTree() {
    final root = _TreeNode();
    for (final f in widget.files) {
      final parts = f.path.split('/');
      var node = root;
      // Navigate to parent folder, creating intermediate nodes.
      for (var i = 0; i < parts.length - 1; i++) {
        node = node.children.putIfAbsent(parts[i], () => _TreeNode());
      }
      node.files.add(f);
    }
    return root;
  }

  @override
  Widget build(BuildContext context) {
    final root = _buildTree();
    // If there's a single top-level dir with everything inside, skip it.
    var effective = root;
    var prefix = '';
    while (effective.files.isEmpty && effective.children.length == 1) {
      final entry = effective.children.entries.first;
      prefix = prefix.isEmpty ? entry.key : '$prefix/${entry.key}';
      effective = entry.value;
    }
    return ListView(
      children: _buildNodes(effective, prefix),
    );
  }

  List<Widget> _buildNodes(_TreeNode node, String path, [int depth = 0]) {
    final widgets = <Widget>[];

    // Folders first (sorted).
    final sortedDirs = node.children.keys.toList()..sort();
    for (final dir in sortedDirs) {
      final fullPath = path.isEmpty ? dir : '$path/$dir';
      final isOpen = _expanded.contains(fullPath);
      widgets.add(_FolderRow(
        name: dir,
        depth: depth,
        isExpanded: isOpen,
        onTap: () {
          setState(() {
            if (isOpen) {
              _expanded.remove(fullPath);
            } else {
              _expanded.add(fullPath);
            }
          });
        },
      ));
      if (isOpen) {
        widgets.addAll(_buildNodes(node.children[dir]!, fullPath, depth + 1));
      }
    }

    // Files (sorted by index).
    final canTap = widget.onFileTap != null;
    final sortedFiles = node.files.toList()..sort((a, b) => a.index - b.index);
    for (final f in sortedFiles) {
      final tappable = f.isVideo && canTap;
      widgets.add(Padding(
        padding: EdgeInsets.only(left: depth * 16.0),
        child: SsFileTile(
          file: f,
          enabled: tappable,
          onTap: tappable ? () => widget.onFileTap!.call(f) : null,
        ),
      ));
    }

    return widgets;
  }
}

class _FolderRow extends StatelessWidget {
  final String name;
  final int depth;
  final bool isExpanded;
  final VoidCallback onTap;

  const _FolderRow({
    required this.name,
    required this.depth,
    required this.isExpanded,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    // folder / folder_open (MaterialIcons)
    final icon = isExpanded
        ? const IconData(0xe2a4, fontFamily: 'MaterialIcons') // folder_open
        : const IconData(0xe2a3, fontFamily: 'MaterialIcons'); // folder

    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: EdgeInsets.only(
          left: 12.0 + depth * 16.0,
          right: 12,
          top: 8,
          bottom: 8,
        ),
        child: Row(
          children: [
            Icon(icon, size: 20, color: theme.onSurface.withValues(alpha: 0.7)),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                name,
                style: theme.bodyStyle.copyWith(fontWeight: FontWeight.w500),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ),
            Icon(
              isExpanded
                  ? const IconData(0xe245, fontFamily: 'MaterialIcons') // expand_less
                  : const IconData(0xe246, fontFamily: 'MaterialIcons'), // expand_more
              size: 18,
              color: theme.onSurface.withValues(alpha: 0.5),
            ),
          ],
        ),
      ),
    );
  }
}
