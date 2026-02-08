# SeekServe — Progress Tracker

> Test torrent: `fixtures/Sintel_archive.torrent` (12 files, 4 MP4s, infohash `e4d37e62...`)
> Test target: index 8 = `sintel-2048-stereo_512kb.mp4` (73.8 MB)

---

## Fase 1: C++ SDK

### M1: Project Skeleton + libtorrent Integration

#### Setup repository
- [x] Creare struttura directory (`seekserve-core/`, `seekserve-serve/`, `seekserve-capi/`, `seekserve-demo/`, `tests/`, `docs/`, `extern/`, `fixtures/`)
- [x] Root `CMakeLists.txt` con opzioni (`SEEKSERVE_ENABLE_WEBTORRENT`, `BUILD_TESTS`, `BUILD_DEMO`, `BUILD_CAPI`)
- [x] `CMakePresets.json` (debug, release)
- [x] `vcpkg.json` manifest con tutte le dipendenze Boost + nlohmann_json + spdlog + sqlite3 + gtest
- [x] `.gitignore`
- [x] `setup.sh` — script automatico per bootstrap completo (vcpkg, submodules, cmake, build)

#### Dipendenze
- [x] Aggiungere libtorrent come git submodule da `master` branch
- [x] Init submodules ricorsivi (deps/try_signal, deps/asio-gnutls)
- [x] Abilitare WebTorrent (M13: switched to master, recursive submodules init, WEBTORRENT=ON)
- [x] vcpkg: installare boost (asio, beast, config, crc, date-time, functional, intrusive, logic, multi-index, multiprecision, optional, pool, predef, range, smart-ptr, system, utility, variant)
- [x] vcpkg: installare nlohmann-json, spdlog, sqlite3, gtest

#### Moduli core (headers + implementazione)
- [x] `seekserve-core/include/seekserve/types.hpp` — TorrentId, FileIndex, PieceSpan, ByteRange, FileInfo, StreamMode, StreamStatus
- [x] `seekserve-core/include/seekserve/error.hpp` — errc enum, seekserve_category, Result<T>
- [x] `seekserve-core/include/seekserve/config.hpp` — SessionConfig, SchedulerConfig, ServerConfig, CacheConfig
- [x] `seekserve-core/src/error.cpp` — implementazione completa
- [x] `seekserve-core/include/seekserve/alert_dispatcher.hpp` — header con template `on<AlertType>`
- [x] `seekserve-core/src/alert_dispatcher.cpp` — implementazione completa (thread dedicato, pop_alerts loop, handler registry)
- [x] `seekserve-core/include/seekserve/session_manager.hpp` — header con AddTorrentParams
- [x] `seekserve-core/src/session_manager.cpp` — implementazione completa (add/remove torrent, magnet + .torrent, settings streaming-optimized)

#### Moduli M2+ (headers + stub)
- [x] `seekserve-core/include/seekserve/metadata_catalog.hpp`
- [x] `seekserve-core/src/metadata_catalog.cpp` — **implementazione completa** (multi-file enumeration, file selection con priorità)
- [x] `seekserve-core/include/seekserve/byte_range_mapper.hpp`
- [x] `seekserve-core/src/byte_range_mapper.cpp` — **implementazione completa** (map via file_storage::map_file)
- [x] `seekserve-core/include/seekserve/piece_availability.hpp`
- [x] `seekserve-core/src/piece_availability.cpp` — **implementazione completa** (atomic bitfield, contiguous calc)
- [x] `seekserve-core/include/seekserve/streaming_scheduler.hpp`
- [x] `seekserve-core/src/streaming_scheduler.cpp` — stub (TODO M5)
- [x] `seekserve-core/include/seekserve/byte_source.hpp`
- [x] `seekserve-core/src/byte_source.cpp` — **implementazione completa** (cv wait, disk read, cancel)
- [x] `seekserve-core/include/seekserve/offline_cache.hpp`
- [x] `seekserve-core/src/offline_cache.cpp` — parziale (init_db completo, metodi CRUD sono stub TODO M6)

#### Moduli serve (headers + stub)
- [x] `seekserve-serve/include/seekserve/http_range_server.hpp`
- [x] `seekserve-serve/src/http_range_server.cpp` — stub (TODO M4)
- [x] `seekserve-serve/include/seekserve/control_api_server.hpp`
- [x] `seekserve-serve/src/control_api_server.cpp` — stub (TODO M6)
- [x] `seekserve-serve/src/range_parser.cpp` — stub (TODO M4)
- [x] `seekserve-serve/src/token_auth.cpp` — **implementazione completa** (generate + constant-time validate)

#### C API (header + stub)
- [x] `seekserve-capi/include/seekserve_c.h` — header completo con tutte le funzioni extern "C"
- [x] `seekserve-capi/src/seekserve_c.cpp` — stub (TODO M7)

#### Demo CLI
- [x] `seekserve-demo/src/main.cpp` — **implementazione completa** (arg parsing, add torrent, metadata wait, file listing, file selection, download progress)

#### Test placeholder
- [x] `tests/unit/test_byte_range_mapper.cpp` — placeholder
- [x] `tests/unit/test_piece_availability.cpp` — placeholder
- [x] `tests/unit/test_streaming_scheduler.cpp` — placeholder
- [x] `tests/unit/test_range_parser.cpp` — placeholder
- [x] `tests/unit/test_offline_cache.cpp` — placeholder
- [x] `tests/integration/test_session_lifecycle.cpp` — placeholder
- [x] `tests/integration/test_http_range_server.cpp` — placeholder
- [x] `tests/integration/test_end_to_end_stream.cpp` — placeholder

#### Verifica M1
- [x] Build completa su macOS (libtorrent + seekserve-core + seekserve-serve + seekserve-capi + demo + tests)
- [x] Unit tests: 5/5 pass (placeholder)
- [x] Demo: carica `Sintel_archive.torrent`, elenca 12 file, identifica 4 MP4
- [x] Scaricato `fixtures/Sintel_archive.torrent` per test

---

