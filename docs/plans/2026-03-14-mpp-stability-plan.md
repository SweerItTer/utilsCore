# MPP Stability Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Harden the MPP encoding path so long-running encode/write workloads stay stable under reset, external DMABUF input, and segmented stream writing.

**Architecture:** Keep the existing public encoder abstractions, but introduce a stricter core state machine, configurable packet polling windows, and safer stream writer flushing/switching semantics. The implementation keeps hot-path ownership in `MppEncoderCore`, pushes configuration validation into `MppEncoderContext`, and limits API growth to a few explicit timeout/retry knobs.

**Tech Stack:** C++14, Rockchip MPP, DMABUF, pthread/std::thread, CMake

---

### Task 1: Document the target design and API delta

**Files:**
- Modify: `docs/plans/2026-03-14-mpp-stability-plan.md`
- Create: `/home/mouj/Projects/EdgeVision/EdgeVision-app/docs/mpp-api-change-notes-2026-03-14.md`

**Step 1: Record the stability issues**

Describe the exact failure classes:
- reset races between `resetConfig()` and `workerThread()`
- external DMABUF metadata mismatch during frame import
- duplicated `releaseSlot()` free-queue insertion
- too-short encode polling windows for MPP packet retrieval
- `StreamWriter` stop/flush/fsync logic treating success as failure

**Step 2: Record the approved design**

Document:
- `MppEncoderCore` gets an explicit reset generation and worker quiesce handshake
- `MppEncoderContext::Config` grows retry/poll timeout knobs
- `StreamWriter` gets reliable shutdown, flush, fsync, and segment switching behavior
- `JpegEncoder` reuses the same polling model and adds better file-write validation

**Step 3: Record the public API delta**

In the EdgeVision app repo document:
- new config fields
- behavioral changes for reset/shutdown semantics
- any public API that still violates naming rules and must stay as `TODO(naming)`

### Task 2: Add explicit configuration and validation in `MppEncoderContext`

**Files:**
- Modify: `include/utils/mpp/encoderContext.h`
- Modify: `src/utils/mpp/encoderContext.cpp`

**Step 1: Extend the public config**

Add documented fields for:
- `packet_poll_retries`
- `packet_poll_interval_us`
- `packet_ready_timeout_us`

Keep names in lower camel case only if the API is already public and unsafe to rename; otherwise use the repo naming rule and mark legacy public names with `TODO(naming)`.

**Step 2: Add config validation helpers**

Validate width/height/stride/format/retry fields before touching MPP. Reject invalid values early with explicit error logs.

**Step 3: Make `applyConfig()` fail atomically**

Track each `set_cfg()` result and abort on the first hard failure instead of silently continuing in a partially applied state.

### Task 3: Rebuild `MppEncoderCore` reset and slot safety

**Files:**
- Modify: `include/utils/mpp/encoderCore.h`
- Modify: `src/utils/mpp/encoderCore.cpp`

**Step 1: Add a core state handshake**

Introduce:
- reset generation counter
- worker idle/active tracking
- a condition variable used to wait until the worker leaves the current encode step before tearing down slots/context

**Step 2: Harden slot ownership**

Make slot release idempotent:
- only specific source states may transition to `Writable`
- duplicate release must be ignored, not requeued
- external DMABUF/lifetime holder cleanup must happen exactly once

**Step 3: Fix external DMABUF frame import**

Use the actual selected DMABUF (`external_dmabuf` or internal slot buffer) for:
- width
- height
- stride
- format
- imported `MppBuffer`

**Step 4: Make packet polling configurable**

Use the config timeout knobs for the worker polling loop and exit early on shutdown/reset generation changes.

### Task 4: Fix `StreamWriter` correctness under sustained load

**Files:**
- Modify: `include/utils/mpp/streamWriter.h`
- Modify: `src/utils/mpp/streamWriter.cpp`

**Step 1: Fix shutdown semantics**

`stop()` must:
- stop accepting new meta
- wake all threads once
- drain already queued work
- join threads cleanly

**Step 2: Fix flush and sync handling**

Correct success/error checks for:
- `fflush`
- `fsync`
- `fileno`

**Step 3: Make segment switching deterministic**

Open the next file on the idle writer before swapping writers, flush/sync the writer that becomes idle, and protect writer selection with a dedicated mutex.

### Task 5: Reuse the same stability model in `JpegEncoder`

**Files:**
- Modify: `include/utils/mpp/jpegEncoder.h`
- Modify: `src/utils/mpp/jpegEncoder.cpp`

**Step 1: Improve public comments and parameter docs**

Document config semantics, expected DMABUF format assumptions, and output behavior.

**Step 2: Reuse config-driven packet polling**

Use the new timeout knobs from `MppEncoderContext::Config` for JPEG packet waits.

**Step 3: Validate output writes**

Check `fwrite`, `fflush`, `fclose`, and directory creation results consistently.

### Task 6: Naming audit and verification

**Files:**
- Modify: all touched MPP headers/sources as needed

**Step 1: Audit naming**

For touched code:
- rename private/non-public helpers to the agreed style directly
- leave public API mismatches only when renaming would break consumers
- add `TODO(naming)` comments where a legacy public name must stay

**Step 2: Build verification**

Run at least:
- native build for any non-MPP targets still affected
- RK cross build for `utils`

**Step 3: Test/CI verification**

Run all available project tests/ctest commands. If no tests exist, record that and treat successful builds as the verification evidence.
