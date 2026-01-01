/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-14 03:11:41
 * @FilePath: /EdgeVision/src/pipeline/uiRenderer.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "uiRenderer.h"

#include "threadPauser.h"
#include "threadUtils.h"
#include "fenceWatcher.h"

#include "qMouseWatch.h"
#include "sys/memoryMonitor.h"
#include "sys/cpuMonitor.h"

#include <QTimer>
#include <QPointer>
#include <QThread>

class UIRenderer::Impl : public QObject {
    Q_OBJECT
public:
    explicit Impl(QObject* parent = nullptr, const std::string& slotTypeName="default");
    ~Impl();

    void init();

    void start();
    void stop();
    void pause(bool refreshing=false);
    void resume();

    void resetTargetSize(const std::pair<uint32_t, uint32_t>& size);
    void resetPlaneHandle(const DisplayManager::PlaneHandle& handle);
    void resetWidgetTargetRect(const DrawRect& targetRect);

    void bindDisplayer(std::weak_ptr<DisplayManager> displayer);
    // TODO : 不应该 被动暴露 widget, 而应该 接收 UI 语义事件
    const MainInterface* getCurrentWidgetUnsafe() const;
    
    void loadCursorIcon(const std::string& iconPath);
    
    void drawText(const QString &text, const QPointF &pos, const QColor &color, int fontSize);
    // TODO: 加上双缓冲 / 
    void updateBoxs(object_detect_result_list&& ret);
    void setFPSUpdater(const fpsUpdater& cb);
private:
    void updateSlotInfo();
    // 渲染
    void setupRenderTimer();
    void onRenderTick();
    // fps/资源
    void setupResourceTimer();
    void onResourceTick();
    
    // slot 名称
    const std::string slotTypeName_;
    // slot 更新标志(第一次为true)
    std::atomic<bool> needUpdate{true};
    bool inited = false;
    // 暂停
    ThreadPauser pauser_;
    std::atomic<bool> running_{false};
    std::atomic<bool> refreshing_{false};

    // fps
    fpsUpdater fpsCb{nullptr};
    // cursor
    int autoCursorSize{32};
    QImage cursor;
    // yoloBoxs
    std::mutex boxMtx;
    object_detect_result_list boxs;
    // 屏幕范围
    uint32_t targetWidth_{0};
    uint32_t targetHeight_{0};
    // 绘制区域
    DrawRect targetRect_;
    struct RectWithVerifiy{
        bool verifiySame(const DrawRect& other) const {
            if (!hasValue) return false;
            return (storedRect.scale == other.scale && storedRect.rect == other.rect);
        }
        void update(const DrawRect& newRect) {
            storedRect = newRect;
            hasValue = true;
        }
        DrawRect storedRect;
        bool hasValue = false;
    };
    
    QMouseWatch mouseWatcher_;  // 继承自MouseWatcher的Qt事件特供类
    CpuMonitor cpuMon_;         // CPU 资源监视
    MemoryMonitor memMon_;      // 内存 资源监视

    // UI
    QPointer<MainInterface> widget_;
    // drm显示器
    std::weak_ptr<DisplayManager> displayer_;
    // drmPlaneHandle
    std::mutex handleMtx;
    DisplayManager::PlaneHandle currentPlaneHandle_;
    
    QTimer renderTimer_;
    QTimer resourceTimer_;

    // 绘制资源
    Core* core_;
    Draw* draw_;

private:
    struct SlotHolder {
        SlotHolder(Core* core, const std::string& name, 
                   std::shared_ptr<Core::resourceSlot> slot)
            : core_(std::move(core)), name_(name), slot_(std::move(slot)) {
        }
        
        ~SlotHolder() {
            if (core_ && slot_) {
                core_->releaseSlot(name_, slot_);
                slot_.reset();
            }
        }
        
        // 禁用拷贝, 允许移动
        SlotHolder(const SlotHolder&) = delete;
        SlotHolder& operator=(const SlotHolder&) = delete;
        SlotHolder(SlotHolder&&) = default;
        SlotHolder& operator=(SlotHolder&&) = default;        
    private:
        Core* core_;
        std::string name_;
        std::shared_ptr<Core::resourceSlot> slot_;
    };
};

