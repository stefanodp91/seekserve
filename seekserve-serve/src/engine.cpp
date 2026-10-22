#include "seekserve/engine.hpp"
#include "seekserve/resume_data.hpp"
#include "seekserve/auth_utils.hpp"
#include "seekserve/file_source.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/file_storage.hpp>

#include <algorithm>
#include <filesystem>
#include <thread>

namespace seekserve {

namespace net = boost::asio;
using json = nlohmann::json;

namespace {

// One entry for each file libtorrent downloads (any priority above
// dont_download): its index, its size, the bytes of it that are there,
// whether it is whole and, for the two files the engine was asked for, what
// each is wanted for.
json wanted_files_json(const lt::torrent_handle& handle, const std::vector<FileInfo>& files,
                       const WantedFiles& roles) {
    const auto priorities = handle.get_file_priorities();
    // As exact as libtorrent can say: the blocks of unfinished pieces count.
    const auto downloaded = handle.file_progress();
    // Checked pieces only, which is what file_completed goes by: a file is
    // whole when every piece it has a byte in is there, never before. Asked
    // second, it may be a piece ahead of the count above.
    const auto checked = handle.file_progress(lt::torrent_handle::piece_granularity);

    json wanted = json::array();
    for (const auto& file : files) {
        const auto i = static_cast<std::size_t>(file.index);
        if (i >= priorities.size() || i >= downloaded.size() || i >= checked.size()) continue;
        if (priorities[i] == lt::dont_download) continue;
        json entry = {
            {"index", file.index},
            {"size", file.size},
            {"downloaded", std::max(downloaded[i], checked[i])},
            {"complete", checked[i] >= file.size},
        };
        if (file.index == roles.reader) {
            entry["role"] = "reader";
        } else if (file.index == roles.background) {
            entry["role"] = "background";
        }
        wanted.push_back(std::move(entry));
    }
    return wanted;
}

// Cancels what the peers were asked for and has not come yet, but for the
// pieces with a deadline: with no other deadline, a deadline set makes
// libtorrent cancel every request that is not time critical
// (torrent::set_piece_deadline, cancel_non_critical). It goes at once, on a
// piece still wanted and missing; libtorrent leaves that piece at a low
// priority, which the caller sets again. False when no piece is wanted and
// missing, and nothing was done.
bool cancel_requests(const lt::torrent_handle& handle) {
    auto st = handle.status(lt::torrent_handle::query_pieces);
    auto priorities = handle.get_piece_priorities();
    for (int p = 0; p < static_cast<int>(priorities.size()); ++p) {
        if (priorities[p] != lt::dont_download && !st.pieces.get_bit(lt::piece_index_t{p})) {
            handle.set_piece_deadline(lt::piece_index_t{p}, 0);
            handle.reset_piece_deadline(lt::piece_index_t{p});
            return true;
        }
    }
    return false;
}

} // namespace

SeekServeEngine::SeekServeEngine(const Config& config)
    : config_(config)
{
    sessions_ = std::make_unique<TorrentSessionManager>(config_.session);
    cache_ = std::make_unique<OfflineCacheManager>(config_.cache);
    wire_alerts();

    // Auto-restore persisted torrents
    auto saved = cache_->list_torrent_uris();
    const bool resume = config_.session.resume_data;
    int from_resume_data = 0;
    for (const auto& [id, uri] : saved) {
        // From its resume data when there is some: the files and the pieces
        // it has are known at once, with no peer and no check of the disk.
        if (resume) {
            auto data = cache_->load_resume_data(id);
            if (!data.empty()) {
                auto params = decode_resume_data(data);
                // Whoever was reading a file of it is gone.
                if (params) drop_reader_priorities(*params);
                if (params && sessions_->restore_torrent(id, std::move(*params))) {
                    ++from_resume_data;
                    continue;
                }
                spdlog::warn("Engine: resume data of {} not usable, restoring from its URI", id);
            }
        }

        AddTorrentParams atp;
        atp.uri = uri;
        atp.save_path = config_.session.save_path;
        // With resume data on, nothing restored starts by itself; without,
        // a restored torrent runs as it always did.
        atp.paused = resume;
        auto result = sessions_->add_torrent(atp);
        if (!result) {
            spdlog::warn("Engine: failed to restore torrent {}: {}", id, result.error().message());
            cache_->remove_torrent_uri(id);
        }
    }
    if (!saved.empty()) {
        spdlog::info("Engine: restored {} torrent(s), {} from resume data",
                     saved.size(), from_resume_data);
    }
}

SeekServeEngine::~SeekServeEngine() {
    stop_server();

    // The last word on what every torrent has, while the alert handlers
    // that write it are still running.
    flush_resume_data();

    // CRITICAL: Stop alert dispatcher FIRST — handlers reference catalog_, cache_, states_.
    // Must stop before those members are destroyed.
    if (sessions_) {
        sessions_->alert_dispatcher().stop();
    }

    api_server_.reset();
    http_server_.reset();
    states_.clear();
    cache_.reset();
    // catalog_ and sessions_ destroyed implicitly in member order
}

void SeekServeEngine::wire_alerts() {
    // metadata_received_alert → catalog + event
    sessions_->alert_dispatcher().on<lt::metadata_received_alert>(
        [this](const lt::metadata_received_alert& a) {
            auto ti = a.handle.torrent_file();
            auto found = sessions_->id_of(a.handle);
            if (!found) return;
            const auto& id = *found;

            // Guard: skip if torrent was explicitly removed
            {
                std::lock_guard lock(mu_);
                if (removed_ids_.count(id)) return;
            }

            register_metadata(id, ti);
            // From here a restart can do without the peers for the metadata.
            request_resume_data(a.handle);
        });

    // add_torrent_alert → catalog (for .torrent files with embedded metadata)
    sessions_->alert_dispatcher().on<lt::add_torrent_alert>(
        [this](const lt::add_torrent_alert& a) {
            if (a.error) {
                spdlog::error("Engine: add_torrent error: {}", a.error.message());
                fire_event("error", "{\"message\":\"" + a.error.message() + "\"}");
                return;
            }
            auto ti = a.handle.torrent_file();
            if (ti) {
                auto found = sessions_->id_of(a.handle);
                if (!found) return;
                const auto& id = *found;

                // Guard: skip if torrent was explicitly removed
                {
                    std::lock_guard lock(mu_);
                    if (removed_ids_.count(id)) return;
                }

                register_metadata(id, ti);
                request_resume_data(a.handle);
            }
        });

    // piece_finished_alert → availability + scheduler + byte_source
    sessions_->alert_dispatcher().on<lt::piece_finished_alert>(
        [this](const lt::piece_finished_alert& a) {
            auto found = sessions_->id_of(a.handle);
            if (!found) return;
            const auto& id = *found;
            auto piece = static_cast<PieceIndex>(a.piece_index);

            std::lock_guard lock(mu_);
            auto* state = find_state(id);
            if (state) {
                state->avail.mark_complete(piece);
                if (state->source) state->source->notify_piece_complete();
                if (state->scheduler) state->scheduler->on_piece_complete(piece);
            }
        });

    // file_completed_alert → cache
    sessions_->alert_dispatcher().on<lt::file_completed_alert>(
        [this](const lt::file_completed_alert& a) {
            auto found = sessions_->id_of(a.handle);
            if (!found) return;
            const auto& id = *found;
            auto fi = static_cast<FileIndex>(a.index);

            cache_->on_file_completed(id, fi);
            fire_event("file_completed",
                "{\"torrent_id\":\"" + id + "\",\"file_index\":" + std::to_string(fi) + "}");
            request_resume_data(a.handle);
        });

    // save_resume_data_alert → cache database
    sessions_->alert_dispatcher().on<lt::save_resume_data_alert>(
        [this](const lt::save_resume_data_alert& a) {
            if (auto found = sessions_->id_of(a.handle)) {
                bool removed = false;
                {
                    std::lock_guard lock(mu_);
                    removed = removed_ids_.count(*found) > 0;
                }
                // The cache only updates the row of a torrent it still has.
                if (!removed) cache_->save_resume_data(*found, encode_resume_data(a.params));
            }
            resume_data_answered();
        });

    // Nothing changed since the last save, no metadata yet, torrent gone.
    sessions_->alert_dispatcher().on<lt::save_resume_data_failed_alert>(
        [this](const lt::save_resume_data_failed_alert&) {
            resume_data_answered();
        });
}

Result<TorrentId> SeekServeEngine::add_torrent(const std::string& uri,
                                                const std::string& save_path) {
    AddTorrentParams atp;
    atp.uri = uri;
    atp.save_path = save_path;
    auto result = sessions_->add_torrent(atp);
    if (result) {
        const auto& id = result.value();
        cache_->save_torrent_uri(id, uri);
        // Added again after a removal in this session: its alerts count again.
        // Before, removed_ids_ kept skipping them, so the torrent never got
        // its files and has_metadata stayed false until the engine restarted
        // (app BUG-68). An alert the alert thread skipped before the erase is
        // caught up from the handle, which already has the metadata then.
        bool was_removed = false;
        {
            std::lock_guard lock(mu_);
            was_removed = removed_ids_.erase(id) > 0;
        }
        if (was_removed) {
            auto handle = sessions_->get_handle(id);
            auto ti = handle.is_valid() ? handle.torrent_file() : nullptr;
            if (ti && !catalog_.has_metadata(id)) {
                register_metadata(id, ti);
                request_resume_data(handle);
            }
        }
    }
    return result;
}

void SeekServeEngine::register_metadata(const TorrentId& id,
                                        std::shared_ptr<const lt::torrent_info> ti) {
    catalog_.on_metadata_received(id, ti);

    auto files_result = catalog_.list_files(id);
    if (files_result) {
        cache_->on_torrent_added(id, files_result.value());
    }

    fire_event("metadata_received", "{\"torrent_id\":\"" + id + "\"}");
}

Result<void> SeekServeEngine::remove_torrent(const TorrentId& id, bool delete_files) {
    cache_->remove_torrent_uri(id);

    // Remove + cancel ByteSources from HttpRangeServer FIRST.
    // This prevents new reads and unblocks in-flight ones before we
    // destroy the TorrentState (which owns avail_ and mapper_).
    if (http_server_) {
        http_server_->remove_byte_sources_for_torrent(id);
    }
    update_shares(id, -1, nullptr);

    // Now safe to destroy TorrentState
    {
        std::lock_guard lock(mu_);
        removed_ids_.insert(id);
        states_.erase(id);
    }
    catalog_.remove(id);
    return sessions_->remove_torrent(id, delete_files);
}

Result<void> SeekServeEngine::pause_torrent(const TorrentId& id) {
    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) {
        return make_error_code(errc::torrent_not_found);
    }
    // An auto-managed torrent can be resumed at any time by libtorrent's
    // queue (see torrent_handle::pause()): take it out of auto-management so
    // a pause really stops downloading and seeding.
    handle.unset_flags(lt::torrent_flags::auto_managed);
    handle.pause();
    spdlog::info("Engine: paused torrent {}", id);
    fire_event("torrent_paused", "{\"torrent_id\":\"" + id + "\"}");
    request_resume_data(id);
    return {};
}

