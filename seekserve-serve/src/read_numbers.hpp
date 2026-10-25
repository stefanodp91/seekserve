#pragma once

#include <nlohmann/json.hpp>

#include "seekserve/byte_source.hpp"
#include "seekserve/piece_reader.hpp"

namespace seekserve {

// What the reads of a stream did, in the engine's status for the app's
// debug log (app BUG-238, DEC-660): libtorrent's answers to the piece reads,
// and what the stream's readers read and wait for. No address, no file name.
void put_piece_reads(nlohmann::json& status, const PieceReadStats& stats);
void put_stream_reads(nlohmann::json& status, const ByteSourceStats& stats);

}  // namespace seekserve