// ------------------ 构造 / 析构 ------------------
UIRenderer::Impl::Impl(QObject* parent, const std::string& slotTypeName)
    : QObject(parent)
    , slotTypeName_(slotTypeName)
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    // 环境配置
    qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", QByteArray("1"));
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", QByteArray("minimal"));
}

UIRenderer::Impl::~Impl() {
    stop();
    if (widget_) {
        widget_->deleteLater();
        widget_.clear();
    }
}

void UIRenderer::Impl::init(){
    widget_ = QPointer<MainInterface>(new MainInterface());
    mouseWatcher_.setNotifyWindow(widget_.data());
    /// core_ 申请 slot 需要确认分辨率
    /// ownership: 全局单例, 注意shutdown时机
    core_ = &Core::instance();
    draw_ = &Draw::instance();
    inited = true;
}

// ------------------ 生命周期控制 ------------------
void UIRenderer::Impl::start() {
    if (!inited) init();
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    
    // 绑定线程到(主线程) CPU CORE 1
    ThreadUtils::bindCurrentThreadToCore(2);

    if (running_.load()) return;
    
    running_.store(true);

    setupRenderTimer();
    setupResourceTimer();

    renderTimer_.setParent(this);
    resourceTimer_.setParent(this);
    renderTimer_.start(33);  // ~30fps
    resourceTimer_.start(1000); // 1s

    std::cout << "[UIRenderer] Started." << std::endl;
}

void UIRenderer::Impl::stop() {
    if (!running_.load()) return;
    resume();
    running_.store(false);
    renderTimer_.stop();
    resourceTimer_.stop();
    // 取消槽函数绑定(若重启则会反复绑定)
    renderTimer_.disconnect();
    resourceTimer_.disconnect();

    std::cout << "[UIRenderer] Stopped." << std::endl;
}

void UIRenderer::Impl::pause(bool refreshing) {
    if (!refreshing) {
        pauser_.pause();
        return;
    }
    refreshing_.store(true);
    mouseWatcher_.pause();
    needUpdate = true;
}

void UIRenderer::Impl::resume(){
    if (pauser_.is_paused()) pauser_.resume();
    if (!refreshing_.exchange(false)) return;
    mouseWatcher_.start();
}

// ------------------ 资源重置 ------------------
void UIRenderer::Impl::resetTargetSize(const std::pair<uint32_t, uint32_t>& size) {
    if (targetWidth_ == size.first ||
        targetHeight_ == size.second){
        return;
    }
    needUpdate = true;
    // draw_->shutdown();
    // core_->shutdown();
    // 存储屏幕大小
    targetWidth_ = size.first;
    targetHeight_ = size.second;
    // 重置屏幕范围
    mouseWatcher_.setScreenSize(targetWidth_, targetHeight_);
    
    std::cout << "[UIRenderer] Target size reset to " 
              << targetWidth_ << "x" << targetHeight_ << std::endl;
}

void UIRenderer::Impl::resetWidgetTargetRect(const DrawRect& targetRect) {
    targetRect_ = std::move(targetRect);
}

void UIRenderer::Impl::resetPlaneHandle(const DisplayManager::PlaneHandle& handle) {
    std::lock_guard<std::mutex> lk(handleMtx);
    currentPlaneHandle_ = std::move(handle);
    std::cout << "[UIRenderer] Plane handle updated." << std::endl;
}

void UIRenderer::Impl::loadCursorIcon(const std::string& iconPath) {
    cursor = QImage(QString::fromStdString(iconPath)); 
}

// ------------------ 绑定外部资源 ------------------
void UIRenderer::Impl::bindDisplayer(std::weak_ptr<DisplayManager> displayer) {
    displayer_ = displayer;
    std::cout << "[UIRenderer] Display manager bound." << std::endl;
}

const MainInterface* UIRenderer::Impl::getCurrentWidgetUnsafe() const {
    return widget_;
}

// ------------------ 绘制接口 ------------------
void UIRenderer::Impl::drawText(const QString& text, const QPointF& pos, 
                                 const QColor& color, int fontSize) {
    // 这里可以缓存文本绘制请求, 在renderTick时统一绘制
    // 简单实现: 直接记录到队列或在renderTick中调用
}

