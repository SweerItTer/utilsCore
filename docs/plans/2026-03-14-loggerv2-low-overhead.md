# LoggerV2 Low Overhead Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reduce `LoggerV2` hot-path overhead under high concurrency while preserving the current logging macros and the shortened `file:line` output style.

**Architecture:** Keep the public `LOG_*` call sites unchanged and optimize the backend. The fast path should do level gating, build a lightweight log record, and enqueue it with predictable cost; formatting, sink fan-out, and most string work should move behind the queue. Add explicit queue overflow accounting and make console output cheaper than file output by default.

**Tech Stack:** C++14, `LoggerV2`, `moodycamel::ConcurrentQueue`, CMake, native demo validation, cross-build validation

---

### Task 1: Baseline and scope lock

**Files:**
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/docs/plans/2026-03-14-loggerv2-low-overhead.md`
- Verify: `/home/mouj/Projects/utilsCore/.worktree/review/include/utils/logger_v2.h`
- Verify: `/home/mouj/Projects/utilsCore/.worktree/review/src/utils/logger_v2.cpp`

**Step 1: Record baseline constraints**

- Keep `LOG_TRACE/LOG_DEBUG/LOG_INFO/LOG_WARN/LOG_ERROR/LOG_FATAL` macro names unchanged.
- Keep current message body and source style: `file:line`.
- Do not change module call sites in this task unless required to compile.

**Step 2: Record optimization targets**

- Lower caller-thread formatting cost.
- Bound queue overflow behavior and count drops.
- Keep console sink readable while trimming formatting cost.

**Step 3: Commit checkpoint**

```bash
git -C /home/mouj/Projects/utilsCore/.worktree/review add docs/plans/2026-03-14-loggerv2-low-overhead.md
git -C /home/mouj/Projects/utilsCore/.worktree/review commit -m "docs: add loggerv2 performance plan"
```

### Task 2: Extend logger data model

**Files:**
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/include/utils/logger_v2.h`
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/src/utils/logger_v2.cpp`

**Step 1: Add queue and drop statistics**

- Extend `AsyncLogQueue` with counters:
  - total pushed
  - total dropped
  - dropped by level
- Add query helpers on `LoggerV2` for reading these counters.

**Step 2: Add overflow policy**

- Add a small enum for queue-full behavior:
  - `Block`
  - `DropNewest`
  - `DropIfBelowError`
- Make default behavior preserve `ERROR/FATAL` traffic and allow dropping low-priority traffic.

**Step 3: Move source-path shortening into reusable helper**

- Keep the `file:line` behavior but centralize it so both console and file formatting share the same short-path rule.

**Step 4: Build validation**

Run:

```bash
cmake --build /tmp/utilsCore-review-build --target utils -j6
```

Expected: `Built target utils`

**Step 5: Commit checkpoint**

```bash
git -C /home/mouj/Projects/utilsCore/.worktree/review add include/utils/logger_v2.h src/utils/logger_v2.cpp
git -C /home/mouj/Projects/utilsCore/.worktree/review commit -m "feat: add loggerv2 queue statistics"
```

### Task 3: Reduce caller-thread formatting cost

**Files:**
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/include/utils/logger_v2.h`
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/src/utils/logger_v2.cpp`

**Step 1: Split record capture from sink formatting**

- Keep message-body formatting in the caller thread for now, but move full pattern expansion and source rendering exclusively to sink-side formatting.
- Avoid repeated path processing and avoid sink-side copying where not needed.

**Step 2: Add small fast-path guards**

- Return before any message construction when:
  - level is below global level
  - logger is off
  - there are no sinks

**Step 3: Reduce console formatting cost**

- Keep console pattern compact by default.
- Do not include function name unless a custom pattern explicitly requests it.

**Step 4: Build validation**

Run:

```bash
cmake --build /tmp/utilsCore-review-build --target utils -j6
cmake -S /home/mouj/Projects/utilsCore/.worktree/review -B /tmp/utilsCore-net-native-logv2 -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/utilsCore-net-native-logv2 --target Net_Http_Demo -j6
```

Expected: both builds succeed

**Step 5: Commit checkpoint**

```bash
git -C /home/mouj/Projects/utilsCore/.worktree/review add include/utils/logger_v2.h src/utils/logger_v2.cpp
git -C /home/mouj/Projects/utilsCore/.worktree/review commit -m "perf: streamline loggerv2 fast path"
```

### Task 4: Add explicit overflow handling

**Files:**
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/include/utils/logger_v2.h`
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/src/utils/logger_v2.cpp`
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/include/utils/logger_config.h`
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/src/utils/logger_config.cpp`

**Step 1: Add config fields**

- Add config for:
  - queue overflow policy
  - whether low-priority logs may be dropped
  - queue stats reporting interval (optional)

**Step 2: Implement overflow decisions**

- On queue push failure or full condition:
  - keep `ERROR/FATAL` if policy allows
  - drop lower levels first
  - count the drop

**Step 3: Avoid noisy self-logging loops**

- Do not recursively log from inside logger-internal overflow handling.
- Keep overflow telemetry queryable rather than continuously printed.

**Step 4: Build validation**

Run:

```bash
cmake --build /tmp/utilsCore-review-build --target utils -j6
```

Expected: `Built target utils`

**Step 5: Commit checkpoint**

```bash
git -C /home/mouj/Projects/utilsCore/.worktree/review add include/utils/logger_v2.h src/utils/logger_v2.cpp include/utils/logger_config.h src/utils/logger_config.cpp
git -C /home/mouj/Projects/utilsCore/.worktree/review commit -m "feat: add loggerv2 overflow control"
```

### Task 5: Add lightweight validation and smoke tests

**Files:**
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/examples/logger_config_demo.cpp`
- Modify: `/home/mouj/Projects/utilsCore/.worktree/review/examples/tcpServer_example.cpp`

