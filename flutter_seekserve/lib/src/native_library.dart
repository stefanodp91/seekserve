import 'dart:ffi';
import 'dart:io';

import 'bindings_generated.dart';

const String _libName = 'seekserve';

/// Loads the platform-specific native SeekServe library.
DynamicLibrary _openLibrary() {
  if (Platform.isIOS || Platform.isMacOS) {
    // Statically linked via framework — symbols in process.
    return DynamicLibrary.process();
  }
  if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('lib$_libName.so');
  }
  throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
}

/// Singleton bindings instance for the SeekServe native library.
final SeekServeBindings nativeBindings = SeekServeBindings(_openLibrary());