### M2: ByteRangeMapper + PieceAvailability + Unit Tests (multi-file)

#### ByteRangeMapper — unit tests reali
- [x] Test: mapping primo byte del file (offset 0)
- [x] Test: mapping ultimo byte del file
- [x] Test: mapping range che attraversa confine tra pezzi (cross-piece boundary)
- [x] Test: mapping range di un singolo byte
- [x] Test: mapping intero file
- [x] Test: mapping con file offset in torrent multi-file (file non inizia al pezzo 0)
- [x] Test: verifica first_piece() e end_piece() per file nel mezzo del torrent
- [x] Test: verifica piece_length() e file_size()

#### PieceAvailabilityIndex — unit tests reali
- [x] Test: mark_complete su singolo pezzo, verifica is_complete
- [x] Test: mark_complete doppio sullo stesso pezzo (idempotente)
- [x] Test: is_span_complete con tutti i pezzi disponibili
- [x] Test: is_span_complete con un pezzo mancante nel mezzo
- [x] Test: contiguous_from con serie contigua
- [x] Test: contiguous_from con gap
- [x] Test: contiguous_bytes_from con offset intra-piece
- [x] Test: progress() — 0%, 50%, 100%
- [x] Test: reset() e re-inizializzazione

#### MetadataCatalog — verifica multi-file
- [x] Test: on_metadata_received registra tutti i file con path, size, offset, piece range corretti
- [x] Test: select_file imposta priorità corrette (file selezionato = default_priority, altri = dont_download)
- [x] Test: selected_file() ritorna il file selezionato
- [x] Test: has_metadata() prima e dopo metadata_received

#### Wire alert → PieceAvailabilityIndex
- [x] Registrare handler per `piece_finished_alert` nell'AlertDispatcher
- [x] PieceAvailabilityIndex::mark_complete chiamato per ogni pezzo completato
- [x] Demo aggiornato: mostra piece completion progress per il file selezionato

#### Integration test con Sintel torrent
- [x] test_multifile_metadata: carica .torrent, verifica ≥10 file, filtra .mp4
- [x] test_file_selection: seleziona 480p mp4 (index 8), verifica priorità
- [x] test_range_mapping_multifile: verifica mapping Range→pezzi per il file selezionato con offset corretto

#### Verifica M2
- [x] Tutti gli unit test ByteRangeMapper passano (19 tests: 14 synthetic + 5 Sintel)
- [x] Tutti gli unit test PieceAvailability passano (20 tests)
- [x] Tutti gli unit test MetadataCatalog passano (15 tests)
- [x] Integration test multi-file passa (6 tests con sessione reale)
- [x] Demo seleziona un .mp4 e mostra progresso download per quel file

---

### M3: ByteSource

#### Implementazione
- [x] Verifica che read_from_disk legga correttamente dal path del file (save_path + file path dal torrent)
- [x] Test: read() su range disponibile → ritorna bytes corretti
- [x] Test: read() su range non disponibile → blocca fino a timeout
- [x] Test: read() su range non disponibile → si sblocca quando pezzo arriva (notify_piece_complete)
- [x] Test: cancel() → read() in attesa ritorna errc::cancelled
- [x] Test: is_available() su range completo e incompleto
- [x] Test: file_size() ritorna la dimensione corretta del file selezionato

#### Integration test
- [x] ByteSource::read() per bytes disponibili → verifica dati corretti (via unit test con file reale su disco)
- [x] ByteSource::read() per bytes non ancora disponibili → verifica blocking + eventual success (via cv notify)
- [x] ByteSource::cancel() → verifica che read bloccato ritorna errore

#### Verifica M3
- [x] Unit test ByteSource passano (15 tests: disk I/O, cv wait/notify, cancel, timeout, edge cases)
- [x] Suite completa: 72 unit tests passati

---

### M4: HTTP Range Server (multi-file aware)

#### Range parser (RFC 7233)
- [x] Implementare `parse_range_header()` completo — header `range_parser.hpp` + implementazione in `range_parser.cpp`
- [x] Test: `bytes=0-499` → {0, 499}
- [x] Test: `bytes=500-` → {500, file_size-1}
- [x] Test: `bytes=-500` → {file_size-500, file_size-1}
- [x] Test: `bytes=0-0` → {0, 0} (singolo byte)
- [x] Test: header malformato → nullopt (no prefix, wrong prefix, empty, bytes-only, no-dash)
- [x] Test: range fuori limiti → nullopt (start beyond file, end clamped)
- [x] Test: range invertito (start > end) → nullopt
- [x] Test: multi-range (non supportato) → nullopt
- [x] Test: suffix-zero, negative suffix, non-numeric, dash-only, empty file size
- [x] Test: whitespace tolerance attorno al range spec

#### HTTP server con Beast
- [x] Implementare `HttpRangeServer::start()` con tcp::acceptor su loopback
- [x] Implementare accept loop asincrono (`async_accept` + `make_strand`)
- [x] Implementare routing: `GET /stream/{torrentId}/{fileIndex}` (regex match)
- [x] Implementare risposta 200 OK (no Range header) con Accept-Ranges: bytes
- [x] Implementare risposta 206 Partial Content con Content-Range header
- [x] Implementare risposta 416 Range Not Satisfiable con `bytes */file_size`
- [x] Implementare streaming write loop (64KB chunks da ByteSource) — streaming con `write_header` + `net::write` chunks (no buffering)
- [x] MIME type detection (.mp4, .mkv, .avi, .webm, .ogv, .mov, .mp3, .ogg, .flac) — da file_path passato a `set_byte_source`
- [x] Token auth: validare `?token=` query param (constant-time comparison)
- [x] Keep-alive HTTP/1.1
- [x] Thread-per-connection per streaming concorrente (VLC seek richiede connessioni simultanee)
- [ ] Connection timeout e limiti connessioni simultanee (M8 hardening)

#### URL routing multi-file
- [x] URL formato: `/stream/{torrentId}/{fileIndex}?token=XXX`
- [x] Content-Length basato su file_size del singolo file (non del torrent)
- [x] Content-Range basato su offset nel file (non nel torrent)

