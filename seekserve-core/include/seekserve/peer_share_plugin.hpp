#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <libtorrent/extensions.hpp>

#include "seekserve/peer_share.hpp"
#include "seekserve/types.hpp"

namespace seekserve {

// The books of who gave and got bytes of pieces, one for each torrent of a
// session, by the id the torrent carries in its userdata (app BUG-792).
// Thread-safe: libtorrent opens and closes them on its network thread, the
// engine finds them on its own.
class PeerShareBooks {
public:
    // A new book for the torrent just added as `id`, in place of any other.
    std::shared_ptr<PeerShareBook> open(const TorrentId& id);
    // The torrent of `book` is gone: its book goes, unless `id` was added
    // again since and has another.
    void close(const TorrentId& id, const std::shared_ptr<PeerShareBook>& book);
    std::shared_ptr<PeerShareBook> find(const TorrentId& id) const;

private:
    mutable std::mutex mu_;
    std::unordered_map<TorrentId, std::shared_ptr<PeerShareBook>> books_;
};

// The session plugin that gives each torrent added with a TorrentId in its
// userdata a book in `books`, and each of its connections a reader: one
// reading every `every` (libtorrent's peer ticks are seconds) and one when it
// closes. It only reads: it changes nothing libtorrent does, and asks the
// torrent nothing that waits for the network thread.
std::shared_ptr<lt::plugin> make_peer_share_plugin(std::shared_ptr<PeerShareBooks> books,
                                                   std::chrono::seconds every);

}  // namespace seekserve
