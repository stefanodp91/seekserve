# Security

## Loopback Binding

Both HTTP servers bind to `127.0.0.1` by default. This prevents remote access to the streaming and control endpoints.

## Token Authentication

### Stream Server
- Token passed as query parameter: `?token=<value>`
- Validated using constant-time comparison to prevent timing attacks
- Missing or invalid token returns `403 Forbidden`

### Control API
- Token accepted via `Authorization: Bearer <token>` header or `?token=` query param
- All endpoints require authentication (except CORS preflight OPTIONS)

### Token Generation
- 32-character alphanumeric tokens generated via `std::random_device` + `std::mt19937`
- Configurable via `auth_token` in engine config JSON

## Connection Limits

- **Stream server**: `max_concurrent_streams` (default: 4) — an excess connection takes the
  place of the oldest reader a newer request of its stream left behind, or is rejected
- **Control API**: Hard limit of 20 concurrent connections
- Stream server waits (app BUG-88): a request must arrive within `read_timeout` (30 s); a
  reader left behind is closed after `stale_reader_timeout`, the newest reader after
  `connection_timeout` (both 0 = no limit by default). `SO_RCVTIMEO` and `SO_SNDTIMEO`,
  set before, had no effect on Asio's blocking calls

## Shares (app WORK-37)

- A share is a stream server for one device of a network (a TV): it listens on the one
  address it is given (never `0.0.0.0` or `::`) and closes, without an answer, any
  connection that does not come from the one client address it is given
- Its URL carries neither the torrent's info hash nor the engine's token: a random share
  id and token (16 bytes each, `std::random_device`, a fresh value each time)
- A file share (`ss_open_file_share`) serves only a regular file inside the save path, links
  and `..` resolved first; anything else is refused. The file is held open from the check on
- The client address check is no authentication (an address can be forged on the same
  network): the token is. Over plain HTTP, whoever can read the network sees the URL and
  the video

## Request Size Limits

- Control API rejects request bodies > 1 MB (`413 Payload Too Large`)

## Logging Safety

- Auth tokens are never logged
- URLs containing `?token=` are sanitized in log output to `?token=***`
- Log levels configurable: debug, info, warn, error

## Memory Safety

- C API uses `new char[]` / `delete[]` for library-owned strings
- Callers must use `ss_free_string()` to deallocate
- Null checks on all C API entry points (return `SS_ERR_INVALID_ARG`)
- Buffer size validation in `ss_add_torrent()` output buffer

## Thread Safety

- Lock-free atomics for piece availability (monotonic false→true)
- Mutex-protected state maps with minimal critical sections
- `removed_ids_` set prevents use-after-free from late alert callbacks
- Alert dispatcher stopped before member destruction in engine destructor
