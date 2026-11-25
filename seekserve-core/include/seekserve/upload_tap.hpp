#pragma once

#include <optional>

#include <libtorrent/settings_pack.hpp>

namespace seekserve {

// How much the engine shares while it downloads (app DEC-996): as libtorrent
// would, with fewer peers served, or with none, for a device that heats or
// slows down, as a car shifts down on a climb. Only who gets pieces from us
// changes: our own requests for pieces never wait behind a limit.
enum class UploadGear {
    normal = 0,
    reduced = 1,
    off = 2,
};

// The gear a number of the C API names; nothing for any other number.
std::optional<UploadGear> upload_gear_from(int value);

// The session settings of `gear`: how many peers get pieces from us and how
// many pieces a peer may take before it gets a place (libtorrent's "allowed
// fast", which would share some even with no place). The normal gear is
// libtorrent's own; the reduced one serves `reduced_slots` peers. Applied as
// a partial pack: nothing else of the session changes, its proxy least of all.
lt::settings_pack upload_gear_settings(UploadGear gear, int reduced_slots);

// The upload limit of a torrent, in bytes a second (app DEC-995, DEC-996):
// with every wanted piece there and sharing a finished torrent off, the tap
// is closed to libtorrent's least, a byte a second, since 0 would be no
// limit at all; any other time it is open. A torrent that wants pieces again
// must have it open before it asks for them: the limit holds back our
// requests too.
inline constexpr int kClosedTapRate = 1;
inline constexpr int kOpenTapRate = 0;
int tap_rate(bool finished, bool share_when_finished);

}  // namespace seekserve
