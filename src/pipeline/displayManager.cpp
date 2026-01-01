/*
* @Author: SweerItTer xxxzhou.xian@gmail.com
* @Date: 2025-12-05 21:58:03
 * @FilePath: /EdgeVision/src/pipeline/displayManager.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
*/

#include "displayManager.h"

#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <atomic>

#include "threadUtils.h"             // 线程绑定
#include "drm/drmLayer.h"            // layer 管理
#include "drm/planesCompositor.h"    // plane 管理
#include "fenceWatcher.h"            // fence 监视

using namespace std::chrono_literals;
static constexpr size_t CACHESIZE = 2; // 双缓冲

// ------------------- planes 信息输出 -------------------
static void infoPrinter(const std::vector<uint32_t>& ids) {
    std::cout << "Gain " << ids.size() << " usable planes";
    for (auto& id : ids) {
        std::cout << " " << id;
    }
    std::cout << std::endl;
}

// ------------------- 内部实现 -------------------
class DisplayManager::Impl {
    enum class DisplayerStatus {
        Free = 0,
        Busy = 1
    };
public:
    Impl();
    ~Impl();

    void start();
    void stop();
    
    void registerPreRefreshCallback(RefreshCallback cb);
    void registerPostRefreshCallback(RefreshCallback cb);

    void presentFrame(PlaneHandle plane,
                      std::vector<DmaBufferPtr>&& buffers, 
                      std::shared_ptr<void> holder);
    PlaneHandle createPlane(const PlaneConfig& config);
    std::pair<uint32_t, uint32_t> getCurrentScreenSize() const;
private:
    void mainLoop();

    uint32_t fx(uint32_t v) { return v << 16; }
    void setRefreshStatus(bool status) {
        {
            std::unique_lock<std::mutex> lock(loopMtx);
            refreshing = status;
        }
        cv.notify_one();
    }
    void initLayer(const std::shared_ptr<DrmLayer>& layer,
                const DrmLayer::LayerProperties& layerProps);

    bool devicesInit();
    void resourcesClean();

    void doPreRefresh();
    void doPostRefresh();
private:
    struct alignas(64) PendingFrame {
        std::mutex slotMtx;
        std::atomic_bool ready{false};
        std::shared_ptr<DrmLayer> layer;             // layer
        std::vector<DmaBufferPtr> pendingBuffers;    // present 时只更新buffer
        std::shared_ptr<void> holder;                // 每一个 planelayer 独立管理生命周期
        
        // 默认构造
        PendingFrame() = default;

        // 禁用拷贝
        PendingFrame(const PendingFrame&) = delete;
        PendingFrame& operator=(const PendingFrame&) = delete;

        PendingFrame(PendingFrame&& other) noexcept
            : PendingFrame{}  // 默认构造
        { *this = std::move(other); }   // 调用移动赋值

        // 移动赋值
        PendingFrame& operator=(PendingFrame&& other) noexcept {
            if (this != &other) {
                layer = std::move(other.layer);
                pendingBuffers = std::move(other.pendingBuffers);
                holder = std::move(other.holder);
                // mutex 禁止拷贝禁止移动
            }
            return *this;
        }
    };
    // DRM 资源
    SharedDev* devices{nullptr};
    DevPtr     dev{nullptr};

    // plane -> layer 映射
    std::vector<PendingFrame> planesVector;
    
    // 合成器
    std::unique_ptr<PlanesCompositor> compositor;

    // 同步
    std::mutex loopMtx;
    std::mutex planesMtx;
    std::mutex preVectorMtx, postVectorMtx;

    std::condition_variable cv;

    // 线程
    std::thread thread;

    // 状态
    std::atomic_bool running{false};
    std::atomic_bool refreshing{false};
    std::atomic_uint32_t pendingFrames{0};
    std::atomic<DisplayerStatus> dispStatus{DisplayerStatus::Free};

    // 回调
    std::vector<RefreshCallback> preRefreshCb;
    std::vector<RefreshCallback> postRefreshCb;

    // handle 分配器
    std::atomic_int32_t handleAlloc{0};
};

// ------------------- 启动 / 停止 -------------------
void DisplayManager::Impl::start() {
    if (running.load()) return;
    running.store(true);
    thread = std::thread(&DisplayManager::Impl::mainLoop, this);
    ThreadUtils::bindThreadToCore(thread, 3);
    ThreadUtils::setRealtimeThread(thread.native_handle(), 80); // 设置高优先级
}

void DisplayManager::Impl::stop(){
    if (false == running.load()) return;
    running.store(false);
    cv.notify_one();
    if (thread.joinable()){
        thread.join();
    }
    // doPreRefresh();
}

