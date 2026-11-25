#pragma once

#include <mutex>
#include <unordered_map>

#include <libtorrent/torrent_handle.hpp>

#include "seekserve/types.hpp"

namespace seekserve {

class TorrentSessionManager;

// The tap of each torrent (app DEC-995, DEC-996; tap_rate): closed to a
// byte a second while it has every wanted piece and sharing a finished
// torrent is off, opened before it asks for pieces again. Checked every
// tick, not on alerts, which can be dropped; a tap just opened is not closed
// again for `grace_ticks` ticks, while libtorrent takes in the new
// priorities.
class UploadTapWatch {
public:
    UploadTapWatch(bool share_when_finished, int grace_ticks);

    // Each torrent of the session, its tap as its state asks.
    void tick(TorrentSessionManager& sessions);

    // Before the priorities of `id` change: its tap open, if closed.
    void open(const TorrentId& id, const lt::torrent_handle& handle);

    bool closed(const TorrentId& id) const;

private:
    struct Tap {
        int rate = 0;
        int grace = 0;
    };

    const bool share_when_finished_;
    const int grace_ticks_;
    mutable std::mutex mu_;
    std::unordered_map<TorrentId, Tap> taps_;
};

}  // namespace seekserve