/// 线程安全 但是依靠Qt主线程, 可在非 ui 线程调用(yolo)
void UIRenderer::Impl::updateBoxs(object_detect_result_list&& ret) {
    std::lock_guard<std::mutex> lk(boxMtx);
    boxs = std::move(ret);
}

void UIRenderer::Impl::setFPSUpdater(const fpsUpdater& cb){
    fpsCb = cb;
}

// ------------------ 资源显示定时器 ------------------
void UIRenderer::Impl::setupResourceTimer() {
    QObject::connect(&resourceTimer_, &QTimer::timeout, [this]() {
        pauser_.wait_if_paused();
        onResourceTick();
    });
}

void UIRenderer::Impl::onResourceTick() {
    if (nullptr == widget_) {
        std::cerr << "[UIRenderer][ERROR] widget is null" << std::endl;
        return;
    }
    widget_->updateCPUpayload(cpuMon_.getUsage());
    widget_->updateMemoryUsage(memMon_.getUsage());
    if(fpsCb) widget_->updateFPS(fpsCb());
}

// ------------------ 渲染定时器 ------------------
void UIRenderer::Impl::setupRenderTimer() {
    QObject::connect(&renderTimer_, &QTimer::timeout, [this]() {
        pauser_.wait_if_paused();
        onRenderTick();
    });
}

void UIRenderer::Impl::updateSlotInfo() {
    // 重新创建 dmabuf 模板
    auto dmabufTemplate = DmaBuffer::create(
        targetWidth_, 
        targetHeight_, 
        DRM_FORMAT_ABGR8888, 
        0, 0
    );
    
    if (!dmabufTemplate) {
        std::cerr << "[UIRenderer][ERROR] Failed to create dmabuf template." << std::endl;
        throw std::runtime_error("UIRenderer error when create slot dmabuf template.\n");
    }
    
    // 注册渲染槽
    core_->registerResSlot(slotTypeName_, 4, std::move(dmabufTemplate));

    double dpiScale = MainInterface::computeDPIScale(targetWidth_, targetHeight_);    // DPI缩放
    
    if (nullptr == widget_) {
        std::cerr << "[UIRenderer][ERROR] widget is null" << std::endl;
        return;
    }
    int windowWidth  = static_cast<int>(widget_->width() * dpiScale);
    int windowHeight = static_cast<int>(widget_->height() * dpiScale);

    targetRect_.rect = QRectF(0.0, targetHeight_ - windowHeight, windowWidth, windowHeight);
    autoCursorSize = 32 * dpiScale;

    mouseWatcher_.start();
    needUpdate = false;
};