// ------------------- 构造 / 析构 -------------------
DisplayManager::Impl::Impl() {
    compositor = std::move(PlanesCompositor::create());
    if (!compositor) {
        std::cerr << "[DisplayManager][ERROR] Failed to create PlanesCompositor object." << std::endl;
    }
    DrmDev::fd_ptr->registerResourceCallback(
        std::bind(&DisplayManager::Impl::doPreRefresh, this),
        std::bind(&DisplayManager::Impl::doPostRefresh, this)
    );
    if (true == devicesInit()) {
        std::cout << "[DisplayManager] Init succeeded." << std::endl;
    } else {
        std::cerr << "[DisplayManager][ERROR] Failed to get Devices combine." << std::endl;
    }
}

DisplayManager::Impl::~Impl() {
    stop();
    doPreRefresh();
}

// ------------------- 释放资源 -------------------
void DisplayManager::Impl::doPreRefresh(){
    setRefreshStatus(true);
    {
        std::lock_guard<std::mutex> lock(preVectorMtx);
        for (auto& cb : preRefreshCb) cb(); 
    }
    resourcesClean();
}

// ------------------- 获取资源 -------------------
void DisplayManager::Impl::doPostRefresh(){
    if(!devicesInit()) return; // 获取资源成功才允许继续
    {
        std::lock_guard<std::mutex> lock(postVectorMtx);
        for (auto& cb : postRefreshCb) cb();
    }
    setRefreshStatus(false);
}

// ------------------- 内部资源释放 -------------------
void DisplayManager::Impl::resourcesClean(){
    {
        std::lock_guard<std::mutex> lk(planesMtx);
        planesVector.clear();
    }
    handleAlloc.store(0); // 重置起始 id 
    if(compositor) compositor->removeAllLayer(); // 清空所有缓存 plane
    if(devices) devices->clear();                // 清空设备组合
    if(dev) dev.reset();                         // 清空当前设备组合
}

// ------------------- 获取设备 -------------------
bool DisplayManager::Impl::devicesInit() {
    // 获取设备组合
    auto& deviceList = DrmDev::fd_ptr->getDevices();
    if (deviceList.empty()) {
        std::cerr << "[DisplayManager] No devices available." << std::endl;
        setRefreshStatus(true);
        return false;
    }
    // 取出第一个可用设备组合
    auto firstDev = deviceList[0];
    if (!firstDev) {
        std::cerr << "[DisplayManager] Failed to get usable device." << std::endl;
        setRefreshStatus(true);
        return false;
    }
    devices = &deviceList;
    dev = firstDev;

    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
              << ", Resolution: " << dev->width << "x" << dev->height << "\n";
    // 绑定 crtc -> plane
    DrmDev::fd_ptr->refreshPlane(dev->crtc_id);
    return true;
}

// ------------------- layer 初始化 -------------------
void DisplayManager::Impl::initLayer(
    const std::shared_ptr<DrmLayer>& layer,
    const DrmLayer::LayerProperties& layerProps) {

    layer->setProperty(layerProps);

    // update后自动调用
    layer->setUpdateCallback(
        [this](const std::shared_ptr<DrmLayer>& layer, uint32_t fbId) {
            if (nullptr != compositor) {
                // 更新layer状态
                compositor->updateLayer(layer, fbId);
            }
        }
    );
}

