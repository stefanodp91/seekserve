#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "seekserve/error.hpp"

namespace seekserve {

/// One line of a subtitle file, already read by the app: from `start_ms` to
/// `end_ms` of the video, UTF-8 text (app WORK-37 step 7).
struct SubtitleCue {
    std::int64_t start_ms = 0;
    std::int64_t end_ms = 0;
    std::string text;
};

/// The subtitles a TV share puts into the video it serves, as a text track
/// (`S_TEXT/UTF8`) that is the default one: a TV that reads only the
/// subtitles inside the file then shows them (app DEC-618).
struct SubtitleTrack {
    std::string language;  // ISO 639-2, three letters; empty for "und"
    std::string name;
    std::vector<SubtitleCue> cues;
};

/// The limits of a subtitle track: a film's file has a few thousand lines.
struct SubtitleLimits {
    static constexpr std::size_t kMaxCues = 20000;
    static constexpr std::size_t kMaxCueBytes = 2048;
    static constexpr std::size_t kMaxTotalBytes = 4 * 1024 * 1024;
    static constexpr std::size_t kMaxNameBytes = 128;
    static constexpr std::int64_t kMaxTimeMs = 48LL * 3600 * 1000;
};

// `track` with its lines in order of start, once checked: no line, a line
// with no text, an end not after its start, a time out of range, text that
// is not UTF-8 or holds a NUL, a language that is not three ASCII letters,
// or anything over SubtitleLimits are errc::invalid_argument. An empty
// language becomes "und".
Result<SubtitleTrack> checked_subtitle_track(SubtitleTrack track);

// Whether `text` is well-formed UTF-8 with no NUL.
bool is_utf8_text(const std::string& text);

}  // namespace seekserve
