/// What the reads of a stream did, as the engine's status says it (app
/// BUG-238, DEC-660): libtorrent's answers to the piece reads, what the
/// stream's readers read and wait for, the player's requests and the alerts
/// libtorrent lost. For the app's debug log.
class StreamReads {
  /// read_piece requests, the repeated ones included.
  final int asks;

  /// Requests for a piece still unanswered.
  final int repeats;

  /// Answers with the piece's bytes.
  final int answers;

  /// Answers with an error, and libtorrent's text for the last one.
  final int errors;
  final String lastError;

  /// Answers that came with nobody waiting.
  final int late;

  /// Pieces asked and not answered now, and since when the oldest waits.
  final int waiting;
  final int oldestWaitMs;

  /// Reads from the file, and reads whose wait ended without the bytes.
  final int diskReads;
  final int timeouts;

  /// Readers waiting for a piece to download, and for libtorrent's answer.
  final int waitingDownload;
  final int waitingAnswer;

  /// Requests of the player that reached the engine for this file.
  final int requests;

  /// Alerts libtorrent lost to a full queue, in the whole session.
  final int alertsDropped;

  const StreamReads({
    required this.asks,
    required this.repeats,
    required this.answers,
    required this.errors,
    required this.lastError,
    required this.late,
    required this.waiting,
    required this.oldestWaitMs,
    required this.diskReads,
    required this.timeouts,
    required this.waitingDownload,
    required this.waitingAnswer,
    required this.requests,
    required this.alertsDropped,
  });

  static const _asks = 'piece_asks';

  /// Null when the engine does not say: an older library, or no reader.
  static StreamReads? fromJson(Map<String, dynamic> json) {
    if (!json.containsKey(_asks)) return null;
    int number(String key) => (json[key] as num?)?.toInt() ?? 0;
    return StreamReads(
      asks: number(_asks),
      repeats: number('piece_repeats'),
      answers: number('piece_answers'),
      errors: number('piece_errors'),
      lastError: json['piece_last_error'] as String? ?? '',
      late: number('piece_late'),
      waiting: number('piece_waiting'),
      oldestWaitMs: number('piece_oldest_wait_ms'),
      diskReads: number('disk_reads'),
      timeouts: number('read_timeouts'),
      waitingDownload: number('readers_waiting_download'),
      waitingAnswer: number('readers_waiting_answer'),
      requests: number('range_requests'),
      alertsDropped: number('alerts_dropped'),
    );
  }

  Map<String, dynamic> toJson() => {
    _asks: asks,
    'piece_repeats': repeats,
    'piece_answers': answers,
    'piece_errors': errors,
    'piece_last_error': lastError,
    'piece_late': late,
    'piece_waiting': waiting,
    'piece_oldest_wait_ms': oldestWaitMs,
    'disk_reads': diskReads,
    'read_timeouts': timeouts,
    'readers_waiting_download': waitingDownload,
    'readers_waiting_answer': waitingAnswer,
    'range_requests': requests,
    'alerts_dropped': alertsDropped,
  };
}
