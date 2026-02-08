import 'package:flutter/widgets.dart';

import 'screens/home_screen.dart';
import 'screens/player_screen.dart';
import 'screens/torrent_detail_screen.dart';

/// Route arguments for the torrent detail screen.
class TorrentDetailArgs {
  final String torrentId;
  const TorrentDetailArgs({required this.torrentId});
}

/// Route arguments for the player screen.
class PlayerArgs {
  final String streamUrl;
  final String torrentId;
  final String fileName;
  const PlayerArgs({
    required this.streamUrl,
    required this.torrentId,
    required this.fileName,
  });
}

class AppRouter {
  static Route<dynamic> generateRoute(RouteSettings settings) {
    switch (settings.name) {
      case '/':
        return PageRouteBuilder(
          settings: settings,
          pageBuilder: (ctx, a1, a2) => const HomeScreen(),
          transitionsBuilder: _fade,
        );

      case '/detail':
        final args = settings.arguments as TorrentDetailArgs;
        return PageRouteBuilder(
          settings: settings,
          pageBuilder: (ctx, a1, a2) =>
              TorrentDetailScreen(torrentId: args.torrentId),
          transitionsBuilder: _fade,
        );

      case '/player':
        final args = settings.arguments as PlayerArgs;
        return PageRouteBuilder(
          settings: settings,
          pageBuilder: (ctx, a1, a2) => AppPlayerScreen(
            streamUrl: args.streamUrl,
            torrentId: args.torrentId,
            fileName: args.fileName,
          ),
          transitionsBuilder: _fade,
        );

      default:
        return PageRouteBuilder(
          settings: settings,
          pageBuilder: (ctx, a1, a2) => const HomeScreen(),
          transitionsBuilder: _fade,
        );
    }
  }

  static Widget _fade(
    BuildContext ctx,
    Animation<double> animation,
    Animation<double> secondary,
    Widget child,
  ) {
    return FadeTransition(opacity: animation, child: child);
  }
}
