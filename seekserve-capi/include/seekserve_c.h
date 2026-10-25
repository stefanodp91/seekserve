#ifndef SEEKSERVE_C_H
#define SEEKSERVE_C_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SeekServeEngine SeekServeEngine;

typedef int32_t ss_error_t;
#define SS_OK                    0
#define SS_ERR_INVALID_ARG      -1
#define SS_ERR_NOT_FOUND        -2
#define SS_ERR_METADATA_PENDING -3
#define SS_ERR_TIMEOUT          -4
#define SS_ERR_IO               -5
#define SS_ERR_ALREADY_RUNNING  -6
#define SS_ERR_CANCELLED        -7

typedef void (*ss_event_callback_t)(const char* event_json, void* user_data);

// config_json keys added for resume data (both optional; without them the
// engine behaves as before):
//   "resume_data" (bool): keep each torrent's resume data in the cache
//     database, without peer addresses, and restore torrents from it; a
//     restored torrent then has its metadata at once and comes back paused.
//   "resume_save_interval" (int, seconds): how often a torrent that changed
//     is saved again while the servers run; 0 or absent = only on events
//     (metadata, a completed file, a pause, ss_engine_destroy).
// config_json keys for the peers of a complete torrent (both optional;
// without them libtorrent's defaults hold):
//   "close_redundant_connections" (bool, default true): with every wanted
//     piece there, disconnect the seeds; false keeps them, so the next file
//     of a pack starts at once (app BUG-198).
//   "min_reconnect_time" (int, seconds, default 60): how long a peer that
//     was let go waits before it is taken back.
// Both are read after every other key: a value of the wrong type cannot
// cost the proxy or the resume settings.
// config_json keys for the stream servers (all optional, in seconds; read
// after the keys above, for the same reason; app BUG-88):
//   "read_timeout" (default 30): how long a client may take to send its
//     whole request; then the connection is closed. 0 = no limit.
//   "connection_timeout" (default 0 = no limit): how long a write to the
//     newest reader of a stream may wait for the client to read. With 0 a
//     paused player keeps its connection.
//   "stale_reader_timeout" (default 0 = no limit): how long a reader that a
//     newer request of the same stream left behind may wait, for its client
//     or for pieces, before it is closed. A TV that seeks leaves its old
//     connection open without reading it. Whatever this says, when every
//     slot is taken the oldest reader left behind makes room for a new
//     connection.
// config_json keys for the start of a stream with priority (both optional;
// read after every key above, for the same reason; app BUG-228, DEC-656):
//   "startup_focus_bytes" (int, default 0 = off): when a reader starts or
//     jumps far, the pieces of its first so many bytes go to the top
//     priority and every other wanted piece to the lowest (never to zero),
//     until those bytes are there. Through Tor this keeps the pieces a
//     player waits for from queuing behind the rest of the file.
//   "startup_focus_timeout" (int, seconds, default 30): the longest such a
//     start lasts; then the files' priorities hold again.
__attribute__((visibility("default")))
SeekServeEngine* ss_engine_create(const char* config_json);

__attribute__((visibility("default")))
void ss_engine_destroy(SeekServeEngine* engine);

__attribute__((visibility("default")))
ss_error_t ss_add_torrent(SeekServeEngine* engine,
                          const char* uri,
                          char* out_torrent_id,
                          int32_t out_torrent_id_len);

__attribute__((visibility("default")))
ss_error_t ss_remove_torrent(SeekServeEngine* engine,
                             const char* torrent_id,
                             bool delete_files);

__attribute__((visibility("default")))
ss_error_t ss_pause_torrent(SeekServeEngine* engine,
                            const char* torrent_id);

/* Resumes a torrent in the download queue. A torrent libtorrent put into
 * error ("torrent_error" in ss_get_status) is checked again first: the
 * check clears the error and reads the files again, and lets its peers go
 * (they come back after min_reconnect_time). */
__attribute__((visibility("default")))
ss_error_t ss_resume_torrent(SeekServeEngine* engine,
                             const char* torrent_id);

/* Resumes a torrent outside the download queue (max_concurrent_torrents),
 * as ss_add_torrent adds it: for streaming. ss_resume_torrent puts it back
 * into the queue. A torrent in error is checked again first, as there. */
__attribute__((visibility("default")))
ss_error_t ss_start_torrent(SeekServeEngine* engine,
                            const char* torrent_id);

__attribute__((visibility("default")))
ss_error_t ss_force_reannounce(SeekServeEngine* engine,
                               const char* torrent_id);

/* Checks the pieces of a torrent on disk again. libtorrent keeps the pieces
 * it had in memory: a file deleted while the torrent is in the engine stays
 * whole for it until this is called. The torrent leaves its peers while it
 * checks, then downloads what is missing. SS_ERR_NOT_FOUND for a torrent the
 * engine does not have. */
