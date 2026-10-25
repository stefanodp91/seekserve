/// What a video player is doing, for an app's debug log (app BUG-238,
/// DEC-662): which decoder it uses, the format, how much it holds, whether
/// it waits for data or just stands, the frames it dropped. Null where the
/// player does not say.
class SsPlayerState {
  const SsPlayerState({
    this.decoder,
    this.format,
    this.cacheSeconds,
    this.pausedForCache,
    this.idle,
    this.drops,
  });

  /// The decoder in use (mpv's `hwdec-current`: `mediacodec`, `no`, …).
  final String? decoder;

  /// The video format (mpv's `video-format`).
  final String? format;

  /// Seconds of the video the player holds ahead (`demuxer-cache-duration`).
  final double? cacheSeconds;

  /// Whether it stands waiting for data (`paused-for-cache`).
  final bool? pausedForCache;

  /// Whether it does nothing at all (`core-idle`).
  final bool? idle;

  /// Frames the decoder dropped (`decoder-frame-drop-count`).
  final int? drops;
}

/// Reads one mpv property, as `NativePlayer.getProperty` does.
typedef SsGetPlayerProperty = Future<String> Function(String name);

/// Asks the player of an [SsVideoPlayer] its state: the player attaches it
/// while it has an engine, the app reads it when it wants.
class SsPlayerProbe {
  SsGetPlayerProperty? _get;

  /// The player's way to read a property; until [detach].
  void attach(SsGetPlayerProperty get) => _get = get;

  void detach() => _get = null;

  /// Null with no player attached.
  Future<SsPlayerState?> read() async {
    final get = _get;
    if (get == null) return null;
    Future<String?> property(String name) async {
      try {
        final value = await get(name);
        return value.isEmpty ? null : value;
      } catch (_) {
        // Not there in this player, or the player went: unknown.
        return null;
      }
    }

    bool? yes(String? value) => value == null ? null : value == 'yes';
    return SsPlayerState(
      decoder: await property('hwdec-current'),
      format: await property('video-format'),
      cacheSeconds: double.tryParse(
        await property('demuxer-cache-duration') ?? '',
      ),
      pausedForCache: yes(await property('paused-for-cache')),
      idle: yes(await property('core-idle')),
      drops: int.tryParse(await property('decoder-frame-drop-count') ?? ''),
    );
  }
}
