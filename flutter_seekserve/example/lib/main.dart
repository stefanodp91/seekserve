import 'dart:io';

import 'package:flutter/material.dart';
import 'package:media_kit/media_kit.dart';
import 'package:path_provider/path_provider.dart';
import 'package:provider/provider.dart';

import 'providers/seekserve_provider.dart';
import 'screens/home_screen.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  MediaKit.ensureInitialized();
  runApp(const SeekServeDemoApp());
}

class SeekServeDemoApp extends StatelessWidget {
  const SeekServeDemoApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => SeekServeProvider(),
      child: MaterialApp(
        title: 'SeekServe Demo',
        theme: ThemeData(
          colorSchemeSeed: Colors.deepPurple,
          useMaterial3: true,
          brightness: Brightness.dark,
        ),
        home: const _InitWrapper(),
      ),
    );
  }
}

/// Initialises the engine with a platform-specific save path before
/// showing the home screen.
class _InitWrapper extends StatefulWidget {
  const _InitWrapper();

  @override
  State<_InitWrapper> createState() => _InitWrapperState();
}

class _InitWrapperState extends State<_InitWrapper> {
  bool _initialised = false;

  @override
  void initState() {
    super.initState();
    _initEngine();
  }

  Future<void> _initEngine() async {
    final dir = await getApplicationDocumentsDirectory();
    final savePath = '${dir.path}${Platform.pathSeparator}seekserve';
    await Directory(savePath).create(recursive: true);

    if (!mounted) return;
    final provider = context.read<SeekServeProvider>();
    provider.init(savePath);
    debugPrint('ENGINE: init done, serverPort=${provider.serverPort}, '
        'isReady=${provider.isReady}, error=${provider.errorMessage}');

    // Auto-add Sintel torrent for development (seeded locally by demo CLI).
    provider.addTorrent(
      'magnet:?xt=urn:btih:e4d37e62d14ba96d29b9e760148803b458aee5b6'
      '&dn=Sintel'
      '&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce'
      '&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce'
      '&tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A6969%2Fannounce'
      '&tr=udp%3A%2F%2Fexodus.desync.com%3A6969%2Fannounce',
    );

    setState(() => _initialised = true);
  }

  @override
  Widget build(BuildContext context) {
    if (!_initialised) {
      return const Scaffold(
        body: Center(child: CircularProgressIndicator()),
      );
    }
    return const HomeScreen();
  }
}
