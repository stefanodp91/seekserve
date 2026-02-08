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
    context.read<SeekServeProvider>().init(savePath);
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
