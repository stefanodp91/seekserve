# HTTP Range Server Specification

## Endpoint

```
GET /stream/{torrentId}/{fileIndex}?token={authToken}
```

- `torrentId`: 40-character hex string (SHA-1 v1 infohash)
- `fileIndex`: integer file index within the torrent
- `authToken`: authentication token (required if configured)

## Supported Methods

| Method | Description |
|--------|-------------|
| GET    | Retrieve file content (full or partial) |
| HEAD   | Retrieve headers only (Content-Length, Accept-Ranges) |

Other methods return `405 Method Not Allowed`.

## Range Handling (RFC 7233)

### Request Headers

```
Range: bytes=start-end     → closed range
Range: bytes=start-        → from start to end of file
Range: bytes=-suffix       → last N bytes
```

Multi-range requests (`bytes=0-100,200-300`) are not supported and return `416`.

### Response Codes

| Code | Condition |
|------|-----------|
| 200 OK | No Range header; returns full file with `Accept-Ranges: bytes` |
| 206 Partial Content | Valid Range header; returns requested byte range |
| 403 Forbidden | Invalid or missing auth token |
| 404 Not Found | Unknown torrent/file or invalid URL path |
| 416 Range Not Satisfiable | Malformed range, inverted range, or start >= file_size |

### Response Headers (206)

```
Content-Type: video/mp4
Content-Length: <range_length>
Content-Range: bytes <start>-<end>/<total_file_size>
Accept-Ranges: bytes
```

Content-Length and Content-Range are relative to the **selected file**, not the entire torrent.

## Content Types

| Extension | MIME Type |
|-----------|-----------|
| .mp4 | video/mp4 |
| .mkv | video/x-matroska |
| .avi | video/x-msvideo |
| .webm | video/webm |
| .ogv | video/ogg |
| .mov | video/quicktime |
| .mp3 | audio/mpeg |
| .ogg | audio/ogg |
| .flac | audio/flac |
| other | application/octet-stream |

## Streaming Behavior

- Body is streamed in 64KB chunks directly from disk
- Headers are sent immediately via Beast; body via raw Asio writes
- ByteSource blocks if requested pieces are not yet downloaded
- Each connection's socket is non-blocking underneath, and every wait for the client is a
  `poll()` in 500 ms slices that can end (Asio's blocking calls ignore `SO_RCVTIMEO` and
  `SO_SNDTIMEO`: before, these limits never applied; app BUG-88):
  - Request: the whole request has `ServerConfig::read_timeout` (default 30 s; 0 = no
    limit) to arrive, then the connection is closed.
  - Newest reader of a stream: a write may wait `ServerConfig::connection_timeout` for the
    client to read (default 0 = as long as it stays connected: a paused player keeps its
    connection).
  - Reader left behind (a newer request of the same stream owns its position, as after a
    seek): it is closed once it has waited `ServerConfig::stale_reader_timeout`, for its
    client or for pieces (default 0 = no limit). A TV leaves its old connection open
    without reading it when it seeks.

## Connection Limits

- Max concurrent connections: `ServerConfig::max_concurrent_streams` (default: 4)
- A connection over the limit takes the place of the oldest reader left behind, which is
  closed; with none, it is closed at once (app BUG-88)
- Loopback binding only (127.0.0.1 by default), except for shares

## Shares

`open_share` (`ss_open_share`) serves the file a player reads to one other device of a
network, a TV (app WORK-37). Each share is a stream server of its own:

- it listens on the address it is given, never a wildcard (`0.0.0.0`, `::`);
- it serves one client address (`ServerConfig::allowed_peer`): any other connection is
  closed at once, without an answer, and takes no slot;
- its URL is `http://<address>:<port>/stream/<share id>/0?token=<token>`, where the share
  id and the token are 16 random bytes each, in hex, from the system's secure source: the
  torrent's id (its info hash) is not in it, and the engine's token does not open it;
- its reads move the file's scheduler like the engine's own stream;
- it follows its file when the file is selected again, answers 404 once the file is no
  longer the reader's, and lasts until `close_share` or `stop_server`.

The control API stays on 127.0.0.1.

## Authentication

Token is validated from `?token=` query parameter using constant-time comparison. Tokens are never logged.