namespace {

// libtorrent's error keeps a torrent paused until it is cleared, and
// clear_error alone checks the files against the sizes libtorrent remembers
// of them: what made the error, a file it could not open, is there again
// (its stat cache goes only when the files are released). A recheck clears
// the error, releases the files and reads them again. A torrent in error has
// no peers to lose.
void try_again_after_error(const lt::torrent_handle& handle, const TorrentId& id) {
    lt::torrent_status st;
    try {
        st = handle.status();
    } catch (const std::exception&) {
        return;  // Removed meanwhile: the resume answers for it.
    }
    if (!st.errc) return;
    spdlog::warn("Engine: torrent {} in error ({}), checked again", id, st.errc.message());
    handle.force_recheck();
}

}  // namespace

Result<void> SeekServeEngine::resume_torrent(const TorrentId& id) {
    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) {
        return make_error_code(errc::torrent_not_found);
    }
    // A torrent libtorrent put into error stays paused through a resume
    // until the error goes: the app's queue resumes it to try again (app
    // DEC-323).
    try_again_after_error(handle, id);
    handle.set_flags(lt::torrent_flags::auto_managed);
    handle.resume();
    spdlog::info("Engine: resumed torrent {}", id);
    fire_event("torrent_resumed", "{\"torrent_id\":\"" + id + "\"}");
    return {};
}

