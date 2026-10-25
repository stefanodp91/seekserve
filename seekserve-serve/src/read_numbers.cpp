#include "read_numbers.hpp"

namespace seekserve {

void put_piece_reads(nlohmann::json& status, const PieceReadStats& stats) {
    status["piece_asks"] = stats.asks;
    status["piece_repeats"] = stats.repeats;
    status["piece_answers"] = stats.answers;
    status["piece_errors"] = stats.errors;
    status["piece_last_error"] = stats.last_error;
    status["piece_late"] = stats.late;
    status["piece_waiting"] = stats.waiting;
    status["piece_oldest_wait_ms"] = stats.oldest_wait_ms;
}

void put_stream_reads(nlohmann::json& status, const ByteSourceStats& stats) {
    status["disk_reads"] = stats.disk_reads;
    status["read_timeouts"] = stats.read_timeouts;
    status["readers_waiting_download"] = stats.waiting_download;
    status["readers_waiting_answer"] = stats.waiting_answer;
}

void put_wait_piece(nlohmann::json& status, const WaitPieceStats& stats) {
    status["wait_piece"] = stats.piece;
    status["wait_piece_peers"] = stats.peers;
    status["wait_piece_peers_open"] = stats.peers_open;
    status["wait_piece_blocks"] = stats.blocks;
    status["wait_piece_done"] = stats.done;
    status["wait_piece_asked"] = stats.asked;
    status["wait_piece_free"] = stats.free;
    status["wait_piece_askers"] = stats.askers;
    status["wait_piece_slowest_kbs"] = stats.slowest_kbs;
    status["wait_piece_fastest_kbs"] = stats.fastest_kbs;
    status["wait_piece_still_ms"] = stats.still_ms;
}

}  // namespace seekserve
