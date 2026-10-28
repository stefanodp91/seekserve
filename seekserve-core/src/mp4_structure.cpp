#include "seekserve/mp4_structure.hpp"

#include <algorithm>
#include <optional>

namespace seekserve {

namespace {

// A film's moov is a few MB: two hours have about half a million samples.
constexpr std::int64_t kMaxMoov = 32 * 1024 * 1024;
constexpr int kMaxTopBoxes = 1024;
constexpr std::size_t kMaxTracks = 32;
constexpr std::uint64_t kMaxSamples = 4000000;
constexpr std::int64_t kNsPerSecond = 1000000000;

constexpr std::uint32_t fourcc(const char (&s)[5]) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[0])) << 24 |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[1])) << 16 |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[2])) << 8 |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(s[3]));
}

// The bytes of the moov, read big-endian; a read past the end gives zeros
// and marks the buffer broken.
class Bytes {
public:
    explicit Bytes(std::vector<std::uint8_t> b) : b_(std::move(b)) {}

    std::size_t size() const { return b_.size(); }
    bool broken() const { return broken_; }
    const std::uint8_t* at(std::size_t p) const { return b_.data() + p; }

    std::uint64_t be(std::size_t p, int n) const {
        if (p + n > b_.size()) {
            broken_ = true;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < n; ++i) v = v << 8 | b_[p + i];
        return v;
    }
    std::uint32_t u8(std::size_t p) const { return static_cast<std::uint32_t>(be(p, 1)); }
    std::uint32_t u16(std::size_t p) const { return static_cast<std::uint32_t>(be(p, 2)); }
    std::uint32_t u32(std::size_t p) const { return static_cast<std::uint32_t>(be(p, 4)); }
    std::uint64_t u64(std::size_t p) const { return be(p, 8); }

private:
    std::vector<std::uint8_t> b_;
    mutable bool broken_ = false;
};

struct Box {
    std::uint32_t type = 0;
    std::size_t data = 0;  // its payload
    std::size_t end = 0;   // one past it
};

// The boxes in [from, to); nullopt when one does not fit.
std::optional<std::vector<Box>> children(const Bytes& b, std::size_t from, std::size_t to) {
    std::vector<Box> out;
    for (std::size_t p = from; p + 8 <= to;) {
        std::uint64_t size = b.u32(p);
        std::size_t head = 8;
        if (size == 1) {
            size = b.u64(p + 8);
            head = 16;
        } else if (size == 0) {
            size = to - p;
        }
        if (size < head || size > to - p) return std::nullopt;
        out.push_back({b.u32(p + 4), p + head, p + static_cast<std::size_t>(size)});
        p += static_cast<std::size_t>(size);
    }
    return out;
}

std::optional<Box> child(const Bytes& b, const Box& parent, std::uint32_t type) {
    auto kids = children(b, parent.data, parent.end);
    if (!kids) return std::nullopt;
    for (const auto& k : *kids) {
        if (k.type == type) return k;
    }
    return std::nullopt;
}

std::optional<Box> path(const Bytes& b, Box box, std::initializer_list<std::uint32_t> types) {
    for (auto t : types) {
        auto next = child(b, box, t);
        if (!next) return std::nullopt;
        box = *next;
    }
    return box;
}

class TrackReader {
public:
    TrackReader(const Bytes& b, std::uint32_t movie_timescale, std::int64_t file_size, std::string& why)
        : b_(b), movie_timescale_(movie_timescale), file_size_(file_size), why_(why) {}

    // The track of `trak`; nullopt with an empty `why` for a track that is
    // neither video nor audio, which the MKV leaves out.
    std::optional<Mp4Track> read(const Box& trak);

private:
    bool fail(const char* reason) {
        why_ = reason;
        return false;
    }
    bool description(const Box& stsd, Mp4Track& t);
    bool video(const Box& entry, Mp4Track& t);
    bool audio(const Box& entry, Mp4Track& t);
    bool esds(const Box& box, Mp4Track& t);
    bool tables(const Box& stbl, Mp4Track& t);
    bool edits(const Box& trak, Mp4Track& t);

