# P0 Static Callback Hotspots Verification

## Build Evidence

The optimized branch was verified in the `perf-dev` worktree with the cross-compilation toolchain and build commands provided by the user.

### Configure

```bash
/usr/bin/cmake \
  -DCMAKE_BUILD_TYPE:STRING=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE \
  -DCMAKE_TOOLCHAIN_FILE=/home/mouj/Projects/utilsCore/rk356x-toolchain.cmake \
  -DTOOLCHAIN_PATH=/home/mouj/rk3568/buildroot/output/rockchip_rk3568/host \
  -DUSE_CROSS_COMPILE=ON \
  -DBUILD_STATIC_UTILS=ON \
  --no-warn-unused-cli \
  -S /home/mouj/Projects/utilsCore/.worktree/perf-dev \
  -B /home/mouj/Projects/build/perf-dev
```

Observed result:
- configure completed successfully

### Build

```bash
/usr/bin/cmake --build /home/mouj/Projects/build/perf-dev --config Debug --target all -j 20 --
```

Observed result:
- `utils`
- `utils_static`
- `LoggerCfg_Demo`
- `LoggerV2_Demo`
- `ThreadPool_Scale_Demo`
- `RGA_DmaBuf_Demo`

all built successfully.

## Source-Level Checks

The following search was run after the refactor:

```bash
rg -n "std::function<void\\(FramePtr\\)>|std::function<void\\(int\\)>|ConcurrentQueue<std::function<void\\(\\)>>|try_enqueue\\(\\[this\\]" include src
```

Observed result:
- no matches

## Compatibility Notes

- Public function names remain unchanged.
- `CameraController::setFrameCallback(...)` still accepts normal lambdas and other callable objects.
- `Frame::setReleaseCallback(...)` keeps the same function name while now allowing statically bound member-function release handlers.
- `asyncThreadPool` keeps `enqueue(...)` and `try_enqueue(...)`; member-function overloads were added to let hot paths avoid capture-lambda wrappers.

## Manual Validation Handoff

Suggested manual checks:

1. Start the camera pipeline and confirm frame delivery still works with existing callback registration code.
2. Verify that buffers are returned correctly when frames go out of scope in both V4L2 and RGA paths.
3. Exercise the RGA processing path and confirm output frames are produced and released without deadlock or starvation.
4. Compare hot-path CPU usage before and after on the target device, focusing on:
   - camera capture thread
   - async thread-pool worker threads
   - RGA worker loop

## Remaining Caveat

This verification confirms cross-compilation success and removal of the targeted P0 erased-callback patterns. Runtime performance gain and behavioral correctness still require manual execution on the target system.
