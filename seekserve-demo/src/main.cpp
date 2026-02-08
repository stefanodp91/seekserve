#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <algorithm>

#include <spdlog/spdlog.h>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/file_storage.hpp>

#include "seekserve/session_manager.hpp"
#include "seekserve/metadata_catalog.hpp"
#include "seekserve/byte_range_mapper.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/byte_source.hpp"
#include "seekserve/streaming_scheduler.hpp"
#include "seekserve/http_range_server.hpp"
#include "seekserve/control_api_server.hpp"
#include "seekserve/offline_cache.hpp"
#include "seekserve/config.hpp"

#include <boost/asio/io_context.hpp>
#include <filesystem>

static std::string infohash_to_hex(const lt::info_hash_t& ih) {
    if (ih.has_v2()) {
        return lt::aux::to_hex({ih.v2.data(), static_cast<ptrdiff_t>(ih.v2.size())});
    }
    return lt::aux::to_hex({ih.v1.data(), static_cast<ptrdiff_t>(ih.v1.size())});
}

static void print_usage() {
    std::cout << "Usage: seekserve-demo <torrent-file-or-magnet>\n"
              << "       seekserve-demo --local-file <path-to-video>\n"
              << "\n"
              << "Options:\n"
              << "  --save-path PATH   Download directory (default: ./downloads)\n"
              << "  --log-level LEVEL  debug|info|warn|error (default: info)\n"
              << "  --local-file PATH  Serve a local file directly (no torrent)\n"
              << std::endl;
}

static std::string format_size(std::int64_t bytes) {
    const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes);
    int idx = 0;
    while (size >= 1024.0 && idx < 4) {
        size /= 1024.0;
        ++idx;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f %s", size, suffixes[idx]);
    return buf;
}