**Step 1: Add queue stats visibility to demo**

- Print queue size and dropped counters in the logger demo.
- Keep the output concise and developer-facing.

**Step 2: Verify net demo still uses short source format**

- Re-run `Net_Http_Demo` and confirm log prefix format remains:
  - timestamp
  - level
  - thread id
  - `file:line`
  - message

**Step 3: Native validation**

Run:

```bash
cmake --build /tmp/utilsCore-net-native-logv2 --target Net_Http_Demo LoggerCfg_Demo -j6
```

Expected: both targets build successfully

**Step 4: Commit checkpoint**

```bash
git -C /home/mouj/Projects/utilsCore/.worktree/review add examples/logger_config_demo.cpp examples/tcpServer_example.cpp
git -C /home/mouj/Projects/utilsCore/.worktree/review commit -m "test: expose loggerv2 queue telemetry"
```

### Task 6: Review and integration

**Files:**
- Modify: `/home/mouj/Projects/EdgeVision/EdgeVision-app/third_party/utils`
- Create: `/home/mouj/Projects/EdgeVision/EdgeVision-app/docs/loggerv2-low-overhead-notes-2026-03-14.md`

**Step 1: Run final verification**

Run:

```bash
cmake --build /tmp/utilsCore-review-build --target utils -j6
cmake --build /tmp/utilsCore-net-native-logv2 --target Net_Http_Demo LoggerCfg_Demo -j6
cmake --build /home/mouj/Projects/EdgeVision/EdgeVision-app/build-rk --target EdgeVision -j6
```

Expected: all builds succeed

**Step 2: Manual smoke test**

Run:

```bash
/tmp/utilsCore-net-native-logv2/examples/Net_Http_Demo
curl http://127.0.0.1:18080/api/ping
```

Expected: request succeeds and log prefix uses `file:line` short source format

**Step 3: Code review pass**

- Review for:
  - recursive logger-internal logging
  - new blocking behavior in hot paths
  - incorrect drop handling for `ERROR/FATAL`
  - naming consistency

**Step 4: Update main repo pointer and notes**

```bash
git -C /home/mouj/Projects/EdgeVision/EdgeVision-app add third_party/utils docs/loggerv2-low-overhead-notes-2026-03-14.md
git -C /home/mouj/Projects/EdgeVision/EdgeVision-app commit -m "docs: record loggerv2 low-overhead changes"
```

**Step 5: Push**

```bash
git -C /home/mouj/Projects/utilsCore/.worktree/review push origin perf/loggerv2-low-overhead
git -C /home/mouj/Projects/EdgeVision/EdgeVision-app push origin feature/ui-static-cache-overlay
```
