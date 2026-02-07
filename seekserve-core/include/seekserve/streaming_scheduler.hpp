#pragma once

#include <mutex>
#include <chrono>

#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>

#include "seekserve/types.hpp"
#include "seekserve/config.hpp"
#include "seekserve/piece_availability.hpp"
#include "seekserve/byte_range_mapper.hpp"

namespace seekserve {

class StreamingScheduler {
public:
    StreamingScheduler(SchedulerConfig config,
                       PieceAvailabilityIndex& avail,
                       ByteRangeMapper& mapper);

    // Called on each HTTP Range request. Maps bytes → pieces and sets deadlines.
    void on_range_request(const ByteRange& range, lt::torrent_handle& handle);

    // Called when a piece finishes downloading.
    void on_piece_complete(PieceIndex piece);

    // Periodic evaluation (1s). Updates mode, detects stalls, adjusts deadlines.
    void tick(lt::torrent_handle& handle, const lt::torrent_status& status);

    StreamMode current_mode() const;

    // Accessors for testing / observability
    PieceIndex playhead_piece() const;
    int active_deadlines() const;
    bool is_seek_boosting() const;
    int stall_count() const;

private:
    void set_deadlines(lt::torrent_handle& h);
    void evaluate_mode_switch(const lt::torrent_status& status);

    SchedulerConfig config_;
    PieceAvailabilityIndex& avail_;
    ByteRangeMapper& mapper_;

    StreamMode mode_ = StreamMode::StreamingFirst;
    PieceIndex playhead_piece_{0};
    int active_deadlines_{0};

    // Seek boost state
    bool seek_boosting_{false};
    std::chrono::steady_clock::time_point seek_boost_start_;

    int stall_count_{0};
    mutable std::mutex mu_;
};

} // namespace seekserve