    // The entries of a full box with a 32-bit count at `count_at`, each
    // `entry` bytes; false when they do not fit or are too many.
    bool entries(const Box& box, std::size_t count_at, std::size_t entry, std::uint32_t& n) {
        n = b_.u32(count_at);
        return n <= kMaxSamples && count_at + 4 + static_cast<std::uint64_t>(n) * entry <= box.end;
    }

    const Bytes& b_;
    const std::uint32_t movie_timescale_;
    const std::int64_t file_size_;
    std::string& why_;
};

std::optional<Mp4Track> TrackReader::read(const Box& trak) {
    Mp4Track t;
    auto tkhd = child(b_, trak, fourcc("tkhd"));
    auto mdhd = path(b_, trak, {fourcc("mdia"), fourcc("mdhd")});
    auto hdlr = path(b_, trak, {fourcc("mdia"), fourcc("hdlr")});
    auto stbl = path(b_, trak, {fourcc("mdia"), fourcc("minf"), fourcc("stbl")});
    if (!tkhd || !mdhd || !hdlr || !stbl) return fail("a malformed track"), std::nullopt;

    auto handler = b_.u32(hdlr->data + 8);
    if (handler != fourcc("vide") && handler != fourcc("soun")) return std::nullopt;
    t.video = handler == fourcc("vide");
    t.id = b_.u32(tkhd->data + (b_.u8(tkhd->data) == 1 ? 20 : 12));
    bool v1 = b_.u8(mdhd->data) == 1;
    t.timescale = b_.u32(mdhd->data + (v1 ? 20 : 12));
    auto lang = b_.u16(mdhd->data + (v1 ? 32 : 20));
    if (t.timescale == 0) return fail("a malformed track"), std::nullopt;
    t.language = "und";
    if (lang != 0 && lang != 0x7FFF) {
        t.language.clear();
        for (int shift : {10, 5, 0}) t.language.push_back(static_cast<char>(0x60 + ((lang >> shift) & 0x1F)));
    }

    auto stsd = child(b_, *stbl, fourcc("stsd"));
    if (!stsd) return fail("a malformed track"), std::nullopt;
    if (!description(*stsd, t) || !tables(*stbl, t) || !edits(trak, t)) return std::nullopt;
    if (b_.broken()) return fail("a malformed track"), std::nullopt;
    return t;
}

bool TrackReader::description(const Box& stsd, Mp4Track& t) {
    if (b_.u32(stsd.data + 4) != 1) return fail("a track with more than one sample description");
    auto list = children(b_, stsd.data + 8, stsd.end);
    if (!list || list->size() != 1) return fail("a malformed track");
    return t.video ? video(list->front(), t) : audio(list->front(), t);
}

bool TrackReader::video(const Box& entry, Mp4Track& t) {
    std::uint32_t config = 0;
    if (entry.type == fourcc("avc1") || entry.type == fourcc("avc3")) {
        t.codec = Mp4Codec::avc;
        config = fourcc("avcC");
    } else if (entry.type == fourcc("hvc1") || entry.type == fourcc("hev1")) {
        t.codec = Mp4Codec::hevc;
        config = fourcc("hvcC");
    } else {
        return fail("a video codec Matroska cannot take as it is");
    }
    t.width = b_.u16(entry.data + 24);
    t.height = b_.u16(entry.data + 26);
    auto kids = children(b_, entry.data + 78, entry.end);
    if (!kids) return fail("a malformed track");
    for (const auto& k : *kids) {
        if (k.type == config) t.codec_private.assign(b_.at(k.data), b_.at(k.end));
    }
    if (t.codec_private.empty()) return fail("a video track without its decoder configuration");
    return true;
}

bool TrackReader::audio(const Box& entry, Mp4Track& t) {
    auto version = b_.u16(entry.data + 8);
    if (version > 1) return fail("an audio description of version 2");
    t.channels = b_.u16(entry.data + 16);
    t.sample_rate = b_.u32(entry.data + 24) >> 16;
    if (entry.type == fourcc("ac-3")) return t.codec = Mp4Codec::ac3, true;
    if (entry.type == fourcc("ec-3")) return t.codec = Mp4Codec::eac3, true;
    if (entry.type == fourcc(".mp3")) return t.codec = Mp4Codec::mp3, true;
    if (entry.type != fourcc("mp4a")) return fail("an audio codec Matroska cannot take as it is");
    // The esds, in the entry or, in QuickTime files, in its wave box.
    Box fields{entry.type, entry.data + (version == 1 ? 44 : 28), entry.end};
    if (fields.data > fields.end) return fail("a malformed track");
    auto found = child(b_, fields, fourcc("esds"));
    if (!found) {
        if (auto wave = child(b_, fields, fourcc("wave"))) found = child(b_, *wave, fourcc("esds"));
    }
    if (!found) return fail("an AAC track without its decoder configuration");
    return esds(*found, t);
}