#### HEAD support
- [x] HEAD senza Range → 200 OK con Content-Length e Accept-Ranges
- [x] HEAD con Range → 206 con Content-Range e Content-Length corretti
- [x] `response_parser::skip(true)` per parsing corretto HEAD lato client

#### Multi-source support
- [x] Refactored da singolo `source_` a `unordered_map<string, SourceEntry>` per servire più torrent/file contemporaneamente

#### Integration test
- [x] Avviare server, richiedere HEAD → verifica Accept-Ranges: bytes e Content-Length
- [x] Richiedere range valido → verifica 206 + Content-Range corretto + dati corretti
- [x] Richiedere range fuori limiti → verifica 416
- [x] Richiedere senza Range → verifica 200 con Accept-Ranges e body completo
- [x] Richiedere con token invalido → verifica 403
- [x] Richiedere con token mancante → verifica 403
- [x] Stream non trovato → verifica 404
- [x] Path invalido → verifica 404
- [x] HEAD con Range → verifica 206 + Content-Range + Content-Length
- [x] stream_url() formato corretto

#### Verifica M4
- [x] Unit test range parser passano (26 tests)
- [x] Integration test HTTP server passano (13 tests)
- [x] Suite completa: 97 unit tests + 20 integration tests = 117 tests tutti verdi
- [x] VLC apre `http://127.0.0.1:PORT/stream/HASH/8?token=XYZ` e riproduce video (test manuale) — testato con `--local-file` mode su sintel-2048-surround_512kb.mp4
- [x] Seek in VLC funziona (nuove Range request servite correttamente) (test manuale) — VLC fa range requests concorrenti (bytes=0-, bytes=505731-), server gestisce con thread-per-connection

---

### M5: Streaming Scheduler

#### Hot window + Lookahead
- [x] Implementare `on_range_request()`: calcola PieceSpan, setta playhead
- [x] Hot window: set_piece_deadline con deadlines brevi (100ms + 50ms/piece) per pezzi nel range corrente + margine
- [x] Lookahead: set_piece_deadline con deadlines lunghe (2000ms + 200ms/piece) per pezzi oltre hot window
- [x] Rispettare deadline_budget (max 30 pezzi con deadline attivi, configurabile)
- [x] Skip pezzi già completi nel deadline loop

#### Seek boost
- [x] Rilevare seek: playhead salta di più di hot_window_pieces (forward o backward)
- [x] Espandere temporaneamente hot window (seek_boost_pieces extra)
- [x] Durata limitata (seek_boost_duration_ms, default 3s)
- [x] clear_piece_deadlines() prima di settare nuove deadline su seek

#### Mode switching
- [x] Implementare `tick()`: valuta metriche ogni 1s
- [x] Criterio streaming-first → download-assist: stall_count >= threshold (default 3)
- [x] Criterio download-assist → download-first: sustained_rate < min_sustained_rate AND stall_count >= 2× threshold
- [x] Criterio download-assist → streaming-first: contiguous bytes >= min_contiguous_bytes AND stall_count == 0
- [x] Criterio download-first → download-assist: contiguous bytes >= min_contiguous_bytes
- [x] `evaluate_mode_switch()` completo

#### Wire al server HTTP
- [x] HttpRangeServer::set_range_callback() — callback invocata su ogni Range request
- [x] HttpRangeServer notifica StreamingScheduler su ogni nuova Range request (full GET + 206)
- [x] Scheduler chiama set_piece_deadline() sul torrent_handle
- [x] tick() chiamata nel loop status della demo (ogni 1s)
- [x] piece_finished_alert → scheduler.on_piece_complete()
- [x] Status line mostra mode (STREAM/ASSIST/DLOAD), playhead piece, active deadlines