// ------------------- Plane 创建 -------------------
DisplayManager::PlaneHandle
DisplayManager::Impl::createPlane(const PlaneConfig& config) {
    // 构造无效 handle
    PlaneHandle planeHandle;

    // 检测资源
    if (nullptr == dev || !DrmDev::fd_ptr || DrmDev::fd_ptr->get() < 0) {
        std::cerr << "[DisplayManager][ERROR] Invalid DRM device or file descriptor" << std::endl;
        return planeHandle;
    }
    // size 检测
    uint32_t srcWidth  = config.srcWidth;
    uint32_t srcHeight = config.srcHeight; // fix spelling
    const uint32_t zero__ = static_cast<uint32_t>(0);

    if ((srcWidth <= zero__) || (srcHeight <= zero__)) {
        std::cerr << "[DisplayManager][ERROR] Invalid dimensions: " 
                  << srcWidth << "x" << srcHeight << std::endl;
        return planeHandle;
    }
    // 不需要检查是否对齐, 但需要检查是否出界
    auto devW = dev->width; auto devH = dev->height;
    if ((devW <= zero__) || (devH <= zero__)) {
        std::cerr << "[DisplayManager][ERROR] Device has no space left on, current Resolution: " 
                  << devW << "x" << devH << std::endl;
        return planeHandle;
    }
    if ((srcWidth > devW) || (srcHeight > devH)) {
        std::cerr << "[DisplayManager][ERROR] Resolution: " 
                  << srcWidth << "x" << srcHeight
                  << "out of range, max size:" << devW << "x" << devH
                  <<std::endl;
        return planeHandle;
    }

    DisplayManager::PlaneType pt = config.type;
    std::vector<uint32_t> usablePlaneIds;

    // 匹配指定类型 Plane
    switch (pt) {
    case DisplayManager::PlaneType::OVERLAY:
        DrmDev::fd_ptr->getPossiblePlane(
            DRM_PLANE_TYPE_OVERLAY, 
            config.drmFormat,
            usablePlaneIds
        );
        break;
    case DisplayManager::PlaneType::PRIMARY:
        DrmDev::fd_ptr->getPossiblePlane(
            DRM_PLANE_TYPE_PRIMARY, 
            config.drmFormat,
            usablePlaneIds
        );
        break;
    default:
        std::cerr << "[DisplayManager][ERROR] unsupported plane type" << std::endl;
        return planeHandle;
    }

    // 检测匹配结果
    infoPrinter(usablePlaneIds);
    if (usablePlaneIds.empty()) { 
        std::cerr << "[DisplayManager][ERROR] No matched plane on createPlane." << std::endl;
        return planeHandle;
    }

	// 创建 layer
    std::shared_ptr<DrmLayer> layer = std::make_shared<DrmLayer>(std::vector<DmaBufferPtr>(), CACHESIZE);

    DrmLayer::LayerProperties props{};
    props.plane_id_      = usablePlaneIds[0];
    props.crtc_id_       = dev->crtc_id;
    // 源图像区域
    props.srcX_          = fx(0);
    props.srcY_          = fx(0);
    props.srcwidth_      = fx(srcWidth);
    props.srcheight_     = fx(srcHeight);
    // 显示图像区域(自动缩放)
    props.crtcX_         = 0;
    props.crtcY_         = 0;
    props.crtcwidth_     = devW;
    props.crtcheight_    = devH;
    props.zOrder_        = config.zOrder;
    
    // 配置 layer
    initLayer(layer, props);
    
    if (!compositor || !compositor->addLayer(layer)){
        std::cerr << "[DisplayManager][ERROR] Failed to add layer into compositor." << std::endl;
        return planeHandle;
    }

    int id = handleAlloc.load();
    planeHandle.reset(id);
    
    PendingFrame pf;
    pf.layer = layer;
    {
        std::lock_guard<std::mutex> lk(planesMtx);
        planesVector.emplace_back(std::move(pf));
    }
    handleAlloc.fetch_add(1);
    return planeHandle;
}

// ------------------- 提交显示 -------------------
void DisplayManager::Impl::presentFrame(
    PlaneHandle plane,
    std::vector<DmaBufferPtr>&& buffers,
    std::shared_ptr<void> holder
) {
    if (!dev || !dev.get()) return;
    int id = plane.get();
    if (planesVector.empty() || id >= planesVector.size() || id < 0) {
        std::cerr << "[DisplayManager][ERROR] PlaneHandle is invalid." << std::endl;
        return;
    }
    if (buffers.empty() || nullptr == buffers[0]) {
        std::cerr << "[DisplayManager][ERROR] Frame buffers is empty." << std::endl;
        return;
    }
    if (false == plane.valid()) {
        std::cerr << "[DisplayManager][ERROR] PlaneHandle is invalid in presentFrame()." << std::endl;
        return;
    }
    auto& pf = planesVector[id];

    { // 每个slot独立互斥锁
        std::lock_guard<std::mutex> lock(pf.slotMtx);
        pf.pendingBuffers = std::move(buffers);
        pf.holder = std::move(holder);
    }
    pf.ready.store(true);
    pendingFrames.fetch_add(1);
    cv.notify_one();
}