void UIRenderer::Impl::onRenderTick() {
    static RectWithVerifiy rwv; // RAII 管理slot回收
    static std::vector<DrawBox> drawBoxs; // 绘制框列表
    // 随机一个box颜色
    static QColor boxColor = QColor::fromRgb(rand() % 256, rand() % 256, rand() % 256);
    static int x{10}, y{10};   // 鼠标坐标
    
    // 检查状态
    if (refreshing_.load() || pauser_.is_paused()) {
        return;
    }
    // 线程安全的单次更新
    if (needUpdate.exchange(false)) {
        updateSlotInfo();
    }

    // 获取空闲槽
    auto slot = core_->acquireFreeSlot(slotTypeName_, 33);
    if (nullptr == slot || !slot->qfbo.get()) {
        std::cerr << "[UIRenderer][WARN] Failed to acquire slot." << std::endl;
        return;
    }

    // 转换yolo输出为qt绘制可用box
    {
        std::lock_guard<std::mutex> lk(boxMtx);
        if (!boxs.empty()){
            drawBoxs.clear();
            for (auto& result : boxs){											
                QRectF boxRect( result.box.x, result.box.y, result.box.w, result.box.h);
                QString label = QString("%1: %2%")
                    .arg(QString::fromStdString(result.class_name))
                    .arg(int(result.prop * 100));
                DrawBox drawBox(boxRect, boxColor, label);
                drawBoxs.emplace_back(std::move(drawBox));
            }
        }
    }

    // 清空画布
    draw_->clear(slot->qfbo.get());
    
    // 绘制UI widget
    if (widget_) {
        auto tempDrawRect = draw_->drawWidget(*slot, widget_, targetRect_.rect);
        // 渲染后的矩形和目标矩形位置不一致才更新
        if (!rwv.verifiySame(tempDrawRect)) {
            widget_->setUiDrawRect(tempDrawRect.rect, tempDrawRect.scale);
            rwv.update(tempDrawRect); // 备份 rect
        }
    }
    
    // 绘制检测框
    if (!drawBoxs.empty()) draw_->drawBoxes(*slot, drawBoxs);		

    // 绘制光标
    if (!cursor.isNull()) {
        // 获取鼠标坐标
        mouseWatcher_.getPosition(x, y);
        draw_->drawImage(*slot, cursor, QPoint(x, y), autoCursorSize);
    }
    
    // 同步到 dmabuf
    int drawFence = -1;
    if (!slot->syncToDmaBuf(drawFence) || !slot->dmabufPtr) {
        std::cerr << "[UIRenderer][ERROR] Failed to sync dmabuf." << std::endl;
        core_->releaseSlot(slotTypeName_, slot);
        return;
    }

    std::lock_guard<std::mutex> lock(handleMtx);
        
    if (currentPlaneHandle_.valid() && displayer_.lock() != nullptr)
        displayer_.lock()->presentFrame(currentPlaneHandle_, {slot->dmabufPtr}, 
            std::make_shared<SlotHolder>(core_, slotTypeName_, slot));

    // 等待绘制完成后显示
    // FenceWatcher::instance().watchFence(drawFence, [this, slot] () mutable {
    //     // 获取当前有效的 plane handle
    //     std::lock_guard<std::mutex> lock(handleMtx);
        
    //     if (currentPlaneHandle_.valid() && displayer_) {
    //         displayer_->presentFrame(currentPlaneHandle_, {slot->dmabufPtr}, 
    //             std::make_shared<SlotHolder>(slotTypeName_, slot));
    //         // 使用slotHolder自动析构 (core_->releaseSlot)
    //         // core_->releaseSlot(slotTypeName_, slot);
    //     }
    // });
}

// ------------------ 对外接口 ------------------
UIRenderer::UIRenderer(const std::string& slotTypeName) {
    impl_ = std::make_unique<Impl>(nullptr, slotTypeName); 
}
UIRenderer::~UIRenderer() = default;
void UIRenderer::init() { impl_->init(); }
void UIRenderer::start() { impl_->start(); }
void UIRenderer::stop() { impl_->stop(); }
void UIRenderer::pause(bool refreshing) { impl_->pause(refreshing); }
void UIRenderer::resume() { impl_->resume(); }

void UIRenderer::resetTargetSize(const std::pair<uint32_t, uint32_t>& size) {
    impl_->resetTargetSize(size);
}
void UIRenderer::resetPlaneHandle(const DisplayManager::PlaneHandle& handle) {
    impl_->resetPlaneHandle(handle);
}
void UIRenderer::resetWidgetTargetRect(const DrawRect& targetRect) {
    impl_->resetWidgetTargetRect(targetRect);
}
void UIRenderer::bindDisplayer(std::weak_ptr<DisplayManager> displayer) {
    impl_->bindDisplayer(displayer);
}
const MainInterface* UIRenderer::getCurrentWidgetUnsafe() const {
    return impl_->getCurrentWidgetUnsafe();
}
void UIRenderer::loadCursorIcon(const std::string& iconPath) {
    impl_->loadCursorIcon(iconPath);
}
void UIRenderer::drawText(const QString& text, const QPointF& pos, 
                          const QColor& color, int fontSize) {
    impl_->drawText(text, pos, color, fontSize);
}
void UIRenderer::updateBoxs(object_detect_result_list&& ret) {
    impl_->updateBoxs(std::move(ret));
}
void UIRenderer::setFPSUpdater(const fpsUpdater& cb) { impl_->setFPSUpdater(cb); }


#include "uiRenderer.moc"