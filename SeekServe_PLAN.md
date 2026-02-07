# SeekServe — Piano di realizzazione (per Claude Code, senza implementazioni)

## Premessa

**SeekServe** è un sottosistema **C++ edge-first** che permette di:
- aprire un torrent (magnet o `.torrent`);
- avviare lo streaming **locale** del file video tramite un **HTTP server su loopback** con supporto **Byte-Range** (necessario per il seek);
- supportare **seek veloce** (salto al minuto X) tramite nuove richieste HTTP Range;
- continuare **in background** il download fino a completamento per rendere il contenuto **offline-ready**;
- evitare re-download: se l’utente torna indietro, i byte già scaricati vengono serviti dallo storage locale.

**Vincoli e scelte tecniche**:
- motore BitTorrent: `arvidn/libtorrent` (libtorrent-rasterbar);
- server HTTP: Boost.Asio + Boost.Beast (HTTP/1.1 e gestione Range/206);
- riproduzione in app: **libVLC/VLCKit** che consuma l’URL locale (nessuna transcodifica necessaria per MKV, ecc.);
- focus: streaming/seek/caching. **Non** è obiettivo “hostare torrent” o creare un portale web.

> Nota di utilizzo: progettare e testare con contenuti **autorizzati o open-license** (es. Sintel / Blender Foundation CC BY).

---

## 1) Obiettivi e non-obiettivi

### 1.1 Obiettivi (MVP)
1. Add torrent (magnet / `.torrent`) e ottenere metadata.
2. Selezionare un file video nel torrent (anche in torrent multi-file).
3. Esporre un endpoint di streaming:
   - `GET /stream/{torrentId}/{fileId}`
   - server ascolta solo su `127.0.0.1`
4. Supporto **Range requests**:
   - risposte `206 Partial Content`
   - header `Accept-Ranges`, `Content-Range`, `Content-Length`
   - gestione errori: `416 Range Not Satisfiable` quando opportuno
5. Scheduling “playback-aware”:
   - **hot window**: pezzi necessari per il range richiesto
   - **lookahead**: prefetch oltre il playhead
   - **seek boost**: reprioritizzazione immediata al seek
6. Modalità **ibrida** “stream + download”:
   - streaming-first quando possibile
   - download-assist se throughput instabile
   - fallback download-first se lo streaming non regge
7. Persistenza locale + indice offline:
   - contenuto “offline-ready” a completamento
   - riuso dei byte già scaricati (no re-download su backward seek)

### 1.2 Non-obiettivi (questa fase)
- WebRTC / WebTorrent compat (non richiesto per questa architettura).
- Transcodifica live (FFmpeg) e packaging HLS/DASH (non necessario se target è app con libVLC).
- UI mobile completa: qui serve solo l’integrazione player (apertura URL) e controlli base.
- DRM, protezioni anticopia, cataloghi/ricerca contenuti.

---

## 2) Librerie e dipendenze (C++ / edge)

**Obbligatorie**
- `arvidn/libtorrent`: sessione torrent, peer, piece picking, storage, alerts.
- Boost.Asio: event loop, socket, timer, thread pool.
- Boost.Beast: parsing/serializzazione HTTP/1.1, gestione connessioni, implementazione server robusta.

**Raccomandate**
- `nlohmann/json`: Control API + status.
- `spdlog`: logging.
- TLS/crypto: *solo se* si decide di esporre HTTPS (tipicamente inutile su loopback).

**Player (lato app)**
- Android: libVLC (AAR)
- iOS: VLCKit / MobileVLCKit

---

## 3) Architettura e moduli (deliverable)

### 3.1 Struttura repository (proposta)
- `seekserve-core/`  
  Libreria C++ con: session manager, metadata catalog, scheduler, storage index.
- `seekserve-serve/`  
  Binario o libreria con HTTP Range Server + Control API (Asio/Beast).