// ------------------- mainLoop -------------------
void DisplayManager::Impl::mainLoop() {
    int drmFence{-1};
    while (running.load()) {
        {
            std::unique_lock<std::mutex> lock(loopMtx);
            // 退出条件: 退出线程 或 未刷新 且 有待处理帧 且 显示状态为空闲
            cv.wait(lock, [this] {
                return (!running.load()) ||
                       (!refreshing.load()
                        && pendingFrames.load() > 0);
                        // && dispStatus.load(std::memory_order_acquire)
                        //     == DisplayerStatus::Free
            });

            if (!running.load()) break;
        }
        // 锁定刷新状态
        // dispStatus.store(DisplayerStatus::Busy, std::memory_order_release);

        bool processedFrame = false;

        for (auto& pf : planesVector) {
            if (!pf.ready.load()) continue;
            bool exc = true;
            pf.ready.compare_exchange_weak(exc, false);
            std::vector<DmaBufferPtr> tmp;
            {
                std::lock_guard<std::mutex> lk(pf.slotMtx);
                if(pf.pendingBuffers.empty() || !pf.pendingBuffers[0]) continue;
                tmp = std::move(pf.pendingBuffers);
            }
            // TODO: 更新 layer 内部实现, 实现fb复用(dmabuf总量固定, 每一帧创建fb存在开销)
            pf.layer->updateBuffer(std::move(tmp));

            processedFrame = true;
        }
        
        if (!processedFrame) continue; // 没有处理任何帧, 跳过

        pendingFrames.fetch_sub(1);

        drmFence = -1;
        if (compositor) compositor->commit(drmFence);

        FenceWatcher::instance().watchFence(drmFence, [this]() {
            for (auto& pf : planesVector) {
                pf.layer->onFenceSignaled();
            }
            // 更新显示状态
            // dispStatus.store(DisplayerStatus::Free, std::memory_order_release);
            // 通知主线程继续处理(无虚假唤醒)
            cv.notify_one();
        });
    }
}

// ------------------- 获取当前DRM设备分辨率(最大) -------------------
std::pair<uint32_t, uint32_t> DisplayManager::Impl::getCurrentScreenSize() const {
    if (nullptr != dev) {
        return std::pair<uint32_t, uint32_t>(dev->width, dev->height);
    }
    return std::pair<uint32_t, uint32_t>(0, 0);
}

// ------------------- 回调注册 -------------------
void DisplayManager::Impl::registerPreRefreshCallback(RefreshCallback cb) {
    std::lock_guard<std::mutex> lock(preVectorMtx);
    preRefreshCb.emplace_back(std::move(cb));
}

void DisplayManager::Impl::registerPostRefreshCallback(RefreshCallback cb) {
    std::lock_guard<std::mutex> lock(postVectorMtx);
    postRefreshCb.emplace_back(std::move(cb));
}

// ------------------- 对外转发 -------------------
DisplayManager::DisplayManager() {
    impl_ = std::make_unique<Impl>();
}
DisplayManager::~DisplayManager() = default;

void DisplayManager::registerPreRefreshCallback(RefreshCallback cb) {
    impl_->registerPreRefreshCallback(std::move(cb));
}
void DisplayManager::registerPostRefreshCallback(RefreshCallback cb) {
    impl_->registerPostRefreshCallback(std::move(cb));
}

void DisplayManager::presentFrame(PlaneHandle plane,
    std::vector<DmaBufferPtr> buffers, std::shared_ptr<void> holder) {
    impl_->presentFrame(plane, std::move(buffers), std::move(holder));
}

std::pair<uint32_t, uint32_t> DisplayManager::getCurrentScreenSize() const {
    return impl_->getCurrentScreenSize();
}

DisplayManager::PlaneHandle
DisplayManager::createPlane(const PlaneConfig& config) const {
    return impl_->createPlane(config);
}
void DisplayManager::start() { impl_->start(); }
void DisplayManager::stop() { impl_->stop(); }


// ------------------- PlaneHandle -------------------
// ---------- 拷贝构造 ----------
DisplayManager::PlaneHandle::PlaneHandle(const PlaneHandle& other) {
    int v = other.id_.load(std::memory_order_acquire);
    id_.store(v, std::memory_order_release);
}

// ---------- 拷贝赋值 ----------
DisplayManager::PlaneHandle& DisplayManager::PlaneHandle::operator=(const PlaneHandle& other) {
    if (this != &other) {
        int v = other.id_.load(std::memory_order_acquire);
        id_.store(v, std::memory_order_release);
    }
    return *this;
}

// ---------- 移动构造 ----------
DisplayManager::PlaneHandle::PlaneHandle(PlaneHandle&& other) noexcept {
    int v = other.id_.load(std::memory_order_acquire);
    id_.store(v, std::memory_order_release);

    // 被移动对象必须失效
    other.id_.store(-1, std::memory_order_release);
}

// ---------- 移动赋值 ----------
DisplayManager::PlaneHandle& DisplayManager::PlaneHandle::operator=(PlaneHandle&& other) noexcept {
    if (this != &other) {
        int v = other.id_.load(std::memory_order_acquire);
        id_.store(v, std::memory_order_release);

        // 清除来源
        other.id_.store(-1, std::memory_order_release);
    }
    return *this;
}
