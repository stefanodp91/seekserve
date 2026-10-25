#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <boost/shared_array.hpp>

#include "seekserve/error.hpp"
#include "seekserve/types.hpp"

namespace seekserve {

// A whole piece's bytes as libtorrent gives them, shared, never copied whole.
struct PieceBuffer {
    boost::shared_array<char> data;
    int size = 0;
};

// Reads whole pieces through libtorrent (app BUG-236, DEC-657): libtorrent
// says a piece is complete once its hash passes, which it checks from its
// store buffer, before the piece is written to the file; read from the file
// at that moment the piece can still be zeros. libtorrent's read_piece
// looks in the store buffer too.
class PieceReader {
public:
    virtual ~PieceReader() = default;

    // The bytes of `piece`, waiting at most `wait` for them:
    // errc::piece_not_available when libtorrent does not have the piece,
    // errc::timeout_waiting_for_piece when the wait is over, errc::cancelled
    // once cancelled.
    virtual Result<PieceBuffer> read(PieceIndex piece, std::chrono::milliseconds wait) = 0;

    // Wakes every read in progress and refuses the next ones.
    virtual void cancel() = 0;
};

// What a TorrentPieceReader did, for the app's debug log (app BUG-238,
// DEC-660): whether libtorrent answers its reads, with what, and how long
// they wait.
struct PieceReadStats {
    std::int64_t asks = 0;     // read_piece requests, repeated ones included
    std::int64_t repeats = 0;  // requests for a piece still unanswered
    std::int64_t answers = 0;  // answers with the piece's bytes
    std::int64_t errors = 0;   // answers with an error
    std::int64_t late = 0;     // answers that came with nobody waiting
    int waiting = 0;           // pieces asked and not answered now
    std::int64_t oldest_wait_ms = 0;  // since when the oldest of them waits
    std::string last_error;    // libtorrent's text of the last error
};

// The PieceReader of one torrent: `ask` starts libtorrent's read_piece, and
// on_read hands it the answer (read_piece_alert). One request per piece for
// the readers that wait for it together. An answer that comes after a read
// stopped waiting is the next read's: reading a big piece can take longer
// than a reader waits at a time. A read that finds the piece still
// unanswered, with nobody else waiting, asks again: an answer that never
// comes would otherwise stop the stream for good (app BUG-238, DEC-659).
class TorrentPieceReader final : public PieceReader {
public:
    using Ask = std::function<void(PieceIndex)>;
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;

    // How many pieces with no reader waiting are kept, asked or answered:
    // a stream reads on from where it is, so few are read again.
    static constexpr std::size_t kMaxIdlePieces = 8;

    explicit TorrentPieceReader(Ask ask, Now now = &Clock::now);

    Result<PieceBuffer> read(PieceIndex piece, std::chrono::milliseconds wait) override;
    void cancel() override;

    // libtorrent's answer for `piece`: its bytes, or an error, with
    // libtorrent's text for it in `error_text`.
    void on_read(PieceIndex piece, Result<PieceBuffer> answer, std::string error_text = {});

    PieceReadStats stats() const;

private:
    struct Pending {
        int waiters = 0;
        bool asked = false;
        Clock::time_point asked_at{};
        std::optional<Result<PieceBuffer>> answer;
        // When it was last left with nobody waiting: the oldest goes first.
        std::uint64_t idle_since = 0;
    };

    // `pending` has nobody waiting now: kept, up to kMaxIdlePieces.
    void keep_idle(Pending& pending);

    Ask ask_;
    Now now_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::unordered_map<PieceIndex, Pending> pending_;
    bool cancelled_ = false;
    std::uint64_t idle_clock_ = 0;
    PieceReadStats stats_;
};

} // namespace seekserve