// The MPEG-4 descriptors of an esds: the object type and the decoder's
// configuration (ISO/IEC 14496-1).
bool TrackReader::esds(const Box& box, Mp4Track& t) {
    std::size_t p = box.data + 4;
    auto descriptor = [&](std::uint32_t tag, std::size_t& length) {
        if (p >= box.end || b_.u8(p) != tag) return false;
        ++p;
        length = 0;
        for (int i = 0; i < 4 && p < box.end; ++i) {
            auto c = b_.u8(p++);
            length = length << 7 | (c & 0x7F);
            if (!(c & 0x80)) return p + length <= box.end;
        }
        return false;
    };
    std::size_t length = 0;
    if (!descriptor(0x03, length)) return fail("a malformed esds");
    auto flags = b_.u8(p + 2);
    p += 3;
    if (flags & 0x80) p += 2;
    if (flags & 0x40) p += 1 + b_.u8(p);
    if (flags & 0x20) p += 2;
    if (!descriptor(0x04, length)) return fail("a malformed esds");
    auto object = b_.u8(p);
    p += 13;
    if (object == 0x69 || object == 0x6B) return t.codec = Mp4Codec::mp3, true;
    if (object != 0x40 && object != 0x66 && object != 0x67 && object != 0x68) {
        return fail("an audio codec Matroska cannot take as it is");
    }
    t.codec = Mp4Codec::aac;
    if (!descriptor(0x05, length) || length == 0) return fail("an AAC track without its decoder configuration");
    t.codec_private.assign(b_.at(p), b_.at(p + length));
    return true;
}

