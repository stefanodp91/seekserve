import 'package:flutter/widgets.dart';
import 'package:flutter_seekserve/seekserve.dart';

import '../atoms/ss_chip.dart';
import '../theme/ss_theme.dart';
import '../utils/format.dart';

/// Shows download/upload rates and peer/seed counts as chips.
class SsTransferStats extends StatelessWidget {
  final TorrentStatus status;

  const SsTransferStats({super.key, required this.status});

  @override
  Widget build(BuildContext context) {
    final theme = SsTheme.of(context);
    const dlIcon = IconData(0xe26a, fontFamily: 'MaterialIcons');
    const ulIcon = IconData(0xe26e, fontFamily: 'MaterialIcons');
    const peerIcon = IconData(0xe486, fontFamily: 'MaterialIcons');

    return Wrap(
      spacing: 12,
      runSpacing: 4,
      children: [
        SsChip(
          icon: dlIcon,
          label: formatRate(status.downloadRate),
          color: theme.downloading,
        ),
        SsChip(
          icon: ulIcon,
          label: formatRate(status.uploadRate),
          color: theme.seeding,
        ),
        SsChip(
          icon: peerIcon,
          label: '${status.numPeers} peers / ${status.numSeeds} seeds',
        ),
      ],
    );
  }
}
