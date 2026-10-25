/// The piece a reader waits for, as the engine's status says it while a
/// reader waits for a download (app BUG-238, BUG-228, DEC-661): who has it,
/// who sends it, where its blocks are. For the app's debug log.
class WaitPiece {
  /// The piece.
  final int piece;

  /// Connected peers that have it, and of them those that send it (they do
  /// not choke us and libtorrent does not find them too slow).
  final int peers;
  final int peersOpen;

  /// Its blocks: there or being written, asked and not there yet, asked of
  /// nobody.
  final int blocks;
  final int done;
  final int asked;
  final int free;

  /// The peers its asked blocks wait on, and how fast the slowest and the
  /// fastest of them send, in kB/s.
  final int askers;
  final int slowestKbs;
  final int fastestKbs;

  /// Since when no block of it came.
  final int stillMs;

  const WaitPiece({
    required this.piece,
    required this.peers,
    required this.peersOpen,
    required this.blocks,
    required this.done,
    required this.asked,
    required this.free,
    required this.askers,
    required this.slowestKbs,
    required this.fastestKbs,
    required this.stillMs,
  });

  static const _piece = 'wait_piece';

  /// Null when no reader waits for a download, or from an older library.
  static WaitPiece? fromJson(Map<String, dynamic> json) {
    if (!json.containsKey(_piece)) return null;
    int number(String key) => (json[key] as num?)?.toInt() ?? 0;
    return WaitPiece(
      piece: number(_piece),
      peers: number('wait_piece_peers'),
      peersOpen: number('wait_piece_peers_open'),
      blocks: number('wait_piece_blocks'),
      done: number('wait_piece_done'),
      asked: number('wait_piece_asked'),
      free: number('wait_piece_free'),
      askers: number('wait_piece_askers'),
      slowestKbs: number('wait_piece_slowest_kbs'),
      fastestKbs: number('wait_piece_fastest_kbs'),
      stillMs: number('wait_piece_still_ms'),
    );
  }

  Map<String, dynamic> toJson() => {
    _piece: piece,
    'wait_piece_peers': peers,
    'wait_piece_peers_open': peersOpen,
    'wait_piece_blocks': blocks,
    'wait_piece_done': done,
    'wait_piece_asked': asked,
    'wait_piece_free': free,
    'wait_piece_askers': askers,
    'wait_piece_slowest_kbs': slowestKbs,
    'wait_piece_fastest_kbs': fastestKbs,
    'wait_piece_still_ms': stillMs,
  };
}