bool TrackReader::tables(const Box& stbl, Mp4Track& t) {
    auto stts = child(b_, stbl, fourcc("stts"));
    auto stsc = child(b_, stbl, fourcc("stsc"));
    auto stsz = child(b_, stbl, fourcc("stsz"));
    auto stco = child(b_, stbl, fourcc("stco"));
    auto co64 = child(b_, stbl, fourcc("co64"));
    if (!stts || !stsc || !stsz || !(stco || co64)) return fail("a track without its sample tables");
    std::uint32_t n = 0;

    if (!entries(*stts, stts->data + 4, 8, n)) return fail("malformed or too large sample tables");
    for (std::uint32_t i = 0; i < n; ++i) {
        t.time_to_sample.emplace_back(b_.u32(stts->data + 8 + 8 * i), b_.u32(stts->data + 12 + 8 * i));
    }
    if (auto ctts = child(b_, stbl, fourcc("ctts"))) {
        if (!entries(*ctts, ctts->data + 4, 8, n)) return fail("malformed or too large sample tables");
        for (std::uint32_t i = 0; i < n; ++i) {
            t.composition.emplace_back(b_.u32(ctts->data + 8 + 8 * i),
                                       static_cast<std::int32_t>(b_.u32(ctts->data + 12 + 8 * i)));
        }
    }
    if (!entries(*stsc, stsc->data + 4, 12, n)) return fail("malformed or too large sample tables");
    for (std::uint32_t i = 0; i < n; ++i) {
        auto first = b_.u32(stsc->data + 8 + 12 * i);
        if (first == 0) return fail("sample tables that disagree");
        t.chunk_runs.push_back({first - 1, b_.u32(stsc->data + 12 + 12 * i)});
    }
    t.fixed_size = b_.u32(stsz->data + 4);
    t.sample_count = b_.u32(stsz->data + 8);
    if (t.sample_count == 0 || t.sample_count > kMaxSamples) return fail("malformed or too large sample tables");
    if (t.fixed_size == 0) {
        if (!entries(*stsz, stsz->data + 8, 4, n)) return fail("malformed or too large sample tables");
        for (std::uint32_t i = 0; i < n; ++i) t.sizes.push_back(b_.u32(stsz->data + 12 + 4 * i));
    }
    const auto& co = stco ? *stco : *co64;
    std::size_t width = stco ? 4 : 8;
    if (!entries(co, co.data + 4, width, n)) return fail("malformed or too large sample tables");
    for (std::uint32_t i = 0; i < n; ++i) {
        auto at = b_.be(co.data + 8 + width * i, static_cast<int>(width));
        // A chunk past the end of the file: the cursor then never adds a
        // sample's size to an offset near the top of int64.
        if (at > static_cast<std::uint64_t>(file_size_)) return fail("a sample outside the file");
        t.chunk_offsets.push_back(static_cast<std::int64_t>(at));
    }
    if (auto stss = child(b_, stbl, fourcc("stss"))) {
        if (!entries(*stss, stss->data + 4, 4, n)) return fail("malformed or too large sample tables");
        t.all_sync = false;
        for (std::uint32_t i = 0; i < n; ++i) {
            auto k = b_.u32(stss->data + 8 + 4 * i);
            if (k == 0 || k > t.sample_count || (!t.sync.empty() && k - 1 <= t.sync.back())) {
                return fail("sample tables that disagree");
            }
            t.sync.push_back(k - 1);
        }
    }

    // Every table speaks of the same samples.
    if (!t.sizes.empty() && t.sizes.size() != t.sample_count) return fail("sample tables that disagree");
    std::uint64_t timed = 0, composed = 0, chunked = 0;
    for (const auto& [count, delta] : t.time_to_sample) timed += count;
    for (const auto& [count, offset] : t.composition) composed += count;
    for (std::size_t i = 0; i < t.chunk_runs.size(); ++i) {
        auto first = t.chunk_runs[i].first_chunk;
        auto last = i + 1 < t.chunk_runs.size() ? t.chunk_runs[i + 1].first_chunk : t.chunk_offsets.size();
        if ((i == 0 && first != 0) || last <= first || last > t.chunk_offsets.size() ||
            t.chunk_runs[i].samples_per_chunk == 0) {
            return fail("sample tables that disagree");
        }
        chunked += static_cast<std::uint64_t>(last - first) * t.chunk_runs[i].samples_per_chunk;
    }
    if (timed != t.sample_count || composed > t.sample_count || chunked < t.sample_count) {
        return fail("sample tables that disagree");
    }
    return true;
}

// The edit list: empty edits (a delay), then at most one part of the media
// at its normal speed.
bool TrackReader::edits(const Box& trak, Mp4Track& t) {
    auto elst = path(b_, trak, {fourcc("edts"), fourcc("elst")});
    if (!elst) return true;
    bool v1 = b_.u8(elst->data) == 1;
    std::size_t step = v1 ? 20 : 12;
    std::uint32_t n = 0;
    if (!entries(*elst, elst->data + 4, step, n)) return fail("a malformed edit list");
    std::int64_t delay = 0;
    bool media = false;
    for (std::uint32_t i = 0; i < n; ++i) {
        auto p = elst->data + 8 + step * i;
        auto duration = static_cast<std::int64_t>(v1 ? b_.u64(p) : b_.u32(p));
        auto time = v1 ? static_cast<std::int64_t>(b_.u64(p + 8)) : static_cast<std::int32_t>(b_.u32(p + 4));
        auto rate = b_.u32(p + (v1 ? 16 : 8));
        if (media) return fail("an edit list with more than one part");
        if (time == -1) {
            if (duration < 0 || delay > (std::int64_t{1} << 40)) return fail("a malformed edit list");
            delay += duration;
            continue;
        }
        if (time < 0 || rate != 0x00010000) return fail("an edit list with more than one part");
        t.media_start = time;
        media = true;
    }
    t.delay_ns = static_cast<std::int64_t>(static_cast<__int128>(delay) * kNsPerSecond / movie_timescale_);
    return true;
}

}  // namespace

