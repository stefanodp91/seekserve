#include "seekserve/subtitle_track.hpp"

#include <algorithm>
#include <cctype>

namespace seekserve {

bool is_utf8_text(const std::string& text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const auto* end = p + text.size();
    while (p < end) {
        unsigned char c = *p;
        if (c == 0) return false;
        std::size_t extra;
        std::uint32_t cp;
        if (c < 0x80) { ++p; continue; }
        if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
        else return false;
        if (static_cast<std::size_t>(end - p) <= extra) return false;
        for (std::size_t i = 1; i <= extra; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        // Overlong forms, surrogates and code points past U+10FFFF.
        static constexpr std::uint32_t kMin[] = {0, 0x80, 0x800, 0x10000};
        if (cp < kMin[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        p += extra + 1;
    }
    return true;
}

Result<SubtitleTrack> checked_subtitle_track(SubtitleTrack track) {
    const auto invalid = make_error_code(errc::invalid_argument);
    if (track.cues.empty() || track.cues.size() > SubtitleLimits::kMaxCues) return invalid;

    if (track.language.empty()) track.language = "und";
    if (track.language.size() != 3 ||
        !std::all_of(track.language.begin(), track.language.end(),
                     [](unsigned char c) { return std::isalpha(c) != 0 && c < 0x80; })) {
        return invalid;
    }
    if (track.name.size() > SubtitleLimits::kMaxNameBytes || !is_utf8_text(track.name)) {
        return invalid;
    }

    std::size_t total = 0;
    for (const auto& cue : track.cues) {
        if (cue.start_ms < 0 || cue.end_ms <= cue.start_ms ||
            cue.end_ms > SubtitleLimits::kMaxTimeMs) {
            return invalid;
        }
        if (cue.text.empty() || cue.text.size() > SubtitleLimits::kMaxCueBytes ||
            !is_utf8_text(cue.text)) {
            return invalid;
        }
        total += cue.text.size();
        if (total > SubtitleLimits::kMaxTotalBytes) return invalid;
    }
    std::stable_sort(track.cues.begin(), track.cues.end(),
                     [](const SubtitleCue& a, const SubtitleCue& b) { return a.start_ms < b.start_ms; });
    return track;
}

}  // namespace seekserve