Result<void> SeekServeEngine::start_torrent(const TorrentId& id) {
    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) {
        return make_error_code(errc::torrent_not_found);
    }
    // Out of auto-management libtorrent's queue neither holds it back nor
    // counts it, so a download already running keeps its slot.
    handle.unset_flags(lt::torrent_flags::auto_managed);
    // As resume_torrent: starting it again is trying again.
    try_again_after_error(handle, id);
    handle.resume();
    spdlog::info("Engine: started torrent {} outside the queue", id);
    fire_event("torrent_resumed", "{\"torrent_id\":\"" + id + "\"}");
    return {};
}

Result<void> SeekServeEngine::force_reannounce(const TorrentId& id) {
    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) {
        return make_error_code(errc::torrent_not_found);
    }
    handle.force_reannounce();
    spdlog::info("Engine: force reannounce for torrent {}", id);
    return {};
}

Result<void> SeekServeEngine::recheck_torrent(const TorrentId& id) {
    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) {
        return make_error_code(errc::torrent_not_found);
    }
    handle.force_recheck();
    spdlog::info("Engine: recheck of torrent {}", id);
    return {};
}

std::vector<TorrentId> SeekServeEngine::list_torrents() const {
    // Return from DB (ordered by added_at) rather than the session's
    // unordered_map so the UI shows torrents in insertion order.
    auto saved = cache_->list_torrent_uris();
    std::vector<TorrentId> ids;
    ids.reserve(saved.size());
    for (auto& [id, uri] : saved) {
        ids.push_back(std::move(id));
    }
    return ids;
}

