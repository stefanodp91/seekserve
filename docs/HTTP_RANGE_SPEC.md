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
- Read timeout: 30 seconds (configurable via `ServerConfig::read_timeout`)
- Send timeout: 60 seconds (configurable via `ServerConfig::connection_timeout`)

## Connection Limits

- Max concurrent connections: `ServerConfig::max_concurrent_streams` (default: 4)
- Connections exceeding the limit are immediately closed
- Loopback binding only (127.0.0.1 by default)

## Authentication

Token is validated from `?token=` query parameter using constant-time comparison. Tokens are never logged.
