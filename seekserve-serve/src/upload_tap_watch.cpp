#include "seekserve/upload_tap_watch.hpp"

#include <spdlog/spdlog.h>

#include "seekserve/session_manager.hpp"
#include "seekserve/upload_tap.hpp"

namespace seekserve {

UploadTapWatch::UploadTapWatch(bool share_when_finished, int grace_ticks)
    : share_when_finished_(share_when_finished), grace_ticks_(grace_ticks) {}

void UploadTapWatch::tick(TorrentSessionManager& sessions) {
    if (share_when_finished_) return;
    std::unordered_map<TorrentId, Tap> now;
    for (const auto& id : sessions.list_torrents()) {
        auto handle = sessions.get_handle(id);
        if (!handle.is_valid()) continue;
        // libtorrent throws on a handle whose torrent went in between.
        try {
            const bool finished = handle.status({}).is_finished;
            std::lock_guard lock(mu_);
            Tap tap = taps_.count(id) ? taps_[id] : Tap{};
            const int rate = tap_rate(finished, share_when_finished_);
            if (rate != tap.rate) {
                if (rate == kClosedTapRate && tap.grace > 0) {
                    --tap.grace;
                } else {
                    handle.set_upload_limit(rate);
                    tap.rate = rate;
                    tap.grace = 0;
                    spdlog::info("UploadTapWatch: tap of {} {}", id,
                                 rate == kClosedTapRate ? "closed" : "open");
                }
            }
            now[id] = tap;
        } catch (const std::exception& e) {
            spdlog::debug("UploadTapWatch: no state for {}: {}", id, e.what());
        }
    }
    std::lock_guard lock(mu_);
    taps_ = std::move(now);
}

void UploadTapWatch::open(const TorrentId& id, const lt::torrent_handle& handle) {
    std::lock_guard lock(mu_);
    auto it = taps_.find(id);
    if (it == taps_.end() || it->second.rate != kClosedTapRate) return;
    try {
        handle.set_upload_limit(kOpenTapRate);
    } catch (const std::exception& e) {
        spdlog::debug("UploadTapWatch: no tap to open for {}: {}", id, e.what());
        return;
    }
    it->second = Tap{kOpenTapRate, grace_ticks_};
    spdlog::info("UploadTapWatch: tap of {} open, pieces wanted again", id);
}

bool UploadTapWatch::closed(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    auto it = taps_.find(id);
    return it != taps_.end() && it->second.rate == kClosedTapRate;
}

}  // namespace seekserve