// Serve a local file directly via HTTP Range Server (no torrent needed)
static int serve_local_file(const std::string& file_path, const std::string& log_level) {
    if (log_level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (log_level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (log_level == "error") spdlog::set_level(spdlog::level::err);
    else spdlog::set_level(spdlog::level::info);

    if (!std::filesystem::exists(file_path)) {
        std::cerr << "File not found: " << file_path << std::endl;
        return 1;
    }

    auto file_size = static_cast<std::int64_t>(std::filesystem::file_size(file_path));
    std::cout << "SeekServe Demo v0.1.0 — Local File Mode\n" << std::endl;
    std::cout << "File: " << file_path << " (" << format_size(file_size) << ")" << std::endl;

    // Create a synthetic single-file file_storage
    int piece_length = 1024 * 1024; // 1 MB pieces
    int num_pieces = static_cast<int>((file_size + piece_length - 1) / piece_length);
    int last_piece_size = static_cast<int>(file_size - static_cast<std::int64_t>(num_pieces - 1) * piece_length);

    lt::file_storage fs;
    fs.set_piece_length(piece_length);
    fs.add_file(std::filesystem::path(file_path).filename().string(), file_size);
    fs.set_num_pieces(num_pieces);

    seekserve::ByteRangeMapper mapper(fs, 0);
    seekserve::PieceAvailabilityIndex avail(num_pieces, piece_length, last_piece_size);

    // Mark all pieces as complete (file is fully available locally)
    for (int i = 0; i < num_pieces; ++i) avail.mark_complete(i);

    auto byte_source = std::make_shared<seekserve::ByteSource>(
        lt::torrent_handle{}, 0, file_path, mapper, avail, std::chrono::seconds(5));

    // Start HTTP server
    boost::asio::io_context ioc;
    seekserve::ServerConfig srv_config;
    seekserve::HttpRangeServer http_server(ioc, srv_config);

    http_server.set_auth_token("seekserve");
    std::string local_id = "00000000";  // hex-compatible ID for local mode
    http_server.set_byte_source(byte_source, local_id, 0, file_path);

    auto start_result = http_server.start(0);
    if (!start_result.ok()) {
        std::cerr << "Failed to start HTTP server: " << start_result.error().message() << std::endl;
        return 1;
    }

    std::thread io_thread([&ioc]() { ioc.run(); });

    auto stream_url = http_server.stream_url(local_id, 0);
    std::cout << "\n========================================"
              << "\n  HTTP Range Server started!"
              << "\n  Stream URL: " << stream_url
              << "\n========================================\n"
              << "\nOpen in VLC: File > Open Network Stream"
              << "\nOr:  vlc \"" << stream_url << "\""
              << "\nOr:  curl -r 0-99 \"" << stream_url << "\" | xxd | head"
              << "\n\nPress Enter to stop.\n" << std::endl;

    std::string line;
    std::getline(std::cin, line);

    std::cout << "Shutting down..." << std::endl;
    http_server.stop();
    ioc.stop();
    if (io_thread.joinable()) io_thread.join();

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string uri;
    std::string save_path = "./downloads";
    std::string log_level = "info";
    std::string local_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--save-path" && i + 1 < argc) {
            save_path = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
            log_level = argv[++i];
        } else if (arg == "--local-file" && i + 1 < argc) {
            local_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (uri.empty()) {
            uri = arg;
        }
    }

    // Local file mode — serve directly without torrent
    if (!local_file.empty()) {
        return serve_local_file(local_file, log_level);
    }

    if (uri.empty()) {
        print_usage();
        return 1;
    }

    // Configure logging
    if (log_level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (log_level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (log_level == "error") spdlog::set_level(spdlog::level::err);
    else spdlog::set_level(spdlog::level::info);

    std::cout << "SeekServe Demo v0.1.0\n" << std::endl;

    // Create session
    seekserve::SessionConfig config;
    config.save_path = save_path;
    config.extra_trackers = {
        "udp://tracker.opentrackr.org:1337/announce",
        "udp://open.stealth.si:80/announce",
        "udp://tracker.openbittorrent.com:6969/announce",
        "udp://exodus.desync.com:6969/announce",
        "udp://tracker.torrent.eu.org:451/announce",
    };

    seekserve::TorrentSessionManager session(config);
    seekserve::MetadataCatalog catalog;

    // Set up metadata handler
    std::atomic<bool> metadata_ready{false};

    session.alert_dispatcher().on<lt::metadata_received_alert>(
        [&](const lt::metadata_received_alert& a) {
            auto ti = a.handle.torrent_file();
            auto status = a.handle.status(lt::torrent_handle::query_name);
            auto id = infohash_to_hex(status.info_hashes);
            catalog.on_metadata_received(id, ti);
            metadata_ready.store(true);
        });

    // Also handle the case when we add a .torrent file (metadata is already available)
    session.alert_dispatcher().on<lt::add_torrent_alert>(
        [&](const lt::add_torrent_alert& a) {
            if (a.error) {
                spdlog::error("Failed to add torrent: {}", a.error.message());
                return;
            }
            auto ti = a.handle.torrent_file();
            if (ti) {
                auto status = a.handle.status(lt::torrent_handle::query_name);
                auto id = infohash_to_hex(status.info_hashes);
                catalog.on_metadata_received(id, ti);
                metadata_ready.store(true);
            }
        });

    // Add torrent
    seekserve::AddTorrentParams atp;
    atp.uri = uri;
    atp.save_path = save_path;

    auto result = session.add_torrent(atp);
    if (!result) {
        std::cerr << "Error adding torrent: " << result.error().message() << std::endl;
        return 1;
    }

    auto torrent_id = result.value();
    std::cout << "Added torrent: " << torrent_id << std::endl;
    std::cout << "Waiting for metadata..." << std::flush;

    // Wait for metadata
    int wait_count = 0;
    while (!metadata_ready.load() && wait_count < 120) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "." << std::flush;
        ++wait_count;
    }
    std::cout << std::endl;

    if (!metadata_ready.load()) {
        std::cerr << "Timeout waiting for metadata (120s)" << std::endl;
        return 1;
    }

    std::cout << "\nMetadata received!\n" << std::endl;

    // List all files
    auto files_result = catalog.list_files(torrent_id);
    if (!files_result) {
        std::cerr << "Error listing files: " << files_result.error().message() << std::endl;
        return 1;
    }

    const auto& files = files_result.value();
    std::cout << "Files in torrent (" << files.size() << " total):\n" << std::endl;

    // Group by extension
    int mp4_count = 0;
    for (const auto& f : files) {
        std::string ext;
        auto dot_pos = f.path.rfind('.');
        if (dot_pos != std::string::npos) {
            ext = f.path.substr(dot_pos);
        }

        // Highlight video files
        bool is_video = (ext == ".mp4" || ext == ".mkv" || ext == ".avi"
                        || ext == ".ogv" || ext == ".webm" || ext == ".mov");

        std::cout << "  [" << std::setw(3) << f.index << "] "
                  << std::setw(12) << std::right << format_size(f.size)
                  << "  " << f.path;

        if (is_video) {
            std::cout << "  <-- VIDEO";
            if (ext == ".mp4") ++mp4_count;
        }

        std::cout << "\n";
    }

    std::cout << "\nTotal files: " << files.size()
              << " | MP4 files: " << mp4_count << std::endl;

    // Interactive mode: select a file
    std::cout << "\nEnter file index to select for streaming (or 'q' to quit): ";
    std::string input;
    std::getline(std::cin, input);

    if (input == "q" || input.empty()) {
        std::cout << "Exiting." << std::endl;
        return 0;
    }

    int file_idx = std::stoi(input);
    auto handle = session.get_handle(torrent_id);
    auto select_result = catalog.select_file(torrent_id, file_idx, handle);
    if (!select_result) {
        std::cerr << "Error selecting file: " << select_result.error().message() << std::endl;
        return 1;
    }

    auto file_info = catalog.get_file(torrent_id, file_idx);
    if (!file_info) {
        std::cerr << "Error getting file info" << std::endl;
        return 1;
    }

    auto ti = handle.torrent_file();
    auto& fi = file_info.value();

    std::cout << "\nSelected: " << fi.path
              << " (" << format_size(fi.size) << ")"
              << "\nPiece range: [" << fi.first_piece
              << ", " << fi.end_piece << ")"
              << std::endl;

    // Set up PieceAvailabilityIndex for the selected file
    int num_pieces = ti->num_pieces();
    int piece_length = ti->piece_length();
    int last_piece_size = static_cast<int>(
        ti->total_size() - static_cast<std::int64_t>(num_pieces - 1) * piece_length);

    seekserve::PieceAvailabilityIndex avail(num_pieces, piece_length, last_piece_size);

    // Wire piece_finished_alert → PieceAvailabilityIndex
    session.alert_dispatcher().on<lt::piece_finished_alert>(
        [&avail](const lt::piece_finished_alert& a) {
            avail.mark_complete(static_cast<seekserve::PieceIndex>(a.piece_index));
        });

    // Set up ByteRangeMapper for display
    seekserve::ByteRangeMapper mapper(ti->layout(), file_idx);
    int file_pieces = mapper.end_piece() - mapper.first_piece();

    // Build the file path on disk (save_path + torrent file path)
    auto file_path_on_disk = (std::filesystem::path(save_path) / fi.path).string();

    // Create ByteSource for serving
    auto byte_source = std::make_shared<seekserve::ByteSource>(
        handle, file_idx, file_path_on_disk, mapper, avail, std::chrono::seconds(30));

    // Wire piece_finished_alert → also notify ByteSource
    session.alert_dispatcher().on<lt::piece_finished_alert>(
        [&byte_source](const lt::piece_finished_alert&) {
            byte_source->notify_piece_complete();
        });

    // Create StreamingScheduler
    seekserve::SchedulerConfig sched_config;
    seekserve::StreamingScheduler scheduler(sched_config, avail, mapper);

    // Wire piece_finished_alert → scheduler
    session.alert_dispatcher().on<lt::piece_finished_alert>(
        [&scheduler](const lt::piece_finished_alert& a) {
            scheduler.on_piece_complete(static_cast<seekserve::PieceIndex>(a.piece_index));
        });

    // Start HTTP Range Server
    boost::asio::io_context ioc;
    seekserve::ServerConfig srv_config;
    seekserve::HttpRangeServer http_server(ioc, srv_config);

    std::string auth_token = "seekserve";
    http_server.set_auth_token(auth_token);
    http_server.set_byte_source(byte_source, torrent_id, file_idx, fi.path);

    // Wire HTTP range requests → scheduler
    http_server.set_range_callback(
        [&scheduler, &handle](const seekserve::ByteRange& range,
                              const seekserve::TorrentId&, seekserve::FileIndex) {
            scheduler.on_range_request(range, handle);
        });

    auto start_result = http_server.start(0);
    if (!start_result.ok()) {
        std::cerr << "Failed to start HTTP server: " << start_result.error().message() << std::endl;
        return 1;
    }

    // Set up OfflineCacheManager
    seekserve::CacheConfig cache_config;
    cache_config.db_path = (std::filesystem::path(save_path) / "seekserve_cache.db").string();
    seekserve::OfflineCacheManager cache(cache_config);

    // Register all torrent files in cache
    cache.on_torrent_added(torrent_id, files);

    // Wire file_completed_alert → cache
    session.alert_dispatcher().on<lt::file_completed_alert>(
        [&cache, &torrent_id](const lt::file_completed_alert& a) {
            cache.on_file_completed(torrent_id, static_cast<seekserve::FileIndex>(a.index));
        });

    // Start Control API Server
    seekserve::ControlApiServer api_server(ioc, session, catalog, http_server, cache, auth_token);

    std::atomic<bool> stop{false};
    api_server.set_shutdown_callback([&stop]() {
        stop.store(true);
    });

    auto api_result = api_server.start(0);
    if (!api_result.ok()) {
        std::cerr << "Failed to start Control API: " << api_result.error().message() << std::endl;
        return 1;
    }

    // Run io_context in background thread
    std::thread io_thread([&ioc]() { ioc.run(); });

    auto stream_url = http_server.stream_url(torrent_id, file_idx);
    std::cout << "\n========================================"
              << "\n  HTTP Range Server started!"
              << "\n  Stream URL: " << stream_url
              << "\n  Control API: http://127.0.0.1:" << api_server.port()
              << "/api"
              << "\n  Auth token: " << auth_token
              << "\n========================================\n"
              << "\nOpen this URL in VLC: File > Open Network..."
              << "\nOr use: vlc \"" << stream_url << "\""
              << "\n\nAPI examples:"
              << "\n  curl -H 'Authorization: Bearer " << auth_token
              << "' http://127.0.0.1:" << api_server.port() << "/api/torrents"
              << "\n  curl -H 'Authorization: Bearer " << auth_token
              << "' http://127.0.0.1:" << api_server.port() << "/api/torrents/"
              << torrent_id << "/status"
              << "\n\nDownloading + serving... (press Enter to stop)\n" << std::endl;

    std::thread input_thread([&]() {
        std::string line;
        std::getline(std::cin, line);
        stop.store(true);
    });

    static const char* mode_names[] = {"STREAM", "ASSIST", "DLOAD"};

    while (!stop.load()) {
        auto status = handle.status();

        // Scheduler tick: evaluate mode, adjust deadlines
        scheduler.tick(handle, status);

        // Count completed pieces for the selected file
        int file_completed = 0;
        for (seekserve::PieceIndex p = fi.first_piece; p < fi.end_piece; ++p) {
            if (avail.is_complete(p)) ++file_completed;
        }
        float file_progress = file_pieces > 0
            ? static_cast<float>(file_completed) / static_cast<float>(file_pieces) * 100.0f
            : 0.0f;

        // Update cache progress
        cache.on_progress_update(torrent_id, file_idx, file_progress / 100.0f);

        auto contig = avail.contiguous_bytes_from(
            mapper.first_piece(), mapper.map(seekserve::ByteRange{0, 0}).first_offset);

        auto mode = scheduler.current_mode();
        auto playhead = scheduler.playhead_piece();
        auto deadlines = scheduler.active_deadlines();
        bool offline = cache.is_offline_ready(torrent_id, file_idx);

        std::cout << "\r  File: " << std::fixed << std::setprecision(1)
                  << file_progress << "% (" << file_completed << "/" << file_pieces << " pcs)"
                  << " | Buf: " << format_size(contig)
                  << " | DL: " << format_size(static_cast<std::int64_t>(status.download_rate)) << "/s"
                  << " | Peers: " << status.num_peers
                  << " | " << mode_names[static_cast<int>(mode)]
                  << " P" << playhead << " D" << deadlines
                  << (offline ? " [OFFLINE]" : "")
                  << "   " << std::flush;

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n\nShutting down..." << std::endl;
    api_server.stop();
    http_server.stop();
    ioc.stop();
    if (io_thread.joinable()) io_thread.join();
    if (input_thread.joinable()) input_thread.join();

    return 0;
}