Result<std::vector<FileInfo>> SeekServeEngine::list_files(const TorrentId& id) {
    return catalog_.list_files(id);
}

Result<void> SeekServeEngine::select_file(const TorrentId& id, FileIndex fi) {
    // To want_files -1 is "no file"; here it is no file of the torrent.
    if (fi < 0) {
        if (!sessions_->get_handle(id).is_valid()) {
            return make_error_code(errc::torrent_not_found);
        }
        return make_error_code(catalog_.has_metadata(id) ? errc::file_not_found
                                                         : errc::metadata_not_ready);
    }
    return want_files(id, fi, -1, /*new_reader=*/true);
}

Result<void> SeekServeEngine::set_wanted_files(const TorrentId& id, FileIndex reader,
                                               FileIndex background) {
    return want_files(id, reader, background, /*new_reader=*/false);
}

Result<void> SeekServeEngine::want_files(const TorrentId& id, FileIndex reader,
                                         FileIndex background, bool new_reader) {
    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) {
        return make_error_code(errc::torrent_not_found);
    }

    auto ti = catalog_.torrent_info(id);
    if (!ti) {
        return make_error_code(errc::metadata_not_ready);
    }
    // Before anything changes: -1 is no file, anything else has to be one.
    for (FileIndex fi : {reader, background}) {
        if (fi == -1) continue;
        if (auto file = catalog_.get_file(id, fi); !file) return file.error();
    }

    FileIndex old_reader = -1;
    {
        std::lock_guard lock(mu_);
        if (auto* state = find_state(id)) old_reader = state->selected_file;
    }
    // A player keeps its stream while only the file behind it changes.
    const bool keeps_reader = !new_reader && reader >= 0 && reader == old_reader;

    // libtorrent throws on a handle whose torrent is gone: another thread
    // may remove it at any point from here on.
    try {
        if (old_reader >= 0 && !keeps_reader) {
            // The reader that was there leaves. Its ByteSource goes from the
            // HttpRangeServer first, which unblocks the reads in flight
            // before the state they use (avail_ and mapper_) is destroyed.
            if (http_server_) {
                http_server_->remove_byte_source(id, old_reader);
            }
            update_shares(id, old_reader, nullptr);
            std::unique_ptr<TorrentState> gone;
            {
                std::lock_guard lock(mu_);
                auto it = states_.find(id);
                if (it != states_.end()) {
                    gone = std::move(it->second);
                    states_.erase(it);
                }
            }
            gone.reset();
            // Deadlines are a reader's alone: the ones its scheduler left in
            // libtorrent go with it, whether its file stops or goes on as a
            // download. (The same file selected again keeps them, as ever.)
            if (reader != old_reader) {
                handle.clear_piece_deadlines();
            }
        }

        // The priorities of the files. libtorrent works the pieces' out
        // again from them, which also takes back the low priority it leaves
        // on a piece that lost its deadline.
        auto result = catalog_.set_wanted_files(id, reader, background, handle);
        if (!result) return result;

        // What the reader that left asked for and has not come yet goes too
        // (app BUG-201, DEC-330): libtorrent keeps the requests a deadline
        // made when the deadline goes or the file's priority drops, and the
        // peers go on sending them. On the phone the file left kept coming
        // for 5 to 10 seconds after the player closed, and the file behind it
        // waited (app P5-06, P5-09). What the requests of the files still
        // wanted lose, libtorrent asks again.
        if (old_reader >= 0 && !keeps_reader && reader != old_reader &&
            cancel_requests(handle)) {
            result = catalog_.set_wanted_files(id, reader, background, handle);
            if (!result) return result;
        }

        if (reader >= 0 && !keeps_reader) {
            auto state = std::make_unique<TorrentState>();

            int num_pieces = ti->num_pieces();
            int piece_length = ti->piece_length();
            int last_piece_size = static_cast<int>(
                ti->total_size() - static_cast<std::int64_t>(num_pieces - 1) * piece_length);

            state->avail.reset(num_pieces, piece_length, last_piece_size);

            // Pre-populate availability with already-downloaded pieces.
            // Without this, ByteSource::read() would block waiting for pieces
            // that are already on disk, since piece_finished_alert only fires
            // for NEW completions after this point.
            {
                auto st = handle.status(lt::torrent_handle::query_pieces);
                for (int p = 0; p < num_pieces; ++p) {
                    if (st.pieces.get_bit(lt::piece_index_t{p})) {
                        state->avail.mark_complete(static_cast<PieceIndex>(p));
                    }
                }
            }

            state->mapper = std::make_unique<ByteRangeMapper>(ti->layout(), reader);
            state->selected_file = reader;

            // Build file path on disk
            auto file_info = catalog_.get_file(id, reader);
            if (!file_info) return file_info.error();
            auto file_path = (std::filesystem::path(config_.session.save_path) /
                              file_info.value().path).string();

            state->source = std::make_shared<ByteSource>(
                handle, reader, file_path, *state->mapper, state->avail,
                std::chrono::duration_cast<std::chrono::milliseconds>(config_.server.read_timeout));

            state->scheduler = std::make_unique<StreamingScheduler>(
                config_.scheduler, state->avail, *state->mapper);

            // Register byte source on HTTP server if running
            if (http_server_) {
                http_server_->set_byte_source(state->source, id, reader, file_info.value().path);
            }
            update_shares(id, reader, state->source, file_info.value().path);

            {
                std::lock_guard lock(mu_);
                states_[id] = std::move(state);
            }
        }
    } catch (const std::exception& e) {
        spdlog::debug("Engine: torrent {} went while its files were chosen: {}", id, e.what());
        return make_error_code(errc::torrent_not_found);
    }

    return {};
}

