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
- [ ] Abilitare WebTorrent (richiede `libdatachannel` recursive submodule — disabilitato per ora)
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
- [ ] Eseguire `ffigen` su `seekserve_c.h` → generare Dart bindings
- [ ] Verificare che i bindings Dart compilano

#### Verifica M7
- [x] C API test (GTest) completa lifecycle senza crash — 19/19 tests pass
- [x] Nessun tipo C++ leak attraverso l'header `seekserve_c.h`
- [x] 186 tests totali (138 unit + 29 integration + 19 C API) — tutti verdi
- [ ] Dart bindings generati e compilabili (FFI PoC — futuro)

---

### M8: Hardening & Documentation

#### Thread safety
- [ ] Stress test: seek concorrenti rapidi (simulare scrubbing VLC veloce)
- [ ] Stress test: multiple HTTP connections simultanee
- [ ] Verificare con ThreadSanitizer (TSan)
- [ ] Verificare con AddressSanitizer (ASan)

#### Logging
- [ ] Logging consistente con spdlog in tutti i moduli
- [ ] Livelli configurabili (debug, info, warn, error)
- [ ] Nessun dato sensibile nei log

#### Robustezza server
- [ ] Connection timeout su HTTP server
- [ ] Max connessioni simultanee (ServerConfig::max_concurrent_streams)
- [ ] Graceful shutdown sequence: stop HTTP → cancel ByteSource → save resume data → stop session

#### Documentazione
- [ ] `docs/ARCHITECTURE.md` — moduli, flussi, thread model
- [ ] `docs/HTTP_RANGE_SPEC.md` — comportamenti 206/416, single-range
- [ ] `docs/SCHEDULER_POLICY.md` — hot/lookahead/seek/fallback, parametri
- [ ] `docs/STORAGE_POLICY.md` — offline-ready, quota, retention
- [ ] `docs/SECURITY.md` — loopback, token, hardening
- [ ] `docs/C_API.md` — documentazione API C per Flutter FFI

#### Cross-compile smoke test
- [ ] Crea script sh per Build statica per iOS (arm64) — solo compilazione, no test
- [ ] Crea script sh per Build statica per Android (arm64-v8a via NDK) — solo compilazione, no test

#### Verifica M8
- [ ] TSan + ASan: nessun errore
- [ ] Documentazione completa

---

## Fase 2: Flutter Plugin (futuro)

### M9: Flutter plugin structure
- [ ] Creare plugin con template `plugin_ffi`
- [ ] Struttura: `flutter_seekserve/` con `android/`, `ios/`, `lib/`, `src/`
- [ ] Copiare `seekserve_c.h` come header FFI target
- [ ] Configurare `ffigen` per generare Dart bindings automaticamente

### M10: iOS integration
- [ ] Podspec con vendored framework (seekserve static lib)
- [ ] Link libtorrent + boost + OpenSSL come static libs
- [ ] Info.plist: NSAppTransportSecurity per HTTP loopback
- [ ] Build e test su iOS simulator

### M11: Android integration
- [ ] CMakeLists.txt per NDK build
- [ ] Gradle configuration con externalNativeBuild
- [ ] Build seekserve + libtorrent per arm64-v8a, armeabi-v7a, x86_64
- [ ] Test su Android emulator

### M12: Dart API layer
- [ ] Classe `SeekServe` Dart con API high-level
- [ ] Stream-based event handling (Dart Streams da callback C)
- [ ] Async/await per operazioni lunghe (add torrent, wait metadata)
- [ ] Integration con video_player o flutter_vlc_player

### M13: WebTorrent support
- [ ] Abilitare libdatachannel recursive submodule
- [ ] Build libtorrent con `webtorrent=ON`
- [ ] Configurare WebTorrent trackers (wss://)
- [ ] Test connessione con peer WebTorrent (browser)
- [ ] Verificare su iOS e Android
