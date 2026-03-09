# Project Performance Audit Slice Plan

## Goal

Split the repository-wide performance audit into independent module slices so each slice can be reviewed in isolation with minimal context pollution.

## Slice Allocation

### Slice A: V4L2 and frame lifecycle

Scope:
- `include/utils/v4l2/*`
- `src/utils/v4l2/*`
- `include/utils/types.h`

Focus:
- frame ownership churn
- release-path overhead
- capture-loop waits and sleeps
- queue handoff costs

### Slice B: async infrastructure

Scope:
- `include/utils/asyncThreadPool.h`
- `src/utils/asyncThreadPool.cpp`
- `include/utils/safeQueue.h`
- `include/utils/orderedQueue.h`
- `include/utils/objectsPool.h`

Focus:
- queue contention
- dynamic scaling overhead
- polling or backoff costs
- hidden allocations in task handoff

### Slice C: graphics pipeline

Scope:
- `include/utils/rga/*`
- `src/utils/rga/*`
- `include/utils/drm/*`
- `src/utils/drm/*`

Focus:
- RGA worker pacing
- pending future queue behavior
- property lookup and callback dispatch
- resource refresh callback fanout

### Slice D: event and device monitors

Scope:
- `include/utils/mouse/*`
- `src/utils/mouse/*`
- `include/utils/udevMonitor.h`
- `src/utils/udevMonitor.cpp`
- `include/utils/fenceWatcher.h`
- `include/utils/v4l2param/*`
- `src/utils/v4l2param/*`

Focus:
- detached-thread-per-event patterns
- predicate and callback fanout
- polling sleeps
- callback wrapper overhead

### Slice E: logging, encoding, and utilities

Scope:
- `include/utils/logger_config.h`
- `src/utils/logger_config.cpp`
- `src/utils/logger_v2.cpp`
- `include/utils/mpp/*`
- `src/utils/mpp/*`
- `include/utils/sys/*`

Focus:
- queue wakeup strategy
- idle sleeps
- thread handoff overhead
- configuration callback hotness

## Required Output Template Per Slice

Each slice should report:

1. Hotspot location
2. Trigger frequency
3. Why it may be expensive
4. Whether the claim is measured or inferred
5. Evidence source
6. Suggested change
7. Expected benefit
8. Regression risk
9. Priority:
   - implement next
   - benchmark first
   - defer

## Rules

- Audit first, no opportunistic broad refactors.
- Keep function names stable in recommendations unless a rename is unavoidable.
- Distinguish throughput problems from latency problems.
- Explicitly call out busy-wait and detached-thread patterns.
- Tag low-frequency `std::function` usage as low priority instead of over-optimizing it.
