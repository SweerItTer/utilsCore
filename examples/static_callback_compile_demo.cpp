#include <future>
#include <memory>
#include <type_traits>

#include "asyncThreadPool.h"
#include "types.h"
#include "v4l2/cameraController.h"
#include "v4l2/frame.h"

namespace {

struct ReleaseOwner {
    void release(int) {}
};

struct TaskOwner {
    int run() { return 7; }
    int runConst() const { return 9; }
};

template <typename Callback>
CameraController::FrameCallback make_frame_callback(Callback&& cb) {
    return CameraController::FrameCallback(std::forward<Callback>(cb));
}

template <typename Callback>
Frame::ReleaseCallback make_release_callback(Callback&& cb) {
    return Frame::ReleaseCallback(std::forward<Callback>(cb));
}

} // namespace

int main() {
    auto frameLambda = [](FramePtr) {};
    auto frameCallback = make_frame_callback(frameLambda);
    static_assert(std::is_same<decltype(frameCallback), CameraController::FrameCallback>::value,
                  "Frame callback should still accept normal lambdas");

    Frame frame;
    frame.setReleaseCallback([](int) {});

    ReleaseOwner releaseOwner;
    frame.setReleaseCallback<ReleaseOwner, &ReleaseOwner::release>(&releaseOwner);
    auto releaseCallback = make_release_callback([](int) {});
    static_assert(std::is_same<decltype(releaseCallback), Frame::ReleaseCallback>::value,
                  "Release callback should accept normal lambdas");

    asyncThreadPool pool(1, 1, 8);
    auto lambdaFuture = pool.enqueue([]() { return 1; });
    TaskOwner owner;
    auto memberFuture = pool.try_enqueue(&owner, &TaskOwner::run);
    const TaskOwner constOwner;
    auto constFuture = pool.try_enqueue(&constOwner, &TaskOwner::runConst);

    if (lambdaFuture.valid()) {
        (void)lambdaFuture.get();
    }
    if (memberFuture.valid()) {
        (void)memberFuture.get();
    }
    if (constFuture.valid()) {
        (void)constFuture.get();
    }

    pool.stop();
    return 0;
}
