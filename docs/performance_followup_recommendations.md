# Performance Follow-Up Recommendations

## Ranked Findings

### 1. Mouse watcher spawns detached threads per event

Files:
- `src/utils/mouse/watcher.cpp:224`
- `src/utils/mouse/watcher.cpp:391`
- `src/utils/mouse/watcher.cpp:400`
- `src/utils/mouse/watcher.cpp:652`

Why it matters:
- creating and detaching a thread per input event is far more expensive than callback dispatch itself
- this can amplify latency jitter and scheduler overhead under bursty input

Recommendation:
- replace per-event detached threads with a fixed worker queue or shared event executor
- if ordering matters, use one dedicated dispatch thread per watcher instance

Priority:
- implement next

### 2. DRM property access uses string-keyed `unordered_map` with `std::function`

Files:
- `include/utils/drm/drmLayer.h:72`
- `include/utils/drm/drmLayer.h:84`
- `include/utils/drm/drmLayer.h:111`
- `include/utils/drm/drmLayer.h:112`

Why it matters:
- repeated string hashing and erased callback dispatch on property get/set can become expensive if called in render/update loops

Recommendation:
- pre-resolve frequently used property names to enum or integer ids
- replace hot-path setter/getter maps with table-driven static dispatch for known properties

Priority:
- benchmark first

### 3. Udev monitor fanout still routes callbacks through generic wrappers

Files:
- `src/utils/udevMonitor.cpp:290`
- `include/utils/udevMonitor.h:31`
- `include/utils/udevMonitor.h:64`

Why it matters:
- callback predicate + callback dispatch are both erased
- this is probably not per-frame hot, but it can become noisy under rapid device churn

Recommendation:
- leave the public API as-is
- if profiling shows pressure, add internal static callback slots similar to the P0 approach

Priority:
- benchmark first

### 4. Fence watcher uses erased callbacks in an event loop

Files:
- `include/utils/fenceWatcher.h:22`
- `include/utils/fenceWatcher.h:85`

Why it matters:
- callback dispatch happens inside a long-lived watcher loop
- frequency depends on fence traffic and may be meaningful on graphics-heavy paths

Recommendation:
- switch callback storage to the same static slot pattern used in this branch
- keep `watchFence(...)` function name unchanged

Priority:
- benchmark first

### 5. RGA worker loop uses fixed sleeps for pacing

Files:
- `src/utils/rga/rgaProcessor.cpp:573`
- `src/utils/rga/rgaProcessor.cpp:577`

Why it matters:
- fixed sleeps trade throughput for simplicity and can create avoidable latency or idle gaps

Recommendation:
- replace unconditional sleeps with queue-aware waiting or a lighter adaptive backoff
- measure end-to-end latency before and after

Priority:
- implement next

### 6. Logger v2 worker loop uses polling sleep

Files:
- `src/utils/logger_v2.cpp:323`

Why it matters:
- low but persistent background wakeups waste CPU on quiet systems

Recommendation:
- move to condition-variable or eventfd wakeup if message rate or idle power matters

Priority:
- defer unless profiling confirms impact

### 7. V4L2 parameter processor has repeated sleep-based polling

Files:
- `src/utils/v4l2param/paramProcessor.cpp:79`
- `src/utils/v4l2param/paramProcessor.cpp:104`
- `src/utils/v4l2param/paramProcessor.cpp:120`

Why it matters:
- repeated fixed sleeps can inflate control-path latency and burn CPU in management threads

Recommendation:
- replace polling with event-driven signaling where practical

Priority:
- benchmark first

### 8. `objectsPool` still exposes `std::function<T()>` factory hooks

Files:
- `include/utils/objectsPool.h:18`

Why it matters:
- likely cold-path initialization cost, not a frame-path bottleneck

Recommendation:
- leave it alone unless object creation shows up in profiling

Priority:
- defer

## Summary

The next two changes with the highest likely return are:

1. Remove detached-thread-per-event dispatch from the mouse watcher.
2. Replace fixed sleeps in the RGA worker loop with queue-aware waiting or adaptive backoff.

The remaining `std::function` users should be prioritized by trigger frequency, not by style alone. Low-frequency configuration callbacks should not be optimized ahead of thread-creation and polling hotspots.