Result<std::string> SeekServeEngine::get_stream_url(const TorrentId& id, FileIndex fi) {
    if (!http_server_) {
        return make_error_code(errc::session_not_started);
    }
    if (!sessions_->has_torrent(id)) {
        return make_error_code(errc::torrent_not_found);
    }
    return http_server_->stream_url(id, fi);
}

std::string SeekServeEngine::get_status_json(const TorrentId& id) {
    json status_json;

    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) {
        status_json["error"] = "torrent not found";
        return status_json.dump();
    }

    lt::torrent_status st;
    try {
        st = handle.status();
    } catch (const std::exception&) {
        // Removed by another thread since the check above: libtorrent
        // throws on a handle whose torrent is gone, and nothing between
        // here and the app's service would catch it (app BUG-199).
        status_json["error"] = "torrent not found";
        return status_json.dump();
    }
    auto selected = catalog_.selected_file(id);

    status_json["torrent_id"] = id;
    status_json["name"] = st.name;
    status_json["progress"] = st.progress;
    status_json["download_rate"] = st.download_rate;
    status_json["upload_rate"] = st.upload_rate;
    status_json["num_peers"] = st.num_peers;
    status_json["num_seeds"] = st.num_seeds;
    status_json["total_download"] = st.total_download;
    status_json["total_upload"] = st.total_upload;
    status_json["state"] = static_cast<int>(st.state);
    status_json["paused"] = !!(st.flags & lt::torrent_flags::paused);
    // libtorrent's own error, which keeps the torrent paused until it is
    // cleared (app BUG-202, P6-04; app DEC-323). Not under "error", which
    // says the engine does not have the torrent. "file" is libtorrent's
    // index, or one of its negative values when no file is to blame.
    if (st.errc) {
        status_json["torrent_error"] = {
            {"message", st.errc.message()},
            {"file", static_cast<int>(st.error_file)},
        };
    }
    status_json["has_metadata"] = catalog_.has_metadata(id);
    status_json["selected_file"] = selected.has_value() ? json(*selected) : json(nullptr);

    if (selected.has_value()) {
        status_json["offline_ready"] = cache_->is_offline_ready(id, *selected);
    }

    // What each wanted file has. "progress" above counts the wanted pieces of
    // the whole torrent: with more than one wanted file it is no file's own.
    if (auto files = catalog_.list_files(id)) {
        try {
            status_json["files"] =
                wanted_files_json(handle, files.value(), catalog_.wanted_files(id));
        } catch (const std::exception& e) {
            // The torrent went between two questions to libtorrent.
            spdlog::debug("Engine: no file status for {}: {}", id, e.what());
        }
    }

    {
        std::lock_guard lock(mu_);
        auto* state = find_state(id);
        if (state && state->scheduler) {
            status_json["stream_mode"] = static_cast<int>(state->scheduler->current_mode());
            status_json["playhead_piece"] = state->scheduler->playhead_piece();
            status_json["active_deadlines"] = state->scheduler->active_deadlines();
        }
    }

    return status_json.dump();
}

