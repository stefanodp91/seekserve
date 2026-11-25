#include "seekserve/peer_share_plugin.hpp"

#include <algorithm>
#include <utility>

#include <libtorrent/peer_connection_handle.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_handle.hpp>

namespace seekserve {

std::shared_ptr<PeerShareBook> PeerShareBooks::open(const TorrentId& id) {
    auto book = std::make_shared<PeerShareBook>();
    std::lock_guard lock(mu_);
    books_[id] = book;
    return book;
}

void PeerShareBooks::close(const TorrentId& id, const std::shared_ptr<PeerShareBook>& book) {
    std::lock_guard lock(mu_);
    auto it = books_.find(id);
    if (it != books_.end() && it->second == book) books_.erase(it);
}

std::shared_ptr<PeerShareBook> PeerShareBooks::find(const TorrentId& id) const {
    std::lock_guard lock(mu_);
    auto it = books_.find(id);
    return it == books_.end() ? nullptr : it->second;
}

namespace {

// One connection's reader. Its callbacks run on libtorrent's network thread,
// inside libtorrent's own handling of the connection: nothing may throw out
// of them, and nothing may wait for that thread.
class PeerShareReader final : public lt::peer_plugin {
public:
    PeerShareReader(lt::peer_connection_handle handle, std::shared_ptr<PeerShareBook> book,
                    std::int64_t every)
        : handle_(std::move(handle)), book_(std::move(book)), every_(every) {}

    ~PeerShareReader() override { book_->close(conn_, nullptr); }

    // Once a second, from the connection's own tick.
    void tick() override {
        if (++ticks_ < every_) return;
        ticks_ = 0;
        lt::peer_info peer;
        if (read(peer)) book_->read(conn_, peer);
    }

    // Before libtorrent lets the connection go: its totals are final.
    void on_disconnect(lt::error_code const&) override {
        lt::peer_info peer;
        book_->close(conn_, read(peer) ? &peer : nullptr);
    }

private:
    // The connection's totals and flags, while its torrent is there: without
    // the torrent libtorrent cannot describe the connection, and its book is
    // going anyway.
    bool read(lt::peer_info& peer) const noexcept {
        try {
            if (!handle_.associated_torrent().is_valid()) return false;
            handle_.get_peer_info(peer);
            return true;
        } catch (...) {
            return false;
        }
    }

    lt::peer_connection_handle handle_;
    std::shared_ptr<PeerShareBook> book_;
    std::int64_t every_;
    std::int64_t ticks_ = 0;
    ConnectionShare conn_;
};

// One torrent: its book, open while the torrent is.
class PeerShareTorrent final : public lt::torrent_plugin {
public:
    PeerShareTorrent(TorrentId id, std::shared_ptr<PeerShareBooks> books, std::int64_t every)
        : id_(std::move(id)), books_(std::move(books)), book_(books_->open(id_)), every_(every) {}

    ~PeerShareTorrent() override { books_->close(id_, book_); }

    std::shared_ptr<lt::peer_plugin> new_connection(lt::peer_connection_handle const& handle) override {
        try {
            return std::make_shared<PeerShareReader>(handle, book_, every_);
        } catch (...) {
            return nullptr;
        }
    }

private:
    TorrentId id_;
    std::shared_ptr<PeerShareBooks> books_;
    std::shared_ptr<PeerShareBook> book_;
    std::int64_t every_;
};

class PeerShareSession final : public lt::plugin {
public:
    PeerShareSession(std::shared_ptr<PeerShareBooks> books, std::int64_t every)
        : books_(std::move(books)), every_(every) {}

    std::shared_ptr<lt::torrent_plugin> new_torrent(lt::torrent_handle const&,
                                                    lt::client_data_t userdata) override {
        const auto* id = userdata.get<TorrentId>();
        if (!id) return nullptr;
        try {
            return std::make_shared<PeerShareTorrent>(*id, books_, every_);
        } catch (...) {
            return nullptr;
        }
    }

private:
    std::shared_ptr<PeerShareBooks> books_;
    std::int64_t every_;
};

}  // namespace

std::shared_ptr<lt::plugin> make_peer_share_plugin(std::shared_ptr<PeerShareBooks> books,
                                                   std::chrono::seconds every) {
    return std::make_shared<PeerShareSession>(std::move(books),
                                              std::max<std::int64_t>(1, every.count()));
}

}  // namespace seekserve