#### Unit tests (21 tests con sessione libtorrent reale)
- [x] Test initial state: mode=StreamingFirst, playhead=first_piece, no boost, stall=0
- [x] Test playhead tracking: on_range_request aggiorna playhead correttamente
- [x] Test playhead updates on subsequent requests
- [x] Test small move no seek boost (< hot_window)
- [x] Test large forward jump triggers seek boost (> hot_window)
- [x] Test backward seek triggers boost
- [x] Test seek boost expires (with short duration)
- [x] Test on_range_request sets deadlines
- [x] Test deadline budget respected
- [x] Test no deadlines when all pieces complete
- [x] Test on_piece_complete decrements active deadlines
- [x] Test on_piece_complete before playhead no effect
- [x] Test deadlines near end of file (don't exceed remaining pieces)
- [x] Test stall increases when playhead incomplete
- [x] Test stall resets when playhead complete
- [x] Test StreamingFirst → DownloadAssist
- [x] Test DownloadAssist → DownloadFirst
- [x] Test DownloadAssist → StreamingFirst (buffer healthy)
- [x] Test DownloadFirst → DownloadAssist (contiguous threshold met)
- [x] Test multiple seeks reset boost
- [x] Test mode switch resets stall count

#### Verifica M5
- [x] Unit test scheduler passano — 21 tests (tutti verdi)
- [x] Suite completa: 117 unit tests tutti verdi
- [x] Mode transitions verificate con torrent reale (Sintel) — STREAM→ASSIST→DLOAD con 0 peers
- [x] VLC test: server funziona con scheduler wired (local-file mode, VLC connette e riproduce)

---

### M6: Control API + Offline Cache

#### Control API REST (JSON)
- [x] Implementare `ControlApiServer::start()` con Beast (thread-per-connection, same pattern as HttpRangeServer)
- [x] `POST /api/torrents` — aggiungere torrent (magnet URI o .torrent path, JSON body)
- [x] `GET /api/torrents` — lista torrent attivi (con nome, progresso, download rate, peer count)
- [x] `GET /api/torrents/{id}/files` — lista file nel torrent (con selected_file)
- [x] `POST /api/torrents/{id}/files/{fileId}/select` — selezionare file per streaming
- [x] `GET /api/torrents/{id}/status` — stato torrent + metriche (rates, peers, seeds, state, offline_ready)
- [x] `GET /api/torrents/{id}/stream-url` — URL stream (con auth token)
- [x] `DELETE /api/torrents/{id}` — rimuovere torrent
- [x] `POST /api/server/stop` — shutdown graceful (via ShutdownCallback)
- [x] `GET /api/cache` — lista cache entries (tutti i file registrati, con progress e offline_ready)
- [x] Token auth: `Authorization: Bearer <token>` header o `?token=` query param
- [x] Risposte JSON (nlohmann_json) con `Access-Control-Allow-Origin: *` (CORS)
- [x] CORS preflight (OPTIONS → 204 con allow headers)
- [x] Gestione errori (404 Not Found, 400 Bad Request, 403 Forbidden)

#### OfflineCacheManager — SQLite CRUD
- [x] `on_torrent_added()` — INSERT OR IGNORE entry per ogni file (idempotente)
- [x] `on_progress_update()` — UPDATE progress + last_access
- [x] `on_file_completed()` — UPDATE offline_ready = 1, progress = 1.0
- [x] `on_access()` — UPDATE last_access timestamp
- [x] `list_cached()` — SELECT * ORDER BY last_access DESC
- [x] `is_offline_ready()` — SELECT offline_ready WHERE torrent_id AND file_index
- [x] `enforce_quota()` — DELETE con LRU (ORDER BY last_access ASC) se quota superata
- [x] Wire `file_completed_alert` → on_file_completed (nella demo)
- [x] Wire cache progress update nel loop status (ogni 1s)

#### Observability metrics (via `/api/torrents/{id}/status`)
- [x] Download rate (bytes/sec)
- [x] Upload rate
- [x] Peer count + seed count
- [x] Total download / upload
- [x] State (torrent state enum)
- [x] Offline ready (per-file)
- [x] Buffer health e seek latency — nel display console della demo (contiguous bytes, mode, deadlines)

#### Unit tests (13 tests OfflineCacheManager)
- [x] InitCreatesDatabase — verifica file .db creato
- [x] OnTorrentAddedInsertsEntries — 3 file inseriti, progress=0, offline_ready=false
- [x] OnTorrentAddedIdempotent — doppio insert non duplica entries
- [x] OnProgressUpdate — aggiorna solo il file specificato
- [x] OnFileCompleted — marca offline_ready, progress=1.0
- [x] IsOfflineReadyNonexistent — ritorna false per entry inesistente
- [x] OnAccessUpdatesTimestamp — file più recente appare primo nel list_cached
- [x] ListCachedReturnsAllFields — tutti i campi presenti e validi
- [x] MultipleTorrents — cache gestisce torrent diversi indipendentemente
- [x] EnforceQuotaUnlimited — quota=0 non evict nulla
- [x] EnforceQuotaEvictsLRU — evicts oldest first (file 300MB evicted, 77MB survives)
- [x] PersistenceAcrossInstances — dati persistono dopo chiudere e riaprire db
- [x] SintelTorrentFiles — 12 file Sintel, complete/progress/offline_ready corretti

#### Integration tests (10 tests Control API con Sintel torrent)
- [x] ListTorrents — GET /api/torrents ritorna Sintel con metadata
- [x] GetTorrentFiles — GET /api/torrents/{id}/files ritorna 12 file, file 8 presente
- [x] SelectFileAndGetStreamUrl — POST select + GET stream-url corretto
- [x] GetTorrentStatus — GET status con tutti i campi (rates, peers, state)
- [x] AuthRequired — request senza token → 403
- [x] TorrentNotFound — id inesistente → 404
- [x] NotFoundEndpoint — path invalido → 404
- [x] GetCacheEntries — GET /api/cache → array (inizialmente vuoto)
- [x] StreamUrlRequiresFileSelection — no file selected → 400
- [x] SelectInvalidFile — file index 999 → 400

#### curl test con demo
- [x] GET /api/torrents — lista Sintel torrent con metadata, progresso, peers
- [x] GET /api/torrents/{id}/files — 12 file con selected_file=8
- [x] GET /api/torrents/{id}/status — state, rates, offline_ready
- [x] GET /api/torrents/{id}/stream-url — URL corretta con token
- [x] GET /api/cache — 12 entries con file 8 last_access più recente
- [x] Auth failure → {"error": "Invalid or missing auth token"}
- [x] Not found torrent → {"error": "Torrent not found"}
- [x] Not found endpoint → {"error": "Not found"}

#### Verifica M6
- [x] API endpoints rispondono correttamente (tutti testati con curl)
- [x] SQLite cache persiste tra restart (test PersistenceAcrossInstances)
- [x] 167 tests totali (138 unit + 29 integration) — tutti verdi
- [x] Demo wired: ControlApiServer + OfflineCacheManager + file_completed_alert + progress updates

---

### M7: C API Layer

#### SeekServeEngine facade (`seekserve-serve/include/seekserve/engine.hpp`)
- [x] Creare classe `SeekServeEngine` che aggrega tutti i moduli
- [x] Costruttore: crea SessionManager, MetadataCatalog, OfflineCacheManager, wire alerts
- [x] Metodi: add_torrent, remove_torrent, list_files, select_file, get_stream_url, get_status_json, start_server, stop_server, set_event_callback
- [x] Per-torrent state: PieceAvailabilityIndex, ByteRangeMapper, ByteSource, StreamingScheduler — creati on select_file()
- [x] Alert multiplexing: piece_finished → avail+source+scheduler, file_completed → cache, metadata → catalog+cache
- [x] 1s tick timer on io_context: calls scheduler.tick() + updates cache progress for all active torrents
- [x] Safe destruction order: stop alert dispatcher FIRST, then destroy states, cache, catalog, sessions

#### Implementazione seekserve_c.cpp
- [x] `ss_engine_create()` — parse config JSON (save_path, auth_token, ports, trackers, log_level), crea SeekServeEngine
- [x] `ss_engine_destroy()` — delete engine (triggers safe destructor chain)
- [x] `ss_add_torrent()` — delega a engine, copia torrent_id in out buffer (truncates if small)
- [x] `ss_remove_torrent()` — delega a engine, clears torrent state
- [x] `ss_list_files()` — serializza file list come JSON {files:[...]}, alloca stringa
- [x] `ss_select_file()` — delega a engine (creates per-torrent state, wires byte source + scheduler)
- [x] `ss_get_stream_url()` — genera URL, alloca stringa
- [x] `ss_get_status()` — serializza status come JSON (torrent info + scheduler state), alloca stringa
- [x] `ss_set_event_callback()` — registra callback per eventi asincroni (metadata_received, file_completed, error)
- [x] `ss_start_server()` — crea HttpRangeServer + ControlApiServer, avvia io_context thread + tick timer
- [x] `ss_stop_server()` — ferma timer + servers + io_context thread
- [x] `ss_free_string()` — delete[] stringhe allocate dalla libreria
- [x] `map_error()` — mappa seekserve::errc → SS_ERR_* codici
- [x] Null-guard su tutti i parametri (ritorna SS_ERR_INVALID_ARG)
- [x] Opaque handle pattern: `struct SeekServeEngine : public seekserve::SeekServeEngine`

#### Test C API (19 tests — `tests/unit/test_capi.cpp`)
- [x] CreateAndDestroyEngine — crea con config valido, distrugge senza crash
- [x] CreateWithNullConfig — usa defaults
- [x] CreateWithEmptyConfig — usa defaults
- [x] CreateWithInvalidJson — parse fallisce, usa defaults, non crasha
- [x] DestroyNull — ss_engine_destroy(nullptr) non crasha
- [x] NullEngineReturnsError — tutti i metodi ritornano SS_ERR_INVALID_ARG con engine=null
- [x] FreeStringNull — ss_free_string(nullptr) non crasha
- [x] AddTorrentFromFile — aggiunge Sintel .torrent, verifica id_buf = 40 chars hex
- [x] AddTorrentNullUri — ritorna SS_ERR_INVALID_ARG
- [x] AddTorrentSmallBuffer — buffer 10 bytes, id troncato correttamente
- [x] ListFilesAfterAddTorrent — lista file Sintel, verifica JSON con sintel-2048-stereo_512kb.mp4
- [x] ListFilesNonexistentTorrent — ritorna errore
- [x] StartAndStopServer — start ritorna porta > 0, doppio start → SS_ERR_ALREADY_RUNNING
- [x] SetEventCallback — set e clear callback senza crash
- [x] GetStatusForAddedTorrent — JSON con torrent_id, progress, has_metadata
- [x] GetStatusNonexistentTorrent — ritorna JSON con error field
- [x] RemoveTorrent — remove + list_files fallisce dopo rimozione
- [x] RemoveNonexistentTorrent — SS_ERR_NOT_FOUND
- [x] FullLifecycle — create → start_server → add_torrent → list_files → get_status → stop_server → destroy

#### Proof-of-concept FFI
- [x] Eseguire `ffigen` su `seekserve_c.h` → generare Dart bindings (completato in M9)
- [x] Verificare che i bindings Dart compilano (completato in M9)

#### Verifica M7
- [x] C API test (GTest) completa lifecycle senza crash — 19/19 tests pass
- [x] Nessun tipo C++ leak attraverso l'header `seekserve_c.h`
- [x] 186 tests totali (138 unit + 29 integration + 19 C API) — tutti verdi
- [x] Dart bindings generati e compilabili (completato in M9: `dart run ffigen` + `flutter analyze` pass)

---

### M8: Hardening & Documentation

#### Server hardening
- [x] Socket timeouts: `SO_RCVTIMEO` (30s) + `SO_SNDTIMEO` (60s) on HttpRangeServer
- [x] Socket timeouts: 30s read/write on ControlApiServer
- [x] Connection limit: `max_concurrent_streams` (default 4) with `std::atomic<int>` counter + RAII guard
- [x] Control API connection limit: 20 concurrent connections
- [x] Request body size limit: 1MB max on ControlApiServer (413 Payload Too Large)

#### Graceful shutdown
- [x] `stop_server()` cancels all active ByteSources before stopping io_context
- [x] Alert handler guards with `removed_ids_` set to prevent use-after-free on late alerts
- [x] `MetadataCatalog::remove()` cleans up metadata on torrent removal
- [x] Fixed flaky RemoveTorrent test (race between add_torrent_alert and remove_torrent)

#### Stress tests
- [x] `ConcurrentHttpConnections`: 20 threads send concurrent Range requests
- [x] `RapidSeekSimulation`: 50 sequential random-offset Range requests
- [x] `ConnectionLimitEnforced`: max_concurrent_streams=2, verify excess rejected

#### Logging
- [x] `spdlog::debug` in ByteRangeMapper constructor
- [x] `spdlog::debug` in PieceAvailabilityIndex constructor
- [x] URL sanitization: `?token=` stripped from logs in both HTTP servers
- [x] Auth tokens never logged

#### Sanitizer presets
- [x] `CMakePresets.json`: asan + tsan configure/build presets
- [x] `setup.sh`: accepts `asan`/`tsan` as build type, passes `-fsanitize=...` flags

#### Documentazione
- [x] `docs/ARCHITECTURE.md` — moduli, flussi, thread model, design decisions
- [x] `docs/HTTP_RANGE_SPEC.md` — endpoint, range handling, response codes, streaming
- [x] `docs/SCHEDULER_POLICY.md` — hot/lookahead/seek boost, modes, parameters
- [x] `docs/STORAGE_POLICY.md` — piece storage, offline cache, SQLite schema, quota
- [x] `docs/SECURITY.md` — loopback, token auth, connection limits, logging safety
- [x] `docs/C_API.md` — function reference, error codes, config JSON, memory management

#### Cross-compile scripts
- [x] `scripts/build-ios.sh` — arm64, vcpkg arm64-ios triplet, CAPI only
- [x] `scripts/build-android.sh` — arm64-v8a via NDK, android-24, CAPI only

#### Misc
- [x] `.gitignore`: added `*.db`, `*.db-wal`, `*.db-shm`

#### Verifica M8
- [x] All 189 tests pass (138 unit + 32 integration + 19 C API)
- [x] Stress tests: 3/3 pass (concurrent connections, rapid seek, connection limit)
- [x] All 6 docs exist with content
- [x] Cross-compile scripts are executable
- [ ] TSan + ASan build + test run (infrastructure ready, run manually)

---

## Phase 2: Flutter Plugin

### M9: Flutter Plugin Scaffold + FFI Bindings

#### Plugin creation
- [x] Create plugin: `flutter create --template=plugin_ffi --platforms=ios,android flutter_seekserve`
- [x] Clean up generated boilerplate (removed `src/flutter_seekserve.c/.h`, old `lib/flutter_seekserve.dart`, old bindings)
- [x] Copy `seekserve-capi/include/seekserve_c.h` → `flutter_seekserve/native_header/seekserve_c.h`

#### ffigen configuration
- [x] Create `ffigen.yaml` with input `native_header/seekserve_c.h`, output `lib/src/bindings_generated.dart`
- [x] Include all `ss_*` functions, `SS_*` error code defines, `ss_event_callback_t` typedef
- [x] Run `dart run ffigen` — generated bindings compile (warning: `SeekServeEngine` opaque struct expected)
- [x] Verify generated Dart file has all 12 functions: `ss_engine_create`, `ss_engine_destroy`, `ss_add_torrent`, `ss_remove_torrent`, `ss_list_files`, `ss_select_file`, `ss_get_stream_url`, `ss_get_status`, `ss_set_event_callback`, `ss_start_server`, `ss_stop_server`, `ss_free_string`

#### Platform DynamicLibrary loader
- [x] Create `lib/src/native_library.dart`:
  - iOS/macOS: `DynamicLibrary.process()` (statically linked via framework)
  - Android/Linux: `DynamicLibrary.open('libseekserve.so')`
  - Singleton `nativeBindings` instance

#### Model classes
- [x] `lib/src/models/file_info.dart` — `FileInfo` with `index`, `path`, `size`, `name`, `extension`, `isVideo`, `fromJson()`, `toJson()`
- [x] `lib/src/models/torrent_status.dart` — `TorrentStatus` with all fields, `fromJson()`, `toJson()`
- [x] `lib/src/models/seekserve_config.dart` — `SeekServeConfig` with all config fields, `toJsonString()`
- [x] `lib/src/models/seekserve_event.dart` — `SeekServeEvent` sealed class: `MetadataReceived`, `FileCompleted`, `TorrentError`, `UnknownEvent`

#### pubspec.yaml
- [x] Add `ffi: ^2.1.3` dependency
- [x] Add `ffigen: ^13.0.0` dev dependency
- [x] Set `flutter: ffiPlugin: true` for android + ios platforms

#### Verification M9
- [x] `dart run ffigen` generates bindings without errors
- [x] `flutter analyze` passes — 0 issues
- [x] Generated `bindings_generated.dart` contains all 12 `ss_*` functions + `SeekServeEngine` opaque class + `SS_OK`/`SS_ERR_*` constants
- [x] Model classes compile and have `fromJson()` constructors
- [x] 14 unit tests pass (`flutter test`)

---

### M10: Native Library Builds (iOS + Android) ✅

#### iOS build
- [x] Update `scripts/build-ios.sh`:
  - Build static library (`.a`) not dynamic (`.dylib`) — App Store requires static
  - Build for `arm64` device (iphoneos SDK, `CMAKE_OSX_SYSROOT=iphoneos`)
  - Build for `arm64` simulator (iphonesimulator SDK, `CMAKE_OSX_SYSROOT=iphonesimulator`)
  - Combine 22 static libraries per slice with `libtool -static`
- [x] Added `SEEKSERVE_CAPI_STATIC` CMake option to root CMakeLists.txt + seekserve-capi/CMakeLists.txt
- [x] Create XCFramework: `xcodebuild -create-xcframework` from device + simulator combined `.a` libraries
- [x] Copy XCFramework to `flutter_seekserve/ios/Frameworks/seekserve.xcframework` (65 MB)
- [x] Write `ios/seekserve.podspec`:
  - `s.vendored_frameworks = 'Frameworks/seekserve.xcframework'`
  - `s.static_framework = true`
  - `s.platform = :ios, '15.0'`
  - `s.pod_target_xcconfig = { 'OTHER_LDFLAGS' => '-lc++ -lsqlite3' }` (link C++ stdlib + sqlite3)
- [x] `flutter build ios --no-codesign` succeeds — **Built Runner.app (37.9 MB)**

#### Android build
- [x] Update `scripts/build-android.sh` to build all 3 ABIs:
  - `arm64-v8a` (arm64-android triplet)
  - `armeabi-v7a` (custom `arm-neon-android` triplet — NDK 29 requires NEON=ON)
  - `x86_64` (x64-android triplet)
- [x] Custom vcpkg triplet `triplets/arm-neon-android.cmake` (NDK 29 dropped NEON=OFF support)
- [x] Export `ANDROID_NDK_HOME` for vcpkg's android toolchain detection
- [x] `ANDROID_PLATFORM=android-28` (Boost.Asio 1.90 requires `aligned_alloc` → API 28+)
- [x] Copy `.so` files to `flutter_seekserve/android/src/main/jniLibs/{abi}/libseekserve.so`
- [x] Strip with `llvm-strip`: 109 MB → 6.4 MB (arm64-v8a), 89 MB → 4.4 MB (armeabi-v7a), 105 MB → 6.5 MB (x86_64)
- [x] Configure `android/build.gradle`:
  - `minSdk = 28` (required for `aligned_alloc` compatibility)
  - `compileSdk = 36`
  - `ndk { abiFilters 'armeabi-v7a', 'arm64-v8a', 'x86_64' }`
  - `jniLibs.srcDirs = ['src/main/jniLibs']` (pre-built, no CMake)
- [x] `flutter build apk` succeeds — **Built app-release.apk (108.1 MB)**

#### Build automation
- [x] Create `scripts/build-flutter-natives.sh` orchestrator:
  1. Build iOS (device + simulator) → create XCFramework → copy to plugin
  2. Build Android (3 ABIs) → strip → copy `.so` to plugin jniLibs
  3. Print summary with library file sizes
  - Supports `ios`, `android`, or `all` argument

#### Platform config (example app)
- [x] iOS `Info.plist`: `NSAllowsLocalNetworking = true` (allow HTTP localhost)
- [x] iOS `Podfile`: `platform :ios, '15.0'`
- [x] Android `AndroidManifest.xml`: `<uses-permission android:name="android.permission.INTERNET"/>` + `android:networkSecurityConfig`
- [x] Android `res/xml/network_security_config.xml`: cleartext traffic allowed only for `127.0.0.1` + `localhost`
- [x] Android example app `minSdk = 28` in `build.gradle.kts`

#### Verification M10
- [x] `scripts/build-flutter-natives.sh` produces XCFramework + 3 `.so` files without errors
- [x] `cd flutter_seekserve/example && flutter build ios --no-codesign` succeeds (37.9 MB)
- [x] `cd flutter_seekserve/example && flutter build apk` succeeds (108.1 MB)
- [x] Library sizes (stripped): arm64-v8a 6.4 MB, armeabi-v7a 4.4 MB, x86_64 6.5 MB — all < 10 MB

---

### M11: Dart API Layer + Event System

#### SeekServeClient implementation
- [x] `lib/src/seekserve_client.dart` — main Dart wrapper class:
  - Constructor: `ss_engine_create()` with JSON config from `SeekServeConfig`
  - `dispose()`: `ss_engine_destroy()`, close `NativeCallable`, close `StreamController`
  - All FFI calls: check `ss_error_t` return, throw `SeekServeException` on error
  - Memory: `ss_free_string()` in `finally` blocks for every `char**` output
- [x] `startServer({int port = 0})` → returns assigned port via `Pointer<Uint16>` out param
- [x] `stopServer()` → calls `ss_stop_server()`
- [x] `addTorrent(String uri)` → returns `String` torrent ID (64-byte buffer, 40 hex chars + null)
- [x] `removeTorrent(String torrentId, {bool deleteFiles = false})`
- [x] `listFiles(String torrentId)` → returns `List<FileInfo>` (parsed from JSON via `ss_free_string`)
- [x] `selectFile(String torrentId, int fileIndex)`
- [x] `getStreamUrl(String torrentId, int fileIndex)` → returns `String` URL
- [x] `getStatus(String torrentId)` → returns `TorrentStatus` (parsed from JSON)

#### Event system (NativeCallable.listener)
- [x] `ss_set_event_callback` via `NativeCallable<ss_event_callback_tFunction>.listener`
- [x] Instance method `_handleNativeEvent` captures `this` — dispatches to Dart event loop
- [x] Events dispatched to `StreamController<SeekServeEvent>.broadcast().add()`
- [x] `Stream<SeekServeEvent> get events` — public broadcast stream
- [x] Event types (sealed class with `fromJson` factory):
  - `MetadataReceived` (torrent_id)
  - `FileCompleted` (torrent_id, file_index)
  - `TorrentError` (torrent_id, message)
  - `UnknownEvent` (type, data) — forward compatibility
- [x] `NativeCallable.close()` in `dispose()` — prevents dangling pointer
- [x] Guard: `_handleNativeEvent` checks `_disposed` before dispatching

#### Model JSON deserialization
- [x] `FileInfo.fromJson(Map<String, dynamic>)`: index, path, size + getters: name, extension, isVideo
- [x] `TorrentStatus.fromJson(Map<String, dynamic>)`: all status fields with null-safe parsing
- [x] `SeekServeConfig.toJsonString()`: serializes only non-null fields to JSON for `ss_engine_create()`

#### Error handling
- [x] `lib/src/seekserve_exception.dart`:
  - `SeekServeException` with error code + human-readable message
  - `checkError(int code)` utility: throws if code != SS_OK
  - All 7 error codes mapped: INVALID_ARG, NOT_FOUND, METADATA_PENDING, TIMEOUT, IO, ALREADY_RUNNING, CANCELLED
- [x] `_ensureNotDisposed()` guard on all public methods → throws `StateError`

#### Barrel export
- [x] `lib/seekserve.dart` exports: `SeekServeClient`, `SeekServeException`, `FileInfo`, `TorrentStatus`, `SeekServeConfig`, `SeekServeEvent`

#### Unit tests (14 tests in `test/models_test.dart`)
- [x] FileInfo: fromJson, name extraction, extension, isVideo, toJson roundtrip (6 tests)
- [x] TorrentStatus: fromJson all fields, fromJson missing optionals (2 tests)
- [x] SeekServeConfig: toJsonString non-null fields, empty config (2 tests)
- [x] SeekServeEvent: metadata_received, file_completed, error, unknown type (4 tests)

#### Verification M11
- [x] `flutter analyze` — 0 issues
- [x] `flutter test` — 14/14 pass
- [ ] Integration test with native library on device (requires M10 native builds)

---

### M12: Example App

#### App structure
- [x] `flutter_seekserve/example/` — standalone Flutter app
- [x] Dependencies (`example/pubspec.yaml`):
  - `flutter_seekserve` (path: `../`)
  - `media_kit: ^1.1.11` + `media_kit_video: ^1.2.5`
  - `media_kit_libs_ios_video: ^1.1.4` + `media_kit_libs_android_video: ^1.3.6`
  - `provider: ^6.1.2` (state management)
  - `path_provider: ^2.1.4` (platform save path)
- [x] `MediaKit.ensureInitialized()` in `main()`

#### State management
- [x] `lib/providers/seekserve_provider.dart` — `ChangeNotifier`:
  - Holds `SeekServeClient` instance + `Map<String, TorrentEntry>` state
  - `init(String savePath)`: create client with config, start server, listen to events, start 1s poll timer
  - `addTorrent(String uri)`: call client, add entry to map
  - `removeTorrent(String id)`: call client, remove from map
  - `listFiles(String id)`: delegate to client, cache in entry
  - `selectAndStream(String torrentId, int fileIndex)`: select file + get stream URL
  - `_onEvent()`: handles `MetadataReceived` (fetch files), `FileCompleted`, `TorrentError`
  - `_pollStatus()`: 1-second `Timer.periodic` polling `getStatus()` on all active torrents
  - `dispose()`: cancel timer, cancel event subscription, dispose client

#### Home Screen (`lib/screens/home_screen.dart`)
- [x] AppBar with title "SeekServe Demo" + server port display
- [x] `TextField` + "Add" `FilledButton`
- [x] Paste from clipboard `IconButton`
- [x] Error banner with `MaterialBanner` when errors occur
- [x] `ListView.builder` of active torrents:
  - `TorrentCard` widget with name, progress bar, download rate, peer count, state badge
  - Tap → navigate to File Selection screen
  - `Dismissible` swipe-to-delete with red background
- [x] Empty state message when no torrents added

#### File Selection Screen (`lib/screens/file_selection_screen.dart`)
- [x] AppBar with torrent name
- [x] `ListView.builder` of files from `provider.listFiles(torrentId)`:
  - File icon based on extension (movie, audiotrack, subtitles, image, file icons)
  - File name (basename from path)
  - Formatted file size (B/KB/MB/GB)
  - Video files: enabled with play arrow, primary color icon
  - Non-video files: greyed out, disabled
- [x] "Waiting for metadata..." when files not yet available
- [x] On tap video file:
  1. `provider.selectAndStream(torrentId, fileIndex)` → get URL
  2. Navigate to Player Screen with URL

#### Player Screen (`lib/screens/player_screen.dart`)
- [x] `media_kit` `Video` widget for video playback
- [x] Initialize `Player` with stream URL, auto-play on open
- [x] Player controls: built-in media_kit controls (play/pause, seek bar, fullscreen)
- [x] Below player — `StatusBar` widget:
  - `LinearProgressIndicator` with download progress
  - Download rate + upload rate (formatted)
  - Peer count
  - Stream mode badge (when available)
- [x] Auto-refresh via provider's 1s polling timer
- [x] `dispose()`: player disposed

#### Utility widgets
- [x] `lib/widgets/torrent_card.dart` — reusable torrent card with progress, rate, peers, state badge, metadata hint
- [x] `lib/widgets/status_bar.dart` — download/upload rates, peer count, stream mode chip

#### Platform configuration
- [x] iOS `Runner/Info.plist`: `NSAllowsLocalNetworking = true`
- [x] Android `AndroidManifest.xml`: `INTERNET` permission + `networkSecurityConfig` reference
- [x] Android `res/xml/network_security_config.xml`: cleartext for `127.0.0.1` + `localhost` only

#### Verification M12
- [x] `flutter analyze` — 0 issues (plugin + example)
- [x] `flutter test` — 14/14 pass
- [ ] App launches on iOS simulator (requires M10 native builds)
- [ ] App launches on Android emulator (requires M10 native builds)
- [ ] End-to-end: paste Sintel magnet → metadata → file list → tap video → playback
- [ ] Seek works with scheduler boost
- [ ] Status bar updates in real-time
- [ ] Swipe-to-delete, back navigation, empty state all functional

---

### M13: WebTorrent Support

#### libtorrent branch switch (RC_2_0 → master)
- [x] Switch libtorrent submodule from `RC_2_0` to `master` branch (commit `24a3adf35`)
- [x] Update `.gitmodules` to track `master` branch
- [x] Init recursive submodules: `libdatachannel`, `libjuice`, `usrsctp`, `plog`, `json`, `try_signal`, `asio-gnutls`
- [x] Fix `setup.sh`: use `--force` flag on nested submodule init (prevents corrupted working trees)

#### libtorrent master API migration (ABI v100 breaking changes)
- [x] `torrent_info::files()` → `torrent_info::layout()` (7 files: metadata_catalog, engine, main, 4 test files)
- [x] `torrent_info(filename, ec)` constructor removed → `lt::load_torrent_file(filename, ec, lt::load_torrent_limits{})` (3 files)
- [x] `shared_ptr<lt::torrent_info>` → `shared_ptr<const lt::torrent_info>` (`atp.ti` is const on master) (3 test files)
- [x] `create_torrent(file_storage&, piece_size, flags)` → `create_torrent(vector<create_file_entry>, piece_size, flags)` (test_streaming_scheduler)

#### Build system updates
- [x] Add `boost-json` to `vcpkg.json` (required by libtorrent master)
- [x] Add `openssl` to `vcpkg.json` (required by libdatachannel for cross-compile)
- [x] Enable WebTorrent in `scripts/build-ios.sh`: `SEEKSERVE_ENABLE_WEBTORRENT=ON`
- [x] Enable WebTorrent in `scripts/build-android.sh`: `SEEKSERVE_ENABLE_WEBTORRENT=ON`

#### Runtime WebTorrent config
- [x] Wire `enable_webtorrent` in `session_manager.cpp`:
  ```cpp
  #ifdef TORRENT_USE_RTC
  if (config.enable_webtorrent) {
      sp.set_str(lt::settings_pack::webtorrent_stun_server, "stun.l.google.com:19302");
  }
  #endif
  ```
- [x] WebTorrent trackers supported via existing `extra_trackers` config (e.g. `wss://tracker.webtorrent.dev`)

#### Verification M13
- [x] `./setup.sh debug` completes with `WebTorrent: ON` in output
- [x] `ctest` — 189/189 tests pass (macOS debug with WebTorrent ON)
- [x] `scripts/build-flutter-natives.sh ios` — XCFramework built with WebTorrent
- [x] `scripts/build-flutter-natives.sh android` — 3 ABIs `.so` built with WebTorrent
- [ ] `flutter build ios --no-codesign` with WebTorrent natives
- [ ] `flutter build apk` with WebTorrent natives
- [ ] Test WebRTC peer connections from mobile (browser peers)
- [ ] Verify on iOS device
- [ ] Verify on Android device
