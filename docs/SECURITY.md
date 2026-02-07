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

- **Stream server**: `max_concurrent_streams` (default: 4) — excess connections rejected
- **Control API**: Hard limit of 20 concurrent connections
- Socket-level timeouts: `SO_RCVTIMEO` (30s read), `SO_SNDTIMEO` (60s send)

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
