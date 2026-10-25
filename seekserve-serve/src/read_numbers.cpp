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

}  // namespace seekserve
