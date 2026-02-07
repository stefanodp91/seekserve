# Streaming Scheduler Policy

## Overview

The StreamingScheduler controls piece download priority using libtorrent's `set_piece_deadline()` API. It optimizes for low-latency streaming and fast seek response.

## Piece Priority Zones

When a Range request arrives, pieces are categorized into zones:

```
[--- hot window ---][--- lookahead ---][--- background ---]
     100-300ms           2000ms+           priority 1
```

### Hot Window
- **Size**: `hot_window_pieces` (default: 5) pieces around the current playhead
- **Deadline**: Short (100-300ms), increasing with distance from playhead
- **Purpose**: Ensure data is available for immediate playback

### Lookahead
- **Size**: `lookahead_pieces` (default: 20) pieces beyond the hot window
- **Deadline**: Longer (2000ms+), linearly increasing
- **Purpose**: Pre-fetch upcoming data to prevent stalls

### Background
- Remaining file pieces at priority 1 (lowest)
- Downloaded opportunistically for offline availability

## Seek Boost

When the playhead jumps by more than `hot_window_pieces`:

1. All existing deadlines are cleared
2. Hot window is temporarily expanded by `seek_boost_pieces` (default: 10)
3. Boost duration: `seek_boost_duration_ms` (default: 3000ms)
4. After boost expires, window returns to normal size

## Deadline Budget

Maximum concurrent deadline pieces: `deadline_budget` (default: 30). This prevents libtorrent from thrashing when too many pieces have deadlines.

## Stream Modes

The scheduler operates in three modes with automatic switching:

### Streaming-First (default)
- Aggressive deadlines on hot + lookahead
- Frequent reprioritization on every Range request
- Used when throughput is sufficient

### Download-Assist
- Larger buffers, less frequent reprioritization
- Triggered when `stall_count > stall_count_threshold` (default: 3)

### Download-First
- Pre-buffer before resuming streaming
- Triggered when sustained download rate < `min_sustained_rate` (default: 500 KB/s)
- Returns to streaming-first when `contiguous_bytes > min_contiguous_bytes` (default: 2 MB)

## Mode Switching Criteria

Evaluated every 1 second via `tick()`:

| From | To | Condition |
|------|----|-----------|
| streaming-first | download-assist | stall_count > threshold |
| download-assist | download-first | sustained_rate < min_rate |
| download-first | streaming-first | contiguous_bytes >= min_buffer AND rate OK |
| any | streaming-first | buffer health restored |

## Configuration

All parameters are in `SchedulerConfig`:

```cpp
struct SchedulerConfig {
    int hot_window_pieces = 5;
    int lookahead_pieces = 20;
    int seek_boost_pieces = 10;
    int deadline_budget = 30;
    int seek_boost_duration_ms = 3000;
    int64_t min_contiguous_bytes = 2 * 1024 * 1024;
    int stall_count_threshold = 3;
    double min_sustained_rate = 500'000;
};
```
