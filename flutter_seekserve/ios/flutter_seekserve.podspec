Pod::Spec.new do |s|
  s.name             = 'flutter_seekserve'
  s.version          = '0.1.0'
  s.summary          = 'Flutter FFI plugin for SeekServe torrent streaming SDK.'
  s.description      = <<-DESC
SeekServe provides torrent-based video streaming with HTTP Range support,
fast seek, and offline caching via a C API wrapped with dart:ffi.
                       DESC
  s.homepage         = 'https://github.com/mapo80/seekserve'
  s.license          = { :file => '../LICENSE' }
  s.author           = { 'SeekServe' => 'seekserve@example.com' }
  s.source           = { :path => '.' }

  s.platform         = :ios, '15.0'
  s.dependency       'Flutter'
  s.swift_version    = '5.0'

  # Pre-built static XCFramework (built by scripts/build-flutter-natives.sh)
  s.vendored_frameworks = 'Frameworks/seekserve.xcframework'
  s.static_framework = true

  # Link C++ standard library (libtorrent is C++)
  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386',
    'OTHER_LDFLAGS' => '-lc++ -lsqlite3',
  }
end
