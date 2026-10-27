#include "seekserve/startup_focus.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace seekserve {

namespace {

bool contains(const PieceSpan& span, PieceIndex p) {
    return p >= span.first && p <= span.last;
}

bool complete_span(const PieceSpan& span, const PieceAvailabilityIndex& avail) {
    for (PieceIndex p = span.first; p <= span.last; ++p) {
        if (!avail.is_complete(p)) return false;
    }
    return true;
}

}  // namespace

StartupFocus::StartupFocus(SchedulerConfig config, bool with_end)
    : config_(config), with_end_(with_end) {}

std::vector<lt::download_priority_t> StartupFocus::begin(
    std::int64_t offset, const ByteRangeMapper& mapper,
    const PieceAvailabilityIndex& avail,
    const std::vector<lt::download_priority_t>& current, Clock::time_point now) {
    active_ = false;
    const std::int64_t size = mapper.file_size();
    const std::int64_t bytes = config_.startup_focus_bytes;
    if (bytes <= 0 || offset < 0 || offset >= size) return {};

    head_ = mapper.map(ByteRange{offset, std::min(size, offset + bytes) - 1});
    end_ = PieceSpan{0, -1, 0, 0};
    if (with_end_) end_ = mapper.map(ByteRange{std::max<std::int64_t>(0, size - bytes), size - 1});
    if (complete(avail)) return {};

    std::vector<lt::download_priority_t> wanted(current);
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        const auto p = static_cast<PieceIndex>(i);
        if (contains(head_, p) || contains(end_, p)) {
            wanted[i] = lt::top_priority;
        } else if (wanted[i] != lt::dont_download) {
            wanted[i] = lt::low_priority;
        }
    }
    active_ = true;
    started_ = now;
    return wanted;
}

bool StartupFocus::over(const PieceAvailabilityIndex& avail, Clock::time_point now) const {
    if (!active_) return false;
    if (now - started_ >= config_.startup_focus_timeout) return true;
    return complete(avail);
}

bool StartupFocus::complete(const PieceAvailabilityIndex& avail) const {
    return complete_span(head_, avail) && complete_span(end_, avail);
}

bool StartupFocus::active() const { return active_; }

void StartupFocus::end() { active_ = false; }

bool keeps_index_at_end(std::string_view path) {
    static constexpr std::array<std::string_view, 3> kExtensions{".mp4", ".m4v", ".mov"};
    for (const auto extension : kExtensions) {
        if (path.size() <= extension.size()) continue;
        const auto tail = path.substr(path.size() - extension.size());
        const bool same = std::equal(tail.begin(), tail.end(), extension.begin(),
                                     [](char a, char b) {
                                         return std::tolower(static_cast<unsigned char>(a)) == b;
                                     });
        if (same) return true;
    }
    return false;
}

} // namespace seekserve