- `seekserve-demo/`  
  Harness di test (desktop) che:
  - avvia engine + server
  - stampa URL locale da aprire con VLC
  - espone comandi minimi (add magnet, select file)
- `docs/`  
  Specifiche (solo testo) per Range, scheduler, storage, sicurezza.

### 3.2 Moduli logici (senza implementazione)
1. **TorrentSessionManager**
   - crea/gestisce `libtorrent::session`
   - add/remove torrent, lifecycle
   - normalizza alerts (metadata, errori, pezzi completati)
2. **MetadataCatalog**
   - mapping torrent → file list
   - selezione file attivo per streaming
3. **ByteRangeMapper**
   - `Range(bytesStart, bytesEnd)` → `PieceSpan(firstPiece,lastPiece)` + offset intra-piece
4. **PieceAvailabilityIndex**
   - indice “completed / missing” + calcolo bytes contigui disponibili dal playhead
5. **StreamingScheduler**
   - politiche: hot window, lookahead, seek boost, budget deadlines
   - modalità: streaming-first / download-assist / download-first
6. **ByteSource**
   - interfaccia file-like: read(offset,len)
   - attesa controllata se bytes non disponibili (timeout)
7. **HttpRangeServer (loopback)**
   - endpoint /stream con Range/206
   - gestione backpressure e connessioni
8. **ControlApiServer (loopback, separato dallo stream)**
   - add torrent, list files, select file, status, stop
9. **OfflineCacheManager / ContentStore**
   - persistenza e indice offline-ready
   - quota/retention (LRU/TTL)
   - riuso segmenti già scaricati

---

## 4) Specifica HTTP: streaming e seek (Range)

### 4.1 Comportamento richiesto
- Il server **deve** supportare richieste con header `Range: bytes=...`.
- Per richieste Range valide:
  - rispondere con `206 Partial Content`
  - includere `Content-Range: bytes start-end/total`
  - includere `Accept-Ranges: bytes`
- Per richieste fuori range / non soddisfacibili:
  - rispondere con `416 Range Not Satisfiable`
- Baseline: supportare **single-range**. Multi-range è opzionale (può essere aggiunto dopo).

### 4.2 Requisiti di robustezza
- Keep-Alive HTTP/1.1
- timeouts per evitare connessioni “appese” se swarm è scarso
- limitare il numero di stream simultanei (edge/battery)

---

## 5) Politiche di streaming (scheduler) — requisiti

### 5.1 Concetti
- **Playhead**: byte offset richiesto dalla richiesta Range corrente.
- **Hot window**: intervallo di pezzi che coprono il range richiesto (+ margine minimo).
- **Lookahead**: intervallo addizionale per bufferizzare avanti.
- **Seek boost**: aggressività temporanea post-seek, limitata da budget.

### 5.2 Vincoli operativi (edge)
- Budget CPU/rete: evitare “thrash” di priorità su troppi pezzi.
- **Deadline budget**: numero massimo di pezzi “time-critical” contemporanei.
- Stabilità throughput: se le deadlines causano regressioni, ridurre aggressività e preferire download-assist.

### 5.3 Modalità e fallback
- **Streaming-first**:
  - priorità massime su hot+lookahead
  - resto a priorità bassa (se offline download abilitato)
- **Download-assist**:
  - aumentare startup buffer/target buffer
  - ridurre frequenza di ripianificazioni e deadlines
- **Download-first**:
  - quando throughput < bitrate stimato o stall ripetuti
  - pre-buffer più grande o completamento file
  - riprendere playback quando soglia è soddisfatta

### 5.4 Criteri di switching (da definire come parametri)
- `min_contiguous_bytes`: bytes contigui minimi per considerare “buffer OK”
- `stall_count_threshold`: numero stall per attivare fallback
- `min_sustained_rate`: throughput medio minimo per sostenere playback

---

## 6) Storage, caching e offline

### 6.1 Principi
- I pezzi verificati vengono persistiti su storage locale (default).
- Ritorno indietro (backward seek) → serve dal disco (immediato).
- Avanzamento (forward seek) → ripianifica hot window, senza perdere ciò che è già scaricato.

