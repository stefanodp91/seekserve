#include "seekserve/startup_focus.hpp"

#include <algorithm>

namespace seekserve {

StartupFocus::StartupFocus(SchedulerConfig config) : config_(config) {}

std::vector<lt::download_priority_t> StartupFocus::begin(
    std::int64_t offset, const ByteRangeMapper& mapper,
    const PieceAvailabilityIndex& avail,
    const std::vector<lt::download_priority_t>& current, Clock::time_point now) {
    active_ = false;
    const std::int64_t size = mapper.file_size();
    if (config_.startup_focus_bytes <= 0 || offset < 0 || offset >= size) return {};

    const std::int64_t end = std::min(size, offset + config_.startup_focus_bytes);
    const auto span = mapper.map(ByteRange{offset, end - 1});
    bool missing = false;
    for (PieceIndex p = span.first; p <= span.last; ++p) {
        if (!avail.is_complete(p)) missing = true;
    }
    if (!missing) return {};

    std::vector<lt::download_priority_t> wanted(current);
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        const auto p = static_cast<PieceIndex>(i);
        if (p >= span.first && p <= span.last) {
            wanted[i] = lt::top_priority;
        } else if (wanted[i] != lt::dont_download) {
            wanted[i] = lt::low_priority;
        }
    }
    active_ = true;
    first_ = span.first;
    last_ = span.last;
    started_ = now;
    return wanted;
}

bool StartupFocus::over(const PieceAvailabilityIndex& avail, Clock::time_point now) const {
    if (!active_) return false;
    if (now - started_ >= config_.startup_focus_timeout) return true;
    for (PieceIndex p = first_; p <= last_; ++p) {
        if (!avail.is_complete(p)) return false;
    }
    return true;
}

bool StartupFocus::active() const { return active_; }

void StartupFocus::end() { active_ = false; }

} // namespace seekserve