Result<std::uint16_t> SeekServeEngine::start_server(std::uint16_t port) {
    if (server_running_.load()) {
        return make_error_code(errc::server_already_running);
    }

    // Create servers
    http_server_ = std::make_shared<HttpRangeServer>(ioc_, config_.server);
    http_server_->set_auth_token(config_.auth_token);

    // Range requests and reader progress move the scheduler of the torrent
    // and file they are for. The callback used to capture the scheduler of
    // the last select_file of any torrent, so a download selected after the
    // stream took its seeks, and it pointed to a freed scheduler once that
    // selection was replaced or removed (app BUG-83).
    http_server_->set_range_callback(
        [this](const ByteRange& range, const TorrentId& id, FileIndex fi) {
            on_stream_range(range, id, fi);
        });

    api_server_ = std::make_unique<ControlApiServer>(
        ioc_, *sessions_, catalog_, *http_server_, *cache_, config_.auth_token);

    api_server_->set_shutdown_callback([this]() {
        stop_server();
    });

    // Start HTTP range server
    auto stream_result = http_server_->start(config_.server.stream_port);
    if (!stream_result) return stream_result;

    // Start control API server
    auto api_result = api_server_->start(port);
    if (!api_result) {
        http_server_->stop();
        return api_result;
    }

    // Start io_context with work guard
    work_guard_ = std::make_unique<work_guard_t>(ioc_.get_executor());
    io_thread_ = std::thread([this]() { ioc_.run(); });

    // Start scheduler tick timer
    start_tick_timer();

    server_running_.store(true);
    spdlog::info("Engine: servers started (stream:{}, api:{})",
                 http_server_->port(), api_server_->port());

    return api_result.value();
}

void SeekServeEngine::on_stream_range(const ByteRange& range, const TorrentId& id,
                                      FileIndex fi) {
    std::lock_guard lock(mu_);
    auto* state = find_state(id);
    if (!state || !state->scheduler || state->selected_file != fi) return;
    auto handle = sessions_->get_handle(id);
    if (!handle.is_valid()) return;
    state->scheduler->on_range_request(range, handle);
}