### 6.2 Offline download
- Se `offline_download_enabled=true`:
  - completare il file in background a priorità bassa
  - marcare `offline-ready` al completamento
- Quota e retention:
  - LRU/TTL su contenuti non attivi
  - protezione contenuto in riproduzione

### 6.3 Indice locale
- mantenere metadati minimi:
  - torrentId/infohash
  - fileId
  - stato (in-progress / complete)
  - percentuale completamento
  - timestamp ultimo accesso

---

## 7) Osservabilità (telemetria) — requisiti

**Metriche**
- time-to-first-byte / time-to-first-frame (stimato)
- buffer health (bytes contigui disponibili)
- download rate / upload rate
- peer count
- seek latency (tempo tra nuova Range e ripresa emissione bytes)
- rebuffer count e durata

**Eventi**
- metadata ready
- change mode (streaming-first → download-assist → download-first)
- stall / timeout
- offline-ready

---

## 8) Sicurezza (edge)

- Server HTTP e Control API **solo su loopback**.
- Token/nonce per endpoint stream/control (prevenire accessi da altre app sul device).
- Logging: evitare dati sensibili nei log.

---

## 9) Piano di lavoro per Claude Code (task list, senza implementazioni)

### 9.1 Setup repository e documentazione
- Creare struttura repo e moduli (`seekserve-core`, `seekserve-serve`, `seekserve-demo`, `docs`).
- Scrivere documenti (solo testo):
  - `docs/ARCHITECTURE.md` (moduli, flussi, thread model)
  - `docs/HTTP_RANGE_SPEC.md` (comportamenti 206/416, single-range baseline)
  - `docs/SCHEDULER_POLICY.md` (hot/lookahead/seek/fallback, parametri)
  - `docs/STORAGE_POLICY.md` (offline-ready, quota, retention)
  - `docs/SECURITY.md` (loopback, token, hardening)

### 9.2 Contratti di interfaccia (design, non codice)
- Definire (in un doc) le interfacce pubbliche:
  - “add torrent”, “list files”, “select file”
  - “get stream URL”
  - “get status/metrics”
  - “stop torrent / stop server”
- Definire i dati scambiati (DTO): status, buffer health, selected file.

### 9.3 Specifiche di test (design, non codice)
- Definire casi di test (documento) usando contenuti open:
  - Avvio: avvio stream con range iniziale, verifica 206 e header.
  - Seek: jump avanti e indietro, verifica tempi e assenza re-download backward.
  - Peer scarso: simulare throughput basso → switch a download-assist o download-first.
  - Offline: completamento file → offline-ready → playback senza rete.
- Definire criteri “Definition of Done” (sotto).

### 9.4 Definition of Done (DoD)
**Core**
- Aggiunta torrent e metadata OK.
- Selezione file OK.
- Status e metriche minime.

**Serve**
- Endpoint /stream con single-range conforme a spec.
- Endpoint control minimi.
- Loopback-only + token.

**Scheduler**
- Hot window + lookahead + seek boost documentati e applicabili.
- Fallback mode documentato e verificabile tramite metriche.

**Offline**
- Persistenza e indice offline-ready.
- Quota/retention policy documentata.

**Demo**
- Harness che avvia tutto e consente verifica con player (VLC/libVLC).

---

## 10) Riferimenti (da tenere come note tecniche)

> Inseriti in code-block per evitare problemi con exporter Markdown→PDF.

```text
- libtorrent streaming: https://www.libtorrent.org/streaming.html
- libtorrent sequential_download note: https://www.libtorrent.org/single-page-ref.html
- RFC 7233 HTTP Range Requests: https://datatracker.ietf.org/doc/html/rfc7233
- MDN Accept-Ranges: https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Accept-Ranges
- Boost.Beast: https://www.boost.org/libs/beast
- Sintel (Blender open movie): https://durian.blender.org/
```
