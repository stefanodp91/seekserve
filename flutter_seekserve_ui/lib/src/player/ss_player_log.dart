/// The video player's messages for an app's debug log (app BUG-238, BUG-239,
/// DEC-662): no address of a stream, of a file on the device or of a peer
/// leaves the player, and at most so many lines are written.
library;

final _url = RegExp(r'\b[a-zA-Z][a-zA-Z0-9+.-]*://\S+');
// A path runs to the end of the line: a file's name may hold spaces, and
// mpv does not say where it ends (app BUG-271, DEC-746).
final _file = RegExp(r'(?<![\w/])/(?:data|storage|sdcard|mnt)/[^\n]*');
final _address = RegExp(r'\b\d{1,3}(?:\.\d{1,3}){3}(?::\d+)?\b');

/// [text] with every address in it replaced: a URL (the stream's carries a
/// token) by `<url>`, a path on the device by `<file>` together with the rest
/// of its line, a network address by `<address>`.
String ssRedactPlayerLog(String text) => text
    .replaceAll(_url, '<url>')
    .replaceAll(_file, '<file>')
    .replaceAll(_address, '<address>');

/// Writes at most [max] lines, then one that says the rest is left out, then
/// nothing: a player that repeats itself does not fill the log.
class SsCappedLog {
  SsCappedLog({required this.max, required void Function(String line) write})
    : _write = write;

  /// The most lines written.
  final int max;
  final void Function(String line) _write;
  int _written = 0;
  bool _full = false;

  void add(String line) {
    if (_written < max) {
      _written++;
      _write(line);
      return;
    }
    if (_full) return;
    _full = true;
    _write('more lines left out (at most $max)');
  }
}