__attribute__((visibility("default")))
ss_error_t ss_recheck_torrent(SeekServeEngine* engine,
                              const char* torrent_id);

__attribute__((visibility("default")))
ss_error_t ss_list_torrents(SeekServeEngine* engine,
                            char** out_json);

__attribute__((visibility("default")))
ss_error_t ss_list_files(SeekServeEngine* engine,
                         const char* torrent_id,
                         char** out_json);

__attribute__((visibility("default")))
ss_error_t ss_select_file(SeekServeEngine* engine,
                          const char* torrent_id,
                          int32_t file_index);

/* The two files a torrent wants from now on: reader_file is the one a player
 * reads, with the stream and the deadlines, and background_file downloads
 * behind it. -1 for none; every other file is not downloaded. With both,
 * the reader's file comes first. A player keeps its stream while only
 * background_file changes. ss_select_file(fi) is a new reader alone.
 * SS_ERR_NOT_FOUND for a torrent or a file that is not there (nothing
 * changes), SS_ERR_METADATA_PENDING before the files are known. */
__attribute__((visibility("default")))
ss_error_t ss_set_wanted_files(SeekServeEngine* engine,
                               const char* torrent_id,
                               int32_t reader_file,
                               int32_t background_file);

__attribute__((visibility("default")))
ss_error_t ss_get_stream_url(SeekServeEngine* engine,
                             const char* torrent_id,
                             int32_t file_index,
                             char** out_url);

/* The torrent's status as JSON. "error" means the engine does not have the
 * torrent; "torrent_error" ({"message", "file"}) is libtorrent's own error,
 * which keeps the torrent paused until it is resumed or started again;
 * "file" is the file's index, or a negative value when no file is to blame. */
__attribute__((visibility("default")))
ss_error_t ss_get_status(SeekServeEngine* engine,
                         const char* torrent_id,
                         char** out_json);

__attribute__((visibility("default")))
ss_error_t ss_set_event_callback(SeekServeEngine* engine,
                                 ss_event_callback_t callback,
                                 void* user_data);

__attribute__((visibility("default")))
ss_error_t ss_start_server(SeekServeEngine* engine,
                           uint16_t port,
                           uint16_t* out_port);

__attribute__((visibility("default")))
ss_error_t ss_stop_server(SeekServeEngine* engine);

__attribute__((visibility("default")))
ss_error_t ss_set_proxy(SeekServeEngine* engine, const char* proxy_json);

/* Opens a share (app WORK-37): the file a player reads (selected with
 * ss_select_file or ss_set_wanted_files), served to one other device, a TV,
 * by a stream server of its own. It listens on bind_address only (the
 * device's address on that network: never 0.0.0.0 or ::) and serves
 * peer_address only: a connection from any other address is closed without
 * an answer. The URL has a random path and token of its own, not the
 * torrent's id (its info hash), nor the engine's token; the control server
 * stays on 127.0.0.1. The share follows its file when the file is selected
 * again, answers 404 once the file is no longer the reader's, and lasts
 * until ss_close_share or ss_stop_server.
 * out_json: {"share_id": "…", "url": "http://<bind_address>:<port>/…"},
 * freed with ss_free_string. SS_ERR_INVALID_ARG for a missing argument or an
 * address that is not one, or a wildcard; SS_ERR_NOT_FOUND for a torrent the
 * engine does not have or a file no player reads; SS_ERR_IO before
 * ss_start_server or when the address cannot be listened on. */
__attribute__((visibility("default")))
ss_error_t ss_open_share(SeekServeEngine* engine,
                         const char* torrent_id,
                         int32_t file_index,
                         const char* bind_address,
                         const char* peer_address,
                         char** out_json);

/* Opens a share of a whole file on disk that is no torrent's (app WORK-37,
 * DEC-627): a download already complete, served like ss_open_share's file,
 * to peer_address only, on bind_address only, with a random path and token.
 * It needs no torrent and no peer, so it works while the session cannot
 * reach the network. file_path must be an absolute path to a regular file
 * inside save_path, once links and ".." are resolved. out_json as for
 * ss_open_share. SS_ERR_INVALID_ARG for a missing argument, an address that
 * is not one or a wildcard, a relative path, a path outside save_path, a
 * folder; SS_ERR_NOT_FOUND for a path with nothing there; SS_ERR_IO before
 * ss_start_server. Closed with ss_close_share. */
__attribute__((visibility("default")))
ss_error_t ss_open_file_share(SeekServeEngine* engine,
                              const char* file_path,
                              const char* bind_address,
                              const char* peer_address,
                              char** out_json);

/* Closes a share and the connections it has; SS_ERR_NOT_FOUND for a share
 * that is not open. */
__attribute__((visibility("default")))
ss_error_t ss_close_share(SeekServeEngine* engine, const char* share_id);

__attribute__((visibility("default")))
void ss_free_string(char* str);

#ifdef __cplusplus
}
#endif

#endif /* SEEKSERVE_C_H */