Result<Mp4Structure> read_mp4_structure(const Mp4Read& read, std::int64_t file_size, std::string& why) {
    auto fail = [&](const char* reason) -> Result<Mp4Structure> {
        why = reason;
        return make_error_code(errc::invalid_argument);
    };
    auto get = [&](std::int64_t offset, std::int64_t len) -> Result<std::vector<std::uint8_t>> {
        auto got = read(offset, len);
        if (got && static_cast<std::int64_t>(got.value().size()) < len) {
            why = "the file ended early";
            return make_error_code(errc::invalid_argument);
        }
        return got;
    };

    // The top-level boxes, by their heads only, up to the moov.
    Mp4Structure s;
    s.file_size = file_size;
    std::int64_t pos = 0;
    for (int n = 0; pos < file_size; ++n) {
        if (n >= kMaxTopBoxes) return fail("too many boxes");
        if (file_size - pos < 8) return fail("a box past the end of the file");
        auto head = get(pos, std::min<std::int64_t>(16, file_size - pos));
        if (!head) return head.error();
        Bytes h(std::move(head).value());
        std::uint64_t size = h.u32(0);
        auto type = h.u32(4);
        std::int64_t head_len = 8;
        if (n == 0 && type != fourcc("ftyp") && type != fourcc("moov") && type != fourcc("mdat") &&
            type != fourcc("free") && type != fourcc("skip") && type != fourcc("wide")) {
            return fail("not an MP4 file");
        }
        if (size == 1) {
            size = h.u64(8);
            head_len = 16;
            if (h.broken()) return fail("a box past the end of the file");
        } else if (size == 0) {
            size = static_cast<std::uint64_t>(file_size - pos);
        }
        if (size < static_cast<std::uint64_t>(head_len)) return fail("a malformed box");
        if (size > static_cast<std::uint64_t>(file_size - pos)) return fail("a box past the end of the file");
        if (type == fourcc("moof")) return fail("a fragmented MP4");
        if (type == fourcc("moov")) {
            if (s.moov_end != 0) return fail("two moov");
            if (static_cast<std::int64_t>(size) > kMaxMoov) return fail("a moov too large to rebuild");
            s.moov_start = pos;
            s.moov_end = pos + static_cast<std::int64_t>(size);
        }
        pos += static_cast<std::int64_t>(size);
    }
    if (s.moov_end == 0) return fail("no moov");

    auto bytes = get(s.moov_start, s.moov_end - s.moov_start);
    if (!bytes) return bytes.error();
    Bytes moov(std::move(bytes).value());
    auto top = children(moov, 0, moov.size());
    if (!top || top->size() != 1) return fail("a malformed moov");
    const Box root = top->front();
    if (child(moov, root, fourcc("mvex"))) return fail("a fragmented MP4");
    auto mvhd = child(moov, root, fourcc("mvhd"));
    if (!mvhd) return fail("a malformed moov");
    s.movie_timescale = moov.u32(mvhd->data + (moov.u8(mvhd->data) == 1 ? 20 : 12));
    if (s.movie_timescale == 0 || moov.broken()) return fail("a malformed moov");

    auto kids = children(moov, root.data, root.end);
    if (!kids) return fail("a malformed moov");
    std::size_t traks = 0;
    bool has_video = false;
    for (const auto& k : *kids) {
        if (k.type != fourcc("trak")) continue;
        if (++traks > kMaxTracks) return fail("too many tracks");
        why.clear();
        auto track = TrackReader(moov, s.movie_timescale, file_size, why).read(k);
        if (!track) {
            if (!why.empty()) return make_error_code(errc::invalid_argument);
            continue;
        }
        has_video = has_video || track->video;
        s.tracks.push_back(std::move(*track));
    }
    if (!has_video) return fail("no video track");

    // Every sample inside the file, before any layout uses them.
    for (const auto& t : s.tracks) {
        Mp4SampleCursor c(t);
        Mp4Sample x;
        std::uint64_t seen = 0;
        while (c.next(x)) {
            // Compared without a sum, which a hostile offset near the top of
            // int64 would wrap past the check.
            if (x.offset < 0 || x.offset > file_size || static_cast<std::int64_t>(x.size) > file_size - x.offset) {
                return fail("a sample outside the file");
            }
            ++seen;
        }
        if (seen != t.sample_count) return fail("sample tables that disagree");
    }
    return s;
}

}  // namespace seekserve