void SeekServeEngine::stop_and_drain(HttpRangeServer& server) {
    server.stop();
    // stop() has woken every connection thread. Bounded, so a stuck thread
    // cannot hold up the caller.
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (server.active_connections() > 0 && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (server.active_connections() > 0) {
        spdlog::warn("Engine: {} stream connection(s) still ending", server.active_connections());
    }
}

void SeekServeEngine::update_shares(const TorrentId& id, FileIndex fi,
                                    const std::shared_ptr<ByteSource>& source,
                                    const std::string& file_path) {
    std::lock_guard lock(shares_mu_);
    for (auto& [share_id, share] : shares_) {
        if (share.torrent != id || (fi >= 0 && share.file != fi)) continue;
        if (source) {
            share.server->set_byte_source(source, share_id, 0, file_path);
        } else {
            share.server->remove_byte_source(share_id, 0);
        }
    }
}

// Two real addresses: never every network of the device, nor any client.
static bool are_share_addresses(const std::string& bind_address,
                                const std::string& peer_address) {
    for (const auto* address : {&bind_address, &peer_address}) {
        boost::system::error_code ec;
        auto parsed = net::ip::make_address(*address, ec);
        if (ec || parsed.is_unspecified()) return false;
    }
    return true;
}

Result<SeekServeEngine::Share> SeekServeEngine::start_share(std::shared_ptr<StreamSource> source,
                                                            const std::string& file_path,
                                                            const std::string& bind_address,
                                                            const std::string& peer_address,
                                                            const TorrentId& torrent, FileIndex fi) {
    ServerConfig server_config = config_.server;
    server_config.bind_address = bind_address;
    server_config.allowed_peer = peer_address;
    server_config.stream_port = 0;
    auto server = std::make_shared<HttpRangeServer>(ioc_, server_config);

    // The share's id is the stream id of its URL: random, like its token.
    auto share_id = random_hex(16);
    server->set_auth_token(random_hex(16));
    server->set_byte_source(std::move(source), share_id, 0, file_path);
    if (!torrent.empty()) {
        server->set_range_callback(
            [this, torrent, fi](const ByteRange& range, const TorrentId&, FileIndex) {
                on_stream_range(range, torrent, fi);
            });
    }

    auto started = server->start(0);
    if (!started) return started.error();

    Share share{share_id, server->stream_url(share_id, 0)};
    shares_[share_id] = ShareState{std::move(server), torrent, fi};
    spdlog::info("Engine: share opened on {} for one client", bind_address);
    return share;
}

Result<SeekServeEngine::Share> SeekServeEngine::open_share(const TorrentId& id, FileIndex fi,
                                                           const std::string& bind_address,
                                                           const std::string& peer_address) {
    if (!are_share_addresses(bind_address, peer_address)) {
        return make_error_code(errc::invalid_argument);
    }

    // Held until the share is in shares_: stop_server, which takes it after
    // clearing server_running_, then closes it too, and update_shares, which
    // takes it when the file gets a new stream or loses it, then updates it.
    std::lock_guard shares_lock(shares_mu_);
    if (!server_running_.load()) return make_error_code(errc::session_not_started);
    if (!sessions_->has_torrent(id)) return make_error_code(errc::torrent_not_found);
    std::shared_ptr<StreamSource> source;
    {
        std::lock_guard lock(mu_);
        auto* state = find_state(id);
        if (state && state->selected_file == fi) source = state->source;
    }
    if (!source) return make_error_code(errc::file_not_found);
    auto file = catalog_.get_file(id, fi);
    if (!file) return file.error();

    return start_share(std::move(source), file.value().path, bind_address, peer_address, id, fi);
}

Result<SeekServeEngine::Share> SeekServeEngine::open_file_share(const std::string& file_path,
                                                                const std::string& bind_address,
                                                                const std::string& peer_address) {
    namespace fs = std::filesystem;
    if (!are_share_addresses(bind_address, peer_address)) {
        return make_error_code(errc::invalid_argument);
    }

    // Only a file inside the save path, once every link and ".." of both
    // paths is resolved.
    fs::path given(file_path);
    if (!given.is_absolute()) return make_error_code(errc::invalid_argument);
    std::error_code ec;
    auto file = fs::canonical(given, ec);
    if (ec) {
        return make_error_code(ec == std::errc::no_such_file_or_directory
                                   ? errc::file_not_found : errc::invalid_argument);
    }
    auto root = fs::canonical(config_.session.save_path, ec);
    if (ec) return make_error_code(errc::invalid_argument);
    auto inside = file.lexically_relative(root);
    if (inside.empty() || *inside.begin() == ".." || inside == ".") {
        spdlog::warn("Engine: a file share outside the save path was refused");
        return make_error_code(errc::invalid_argument);
    }

    // Opened by its resolved path, which FileSource then holds: no link in
    // between, and the file read is the file checked here.
    auto source = FileSource::open(file.string());
    if (!source) return source.error();

    std::lock_guard shares_lock(shares_mu_);
    if (!server_running_.load()) return make_error_code(errc::session_not_started);
    return start_share(source.value(), file.string(), bind_address, peer_address, {}, -1);
}

Result<void> SeekServeEngine::close_share(const std::string& share_id) {
    std::shared_ptr<HttpRangeServer> server;
    {
        std::lock_guard lock(shares_mu_);
        auto it = shares_.find(share_id);
        if (it == shares_.end()) return make_error_code(errc::file_not_found);
        server = std::move(it->second.server);
        shares_.erase(it);
    }
    stop_and_drain(*server);
    spdlog::info("Engine: share closed");
    return {};
}

void SeekServeEngine::stop_server() {
    if (!server_running_.exchange(false)) return;

    std::vector<std::shared_ptr<HttpRangeServer>> shares;
    {
        std::lock_guard lock(shares_mu_);
        for (auto& [share_id, share] : shares_) shares.push_back(std::move(share.server));
        shares_.clear();
    }
    for (auto& share : shares) stop_and_drain(*share);

    if (api_server_) api_server_->stop();
    if (http_server_) http_server_->stop();

    // Cancel all active ByteSources so in-flight reads unblock immediately
    {
        std::lock_guard lock(mu_);
        for (auto& [id, state] : states_) {
            if (state->source) state->source->cancel();
        }
    }

    // Stream connection threads own sockets of ioc_: let them end before
    // ioc_ can go.
    if (http_server_) stop_and_drain(*http_server_);

    if (work_guard_) {
        work_guard_.reset();
    }
    ioc_.stop();

    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    // Only now: on_tick runs on the io thread and re-arms the timer, and a
    // completion already queued ignores cancel().
    tick_timer_.reset();

    spdlog::info("Engine: servers stopped");
}

void SeekServeEngine::set_proxy(const ProxyConfig& proxy) {
    sessions_->set_proxy(proxy);
    config_.session.proxy = proxy;
}

void SeekServeEngine::set_event_callback(EventCallback cb) {
    std::lock_guard lock(event_mu_);
    event_cb_ = std::move(cb);
}

void SeekServeEngine::start_tick_timer() {
    tick_timer_ = std::make_unique<net::steady_timer>(ioc_);
    on_tick({});
}

void SeekServeEngine::on_tick(const boost::system::error_code& ec) {
    if (ec) return;

    {
        std::lock_guard lock(mu_);
        for (auto& [id, state] : states_) {
            if (!state->scheduler) continue;
            auto handle = sessions_->get_handle(id);
            if (!handle.is_valid()) continue;
            auto st = handle.status(lt::torrent_handle::query_pieces);

            // Pieces libtorrent has but the index missed (a completion
            // between select_file's snapshot and the new state, or a
            // dropped alert) would keep a stream waiting forever.
            if (state->mapper && st.pieces.size() > 0) {
                int end = std::min(state->mapper->end_piece(), st.pieces.size());
                bool added = false;
                for (int p = state->mapper->first_piece(); p < end; ++p) {
                    if (!state->avail.is_complete(p) &&
                        st.pieces.get_bit(lt::piece_index_t{p})) {
                        state->avail.mark_complete(p);
                        state->scheduler->on_piece_complete(p);
                        added = true;
                    }
                }
                if (added && state->source) state->source->notify_piece_complete();
            }

            state->scheduler->tick(handle, st);

            // Update cache progress for selected file
            if (state->selected_file >= 0 && state->mapper) {
                int first = state->mapper->first_piece();
                int end = state->mapper->end_piece();
                int completed = 0;
                for (int p = first; p < end; ++p) {
                    if (state->avail.is_complete(p)) ++completed;
                }
                int total = end - first;
                float progress = total > 0
                    ? static_cast<float>(completed) / static_cast<float>(total)
                    : 0.0f;
                cache_->on_progress_update(id, state->selected_file, progress);
            }
        }
    }

    // Torrents that changed are saved again every resume_save_interval: a
    // process killed without warning loses at most that much.
    auto every = config_.session.resume_save_interval.count();
    if (config_.session.resume_data && every > 0 && ++ticks_since_resume_save_ >= every) {
        ticks_since_resume_save_ = 0;
        request_all_resume_data();
    }

    tick_timer_->expires_after(std::chrono::seconds(1));
    tick_timer_->async_wait([this](const boost::system::error_code& ec2) {
        on_tick(ec2);
    });
}

void SeekServeEngine::request_resume_data(const TorrentId& id, bool flush_to_disk) {
    if (!config_.session.resume_data) return;
    request_resume_data(sessions_->get_handle(id), flush_to_disk);
}

void SeekServeEngine::request_resume_data(const lt::torrent_handle& handle, bool flush_to_disk) {
    if (!config_.session.resume_data || !handle.is_valid()) return;

    // With the info dictionary, so the files are known at the next start;
    // only if something changed, so an idle torrent is not written again.
    auto flags = lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified;
    if (flush_to_disk) flags |= lt::torrent_handle::flush_disk_cache;

    {
        std::lock_guard lock(resume_mu_);
        ++resume_pending_;
    }
    try {
        handle.save_resume_data(flags);
    } catch (const std::exception& e) {
        // The torrent left the session meanwhile: no alert will come.
        spdlog::debug("Engine: no resume data: {}", e.what());
        resume_data_answered();
    }
}

void SeekServeEngine::request_all_resume_data(bool flush_to_disk) {
    if (!config_.session.resume_data) return;
    for (const auto& id : sessions_->list_torrents()) {
        request_resume_data(id, flush_to_disk);
    }
}

void SeekServeEngine::resume_data_answered() {
    {
        std::lock_guard lock(resume_mu_);
        if (resume_pending_ > 0) --resume_pending_;
    }
    resume_cv_.notify_all();
}

void SeekServeEngine::flush_resume_data() {
    if (!config_.session.resume_data || !sessions_) return;
    request_all_resume_data(/*flush_to_disk=*/true);

    std::unique_lock lock(resume_mu_);
    bool done = resume_cv_.wait_for(lock, config_.session.resume_flush_timeout,
                                    [this] { return resume_pending_ == 0; });
    if (!done) {
        spdlog::warn("Engine: {} resume data request(s) unanswered at shutdown", resume_pending_);
    }
}

SeekServeEngine::TorrentState* SeekServeEngine::find_state(const TorrentId& id) {
    auto it = states_.find(id);
    return it != states_.end() ? it->second.get() : nullptr;
}

void SeekServeEngine::fire_event(const std::string& type, const std::string& data) {
    std::lock_guard lock(event_mu_);
    if (event_cb_) {
        std::string event = "{\"type\":\"" + type + "\",\"data\":" + data + "}";
        event_cb_(event);
    }
}

} // namespace seekserve
