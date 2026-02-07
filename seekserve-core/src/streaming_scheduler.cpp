#include "seekserve/streaming_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace seekserve {

StreamingScheduler::StreamingScheduler(SchedulerConfig config,
                                       PieceAvailabilityIndex& avail,
                                       ByteRangeMapper& mapper)
    : config_(config)
    , avail_(avail)
    , mapper_(mapper)
    , seek_boost_start_(std::chrono::steady_clock::now())
{
    playhead_piece_ = mapper_.first_piece();
}

void StreamingScheduler::on_range_request(const ByteRange& range, lt::torrent_handle& handle) {
    std::lock_guard lock(mu_);

    auto span = mapper_.map(range);
    auto new_playhead = span.first;

    // Detect seek: playhead jumped by more than hot_window_pieces
    auto distance = std::abs(new_playhead - playhead_piece_);
    if (distance > config_.hot_window_pieces) {
        spdlog::debug("StreamingScheduler: seek detected, {} → {} (distance={})",
                      playhead_piece_, new_playhead, distance);

        // Clear existing deadlines and activate seek boost
        handle.clear_piece_deadlines();
        seek_boosting_ = true;
        seek_boost_start_ = std::chrono::steady_clock::now();
    }

    playhead_piece_ = new_playhead;
    set_deadlines(handle);
}

void StreamingScheduler::on_piece_complete(PieceIndex piece) {
    std::lock_guard lock(mu_);
    if (piece >= playhead_piece_) {
        if (active_deadlines_ > 0) --active_deadlines_;
    }
}

void StreamingScheduler::tick(lt::torrent_handle& handle, const lt::torrent_status& status) {
    std::lock_guard lock(mu_);

    // Expire seek boost
    if (seek_boosting_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - seek_boost_start_).count();
        if (elapsed >= config_.seek_boost_duration_ms) {
            spdlog::debug("StreamingScheduler: seek boost expired");
            seek_boosting_ = false;
        }
    }

    // Detect stall: playhead piece not complete
    if (!avail_.is_complete(playhead_piece_)) {
        ++stall_count_;
    } else {
        stall_count_ = 0;
    }

    evaluate_mode_switch(status);
    set_deadlines(handle);
}

StreamMode StreamingScheduler::current_mode() const {
    std::lock_guard lock(mu_);
    return mode_;
}

PieceIndex StreamingScheduler::playhead_piece() const {
    std::lock_guard lock(mu_);
    return playhead_piece_;
}

int StreamingScheduler::active_deadlines() const {
    std::lock_guard lock(mu_);
    return active_deadlines_;
}

bool StreamingScheduler::is_seek_boosting() const {
    std::lock_guard lock(mu_);
    return seek_boosting_;
}

int StreamingScheduler::stall_count() const {
    std::lock_guard lock(mu_);
    return stall_count_;
}

void StreamingScheduler::set_deadlines(lt::torrent_handle& h) {
    auto file_first = mapper_.first_piece();
    auto file_end = mapper_.end_piece();  // exclusive

    // Determine effective hot window size (expand during seek boost)
    int hot_size = config_.hot_window_pieces;
    int lookahead_size = config_.lookahead_pieces;

    if (seek_boosting_) {
        hot_size += config_.seek_boost_pieces;
    }

    // Mode-dependent adjustments
    switch (mode_) {
        case StreamMode::StreamingFirst:
            break;
        case StreamMode::DownloadAssist:
            lookahead_size *= 2;
            break;
        case StreamMode::DownloadFirst:
            hot_size = std::max(3, hot_size / 2);
            lookahead_size = hot_size;
            break;
    }

    int deadline_count = 0;
    int budget = config_.deadline_budget;

    // Hot window: short deadlines (100ms base, +50ms per piece from playhead)
    for (int i = 0; i < hot_size && deadline_count < budget; ++i) {
        PieceIndex p = playhead_piece_ + i;
        if (p < file_first || p >= file_end) continue;
        if (avail_.is_complete(p)) continue;

        int deadline_ms = 100 + i * 50;
        h.set_piece_deadline(lt::piece_index_t{p}, deadline_ms);
        ++deadline_count;
    }

    // Lookahead: longer deadlines (2000ms base, +200ms per piece beyond hot)
    for (int i = 0; i < lookahead_size && deadline_count < budget; ++i) {
        PieceIndex p = playhead_piece_ + hot_size + i;
        if (p < file_first || p >= file_end) continue;
        if (avail_.is_complete(p)) continue;

        int deadline_ms = 2000 + i * 200;
        h.set_piece_deadline(lt::piece_index_t{p}, deadline_ms);
        ++deadline_count;
    }

    active_deadlines_ = deadline_count;
}

void StreamingScheduler::evaluate_mode_switch(const lt::torrent_status& status) {
    auto prev_mode = mode_;

    // Check contiguous buffer from playhead
    auto span = mapper_.map(ByteRange{0, 0});
    auto contig = avail_.contiguous_bytes_from(playhead_piece_, span.first_offset);

    switch (mode_) {
        case StreamMode::StreamingFirst:
            if (stall_count_ >= config_.stall_count_threshold) {
                mode_ = StreamMode::DownloadAssist;
            }
            break;

        case StreamMode::DownloadAssist:
            if (status.download_rate < config_.min_sustained_rate
                && stall_count_ >= config_.stall_count_threshold * 2) {
                mode_ = StreamMode::DownloadFirst;
            }
            if (contig >= config_.min_contiguous_bytes && stall_count_ == 0) {
                mode_ = StreamMode::StreamingFirst;
            }
            break;

        case StreamMode::DownloadFirst:
            if (contig >= config_.min_contiguous_bytes) {
                mode_ = StreamMode::DownloadAssist;
            }
            break;
    }

    if (mode_ != prev_mode) {
        spdlog::info("StreamingScheduler: mode {} → {}",
                     static_cast<int>(prev_mode), static_cast<int>(mode_));
        stall_count_ = 0;
    }
}

} // namespace seekserve
