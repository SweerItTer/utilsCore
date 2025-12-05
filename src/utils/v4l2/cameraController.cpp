#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <cstring>
#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "v4l2/cameraController.h"
#include "v4l2/v4l2Exception.h"

#include "fdWrapper.h"      // fd RAII处理类
#include "logger.h"
#include "v4l2/formatTool.h"
#include "dma/dmaBuffer.h"    // 包含 drm 头文件
#include "objectsPool.h"
#include "threadUtils.h"

#ifndef _XF86DRM_H_
#include <drm.h>
#include <drm_mode.h>
#endif // _XF86DRM_H_

// 具体功能实现在Impl类
class CameraController::Impl {
public:
    // 配置文件
    Impl(const Config& config);
    ~Impl();

    void start();
    void pause();
    void stop();
    
    void setThreadAffinity(int cpu_core);
    void setFrameCallback(FrameCallback&& enqueueCallback_);
    int returnBuffer(int index);

    int getDeviceFd() const;

    // 修改 分辨率 格式
    int changeConfig(const Config& config);
    
private:
    // 多平面
    struct Plane {
        std::shared_ptr<SharedBufferState> state;
        
        // vector 需要调用默认构造函数,需要显式声明
        Plane() = default;
        // 禁止拷贝避免出现多次析构
        Plane(const Plane&) = delete;
        Plane& operator=(const Plane&) = delete;
        // 保留移动构造和移动运算符
        Plane(Plane&&) = default;
        Plane& operator=(Plane&&) = default;

        // RAII
        ~Plane() {
            if (state) {
                state->valid = false; // 主动标记无效
                state.reset();        // 释放共享对象，触发资源释放
            }
        }
    };    
    // 缓冲区管理
    struct Buffer {
        // 单平面使用dma任适应
        std::shared_ptr<SharedBufferState> state;
        
        std::vector<Plane> planes;  // 多平面用
        bool queued = false;

        Buffer() = default;
        // 避免拷贝多次析构
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = default;
        Buffer& operator=(Buffer&&) = default;

        ~Buffer() {
            if (state) {
                state->valid = false; // 主动标记无效
            }
            // planes 自动析构,不需要额外做
        }
    };
private:
    // 创建dmabuf
    DmaBufferPtr createDmabuf(__u32 width, __u32 height, size_t needed_size, uint32_t offset, uint32_t planeIndex);
    // 获取格式尺寸
    bool getFormatSize(uint32_t& width, uint32_t& height, uint32_t planeIndex);

    void inquireCapabilities();
    void init();
    void setupFormat();
    void requestBuffers();
    void mapBuffers();
    
    void allocateDMABuffers();
    int  enqueueBuffer(int index);
    void startStreaming();
    void captureLoop();

    void reclaimAllBuffers();
    void releaseBuffers();

    // mmap单一平面
    std::shared_ptr<SharedBufferState> mmapPlane(int fd, size_t length, off_t offset);
    // 统一调用接口参数
    std::shared_ptr<SharedBufferState> mapSinglePlaneBuffer(int fd, v4l2_buffer& buf);    
    std::vector<Plane> mapMultiPlaneBuffer(int fd, v4l2_buffer& buf);
    void resolutionVerify(v4l2_format& fmt, uint32_t bpl);
    // 等待帧 v4l2
    bool waitForFrameReady();
    // 计算帧率
    void calculateFps(uint64_t timestamp);
    // 出队 buf
    bool dequeueBuffer(v4l2_buffer& buf, v4l2_plane* planes);
    // 构造 Frame
    FramePtr makeFrame(const v4l2_buffer& buf, uint64_t timestamp);
private:
    __u32 currentWidth;
    __u32 currentHeight;

    Config config_;
    FdWrapper fd_; // video 设备描述符
    
    v4l2_buf_type buf_type_;  // V4L2_BUF_TYPE_VIDEO_CAPTURE || V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
    v4l2_memory memory_type_; // V4L2_MEMORY_DMABUF || V4L2_MEMORY_MMAP

    std::vector<Buffer> buffers_;
    
    std::mutex _mutex_; // 添加互斥锁
    FrameCallback enqueueCallback_; // 回调函数 建议使用队列,直接使用数据处理函数容易出现调用混乱
    
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::thread capture_thread_;

    std::atomic<bool> is_destroying_{false}; // 添加析构标志
};

CameraController::Impl::Impl(const Config& config) 
    : config_(config) {
    try{
        // 打开 video 设备
        fd_ = FdWrapper(open(config_.device.c_str(), O_RDWR | O_NONBLOCK));
        if (fd_.get() < 0) {
            throw V4L2Exception("Failed to open device: " + config_.device, errno);
        }
        inquireCapabilities();
        // 初始化设备
        init();
    } catch (const V4L2Exception& ex) {
        fprintf(stderr, "[CameraController][ERROR] Error in Constructor : %s\n", ex.what());
        throw;  // 也可以选择不向上传递，或者改成其他处理
    }
}

CameraController::Impl::~Impl() {
    is_destroying_ = true; // 标记正在析构
    stop();
}

void CameraController::Impl::inquireCapabilities(){
    // 查询设备能力
    v4l2_capability cap = {};
    if (ioctl(fd_.get(), VIDIOC_QUERYCAP, &cap) < 0) {
        throw V4L2Exception("VIDIOC_QUERYCAP failed", errno);
    }
    
    // 确定缓冲区类型
    buf_type_ = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ?
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : 
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    memory_type_ = config_.use_dmabuf ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
}

void CameraController::Impl::start() {
    if (true == paused_){
        paused_ = false;
        return;
    } else if (false == running_){
        startStreaming();
        running_ = true;
        capture_thread_ = std::thread(&Impl::captureLoop, this);
    } else return;    
}

void CameraController::Impl::pause(){
    paused_ = true;
}

void CameraController::Impl::stop() {
    is_destroying_ = true; // 标记正在析构

    if (false == running_) {
        return;
    }
    
    running_ = false;
    paused_ = false;

    // 先关闭捕获线程,防止后续 buffer 操作被并发访问
    if (true == capture_thread_.joinable()) {
        capture_thread_.join();
    }

    /* 
     * 如果只依赖析构自动释放内存,而不保证驱动中的缓冲区状态,
     * 可能导致后续再次启动时失败(如 VIDIOC_STREAMON 报错)或者导致丢帧、内核警告等问题。
     * 
     * 为了保持内核缓冲队列状态一致,这里需要将所有未入队的缓冲区重新归还给内核,
     * 保证最后一次 STREAMOFF 时,驱动缓冲区状态是干净、统一的。
     */
    reclaimAllBuffers();

    // 停止视频流,驱动会自动 dequeue 内核持有的所有缓冲区
    v4l2_buf_type type = buf_type_;
    ioctl(fd_.get(), VIDIOC_STREAMOFF, &type);

    // 清空并释放所有缓冲区内存
    releaseBuffers();
}

void CameraController::Impl::init() {
    // 设置视频格式
    setupFormat();
    // 请求buffer
    requestBuffers();
    if (config_.use_dmabuf) {
        allocateDMABuffers(); // DMABUF专属初始化
    } else {
        mapBuffers(); // MMAP专属初始化
    }
}

int CameraController::Impl::getDeviceFd() const {
    return fd_.get();
}

void CameraController::Impl::resolutionVerify(v4l2_format& fmt, uint32_t bpl) {
    if (0 != ioctl(fd_.get(), VIDIOC_G_FMT, &fmt)) {
        fprintf(stderr, "[CameraController][Warning] VIDIOC_G_FMT failed: %s\n", strerror(errno));
    }
    bool muitable = (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_);
    // 更新当前分辨率
    currentWidth = muitable ? fmt.fmt.pix_mp.width : fmt.fmt.pix.width;
    currentHeight = muitable ? fmt.fmt.pix_mp.height : fmt.fmt.pix.height;
    fprintf(stdout, "[CameraController] Current Format: width=%u, height=%u, bpl=%u\n",
        muitable ? fmt.fmt.pix_mp.width : fmt.fmt.pix.width,
        muitable? fmt.fmt.pix_mp.height : fmt.fmt.pix.height,
        muitable ? fmt.fmt.pix_mp.plane_fmt[0].bytesperline : fmt.fmt.pix.bytesperline);
    // 验证 bytesperline
    uint32_t actual_bpl = muitable ? fmt.fmt.pix_mp.plane_fmt[0].bytesperline : fmt.fmt.pix.bytesperline;
    if (actual_bpl != bpl) {
        fprintf(stdout, "[CameraController][Warning] Driver adjusted bytesperline from %u to %u\n", bpl, actual_bpl);
    }
    // 只对 V4L2_BUF_TYPE_VIDEO_CAPTURE 却驱动支持时输出帧率
    if (muitable == true) return;

    struct v4l2_streamparm streamparm{};
    streamparm.type = buf_type_;
    if (0 == ioctl(fd_.get(), VIDIOC_G_PARM, &streamparm)) {
        fprintf(stdout, "[CameraController] Current Frame Rate: %u/%u\n",
            streamparm.parm.capture.timeperframe.numerator,
            streamparm.parm.capture.timeperframe.denominator);
    } else if (EINVAL == errno) {
        fprintf(stdout, "[CameraController][Warning] VIDIOC_G_PARM not supported for this capture type\n");
    }
}

// Stride控制: 只能通过 bytesperline, 没有其他途径
void CameraController::Impl::setupFormat() {
    v4l2_format fmt = {};
    fmt.type = buf_type_;
    // auto buf = DmaBuffer::create(config_.width, config_.height, convertV4L2ToDrmFormat(config_.format), 0, 0);
    uint32_t bpl = config_.width;
    // 配置格式
    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
        fmt.fmt.pix_mp.width = config_.width;
        fmt.fmt.pix_mp.height = config_.height;
        fmt.fmt.pix_mp.pixelformat = config_.format;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        // 根据驱动需求修改
        fmt.fmt.pix_mp.num_planes = config_.plane_count;
        fmt.fmt.pix_mp.plane_fmt[0].bytesperline = bpl;
    } else {
        fmt.fmt.pix.width = config_.width;
        fmt.fmt.pix.height = config_.height;
        fmt.fmt.pix.pixelformat = config_.format;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        fmt.fmt.pix.bytesperline = bpl;
    }
    
    if (ioctl(fd_.get(), VIDIOC_S_FMT, &fmt) < 0) {
        throw V4L2Exception("VIDIOC_S_FMT failed", errno);
    }

    // 验证
    resolutionVerify(fmt, bpl);
}

void CameraController::Impl::requestBuffers() {
    // VIDIOC_REQBUFS 调用对于多平面与单平面是一样的
    v4l2_requestbuffers req = {};
    req.count = config_.buffer_count;
    req.type = buf_type_;
    req.memory = memory_type_;
    
    // 申请缓冲区
    if (ioctl(fd_.get(), VIDIOC_REQBUFS, &req) < 0) {
        throw V4L2Exception("VIDIOC_REQBUFS failed", errno);
    }
    
    // 预留缓冲区空间
    buffers_.resize(req.count);
}

std::shared_ptr<SharedBufferState> CameraController::Impl::mmapPlane(int fd, size_t length, off_t offset) {
    // nullptr - 系统自动选定地址
    void* start = mmap(nullptr, length, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED, fd, offset);
    // 检查
    if (MAP_FAILED == start) {
        throw V4L2Exception("mmap failed", errno);
    }
    return std::make_shared<SharedBufferState>(-1, start, length);
}

std::shared_ptr<SharedBufferState> CameraController::Impl::mapSinglePlaneBuffer(int fd, v4l2_buffer& buf) {
    return mmapPlane(fd, buf.length, buf.m.offset);
}

std::vector<CameraController::Impl::Plane> CameraController::Impl::mapMultiPlaneBuffer(int fd, v4l2_buffer& buf) {
    // 根据实际情况分配栈内存
    std::vector<Plane> planes(buf.length);
    for (int p = 0; p < buf.length; ++p) {
        planes[p].state = mmapPlane(fd, buf.m.planes[p].length, buf.m.planes[p].m.mem_offset);
    }
    return planes;
}

void CameraController::Impl::mapBuffers() {
    for (int i = 0; i < buffers_.size(); i++) {
        v4l2_buffer buf = {};
        buf.type = buf_type_;
        buf.memory = V4L2_MEMORY_MMAP; // memory_type_
        buf.index = i;
        
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
            // 多平面需要初始化
            v4l2_plane planes[VIDEO_MAX_PLANES] = {};
            buf.length = config_.plane_count; // 配置中写好的 plane 个数
            buf.m.planes = planes;

            if (ioctl(fd_.get(), VIDIOC_QUERYBUF, &buf) < 0) {
                throw V4L2Exception("VIDIOC_QUERYBUF failed", errno);
            }
            /* 修改为实际长度
             * 根据不同的驱动有的时候需要用户手动指定平面数量
             * 但是根据提前 VIDIOC_REQBUFS ,多数是自动调整为实际面数
             * 如果没有重新调整大小,根据用户自定义的(大于实际情况)
             * 会在内存映射时出现尝试映射未分配内存的非法操作
             */
            buffers_[i].planes.resize(buf.length);
            
            // 只有所有平面都mmap成功才将平面给buffers_
            buffers_[i].planes = std::move(mapMultiPlaneBuffer(fd_.get(), buf));
        }
        else if( V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type_ ) {
            if (ioctl(fd_.get(), VIDIOC_QUERYBUF, &buf) < 0) {
                throw V4L2Exception("VIDIOC_QUERYBUF failed", errno);
            }
            
            buffers_[i].state = mapSinglePlaneBuffer(fd_.get(), buf);
        }
    }
}

void CameraController::Impl::allocateDMABuffers() {
    /* 虽然DRM API导出了DMA内存
     * 但这属于DRM框架对DMA技术的应用封装,二者不是层级对等的关系
     * 不要误解了 DMA 和 DRM 的关系,一个是 硬件内存访问技术,一个是 图形显示架构
     * 只是刚刚好使用了 DRM 导出了 DMA buf
     */
    // 使用DRM API创建DUMB缓冲区
    // --- 根据实际情况修改容器长度 ---
    for (int i = 0; i < buffers_.size(); i++) {
        size_t plane_num = config_.plane_count;     // 备存储实际 planes 个数
        v4l2_buffer buf{};
        buf.type = buf_type_;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = i;
        buf.length = plane_num; // 先手动设置平面个数
        
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
            // 多平面格式
            v4l2_plane planes[VIDEO_MAX_PLANES]{};
            buf.m.planes = planes;
            // 查询缓冲区信息
            if (0 > ioctl(fd_.get(), VIDIOC_QUERYBUF, &buf)) {
                throw V4L2Exception("VIDIOC_QUERYBUF failed", errno);
            } 
            // 获取实际分配平面数
            plane_num = buf.length; 
            if (plane_num == 0 || plane_num > VIDEO_MAX_PLANES) {
                throw V4L2Exception("Invalid number of planes reported by VIDIOC_QUERYBUF", EINVAL);
            }
            // 修改 buffers_.planes 容器大小为实际 planes 个数
            buffers_[i].planes.resize(plane_num);
        } else {
            // 查询缓冲区信息
            if (0 > ioctl(fd_.get(), VIDIOC_QUERYBUF, &buf)) {
                throw V4L2Exception("VIDIOC_QUERYBUF failed", errno);
            } 
            // 获取实际分配平面数
            plane_num = buf.length; 
        }
        config_.plane_count = plane_num; // 更新配置中的 plane 个数
        fprintf(stdout, "[CameraController] Buffer %d: plane count = %zu\n", i, plane_num);
        // --- 创建DMABUF缓冲区 ---
        /* V4L2 内核驱动可能做了对齐或 stride 扩展
         * 导致需要的 plane.length 比通过 currentWidth * currentHeight * bpp 估算的分配的大
         */
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type_){
            auto dmabuf = createDmabuf(currentWidth, currentHeight, buf.bytesused, 0, 0);
            // 单平面直接对buffers_赋值
            buffers_[i].state = std::make_shared<SharedBufferState>(
                std::move(dmabuf), nullptr);
            continue;
        }
        for (size_t p = 0; p < plane_num; ++p) {
            /* 通常只取第一个平面的长度是不正确的,但是若仅支持NV12的话
             * 第一个平面(Y)长度 = 宽 × 高
             * 第二个平面(UV)长度 = 宽 × 高 / 2
             * 完全可以使用p1的长度去分配p2,但是也有弊端,内存比实际需求要大
             * 并且对多格式兼容性为0,后续优化可以是支持所有平面
             */
            size_t planes_length = buf.m.planes[p].length;          // 当前 planes 所需内存大小
            size_t planes_offset = buf.m.planes[p].data_offset;     // 数据偏移量 (第一个平面一般为0)
            __u32 width_ = currentWidth;
            __u32 height_ = currentHeight;
            // if (!getFormatSize(width_, height_, p)) {   // 获取当前层的尺寸
            //     throw V4L2Exception("Failed to get format size for plane " + std::to_string(p), EINVAL);
            // }
            // 创建 dmabuf
            auto dmabuf = createDmabuf(width_, height_, planes_length, planes_offset, p);
            fprintf(stdout, "[CameraController] Allocated plane %zu: size=%ux%u, length=%zu, offset=%u\n", 
                p, width_, height_, planes_length, planes_offset);
            // 多平面下给对应的平面赋值
            buffers_[i].planes[p].state = std::make_shared<SharedBufferState>(
                std::move(dmabuf), nullptr, planes_length);
            /* 需要移动保证生命周期
             * shared_ptr 的意义不在这,意义在于给其他硬件共享 prime fd
             * 如果在这里引用 +1 后又 -1 ,白白浪费了一次拷贝销耗的性能
             */
        }
    }
}

int CameraController::Impl::enqueueBuffer(int index) {
    v4l2_buffer buf = {};
    v4l2_plane planes[VIDEO_MAX_PLANES] = {};
    
    buf.type = buf_type_;
    buf.index = index;
    buf.memory = memory_type_;

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type_) {
        if (V4L2_MEMORY_DMABUF == memory_type_) {
            buf.m.fd = buffers_[index].state->dmabuf_fd();
        }
        // MMAP 不需要填 fd,直接 queue
    } else { // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
        buf.length = buffers_[index].planes.size();
        buf.m.planes = planes;

        if (V4L2_MEMORY_DMABUF == memory_type_) {
            // DMABUF: 每个 plane 填 fd
            for (size_t p = 0; p < buffers_[index].planes.size(); ++p) {
                auto pbuf = buffers_[index].planes[p].state->dmabuf_ptr;
                planes[p].m.fd = pbuf->fd();
                planes[p].length = pbuf->size();
                // planes[p].bytesused = pbuf->size(); // bytesused 驱动填充了多少数据, 是只读数据...
            }
        }
        else {
            // MMAP: 不需要额外填 fd,内核知道 offset
            for (size_t p = 0; p < buffers_[index].planes.size(); ++p) {
                planes[p].length = buffers_[index].planes[p].state->length;
            }
        }
    }

    int ret = ioctl(fd_.get(), VIDIOC_QBUF, &buf);
    if (0 > ret) {
        fprintf(stderr, "[CameraController][ERROR] VIDIOC_QBUF failed in returnBuffer (errno=%d): %s\n", errno, strerror(errno));
    }
    // 当操作成功即入队
    buffers_[index].queued = (0 == ret);
    return ret;
}

void CameraController::Impl::startStreaming() {
    std::lock_guard<std::mutex> lock(_mutex_);
    for (int i = 0; i < buffers_.size(); i++) {
        enqueueBuffer(i);
    }
    // 启动流
    enum v4l2_buf_type type = buf_type_;
    if (ioctl(fd_.get(), VIDIOC_STREAMON, &type) < 0) {
        throw V4L2Exception("VIDIOC_STREAMON failed", errno);
    }
}

int CameraController::Impl::returnBuffer(int index) {
    std::lock_guard<std::mutex> lock(_mutex_);
    // 检查是否正在析构
    if (is_destroying_) {
        return -1;
    }
    // 检查缓冲区数组是否为空
    if (buffers_.empty()) {
        return -1;
    }
    
    // 检查索引范围
    if (index < 0 || index >= buffers_.size()) {
        fprintf(stderr, "[CameraController][Warning] returnBuffer: invalid index %d\n", index);
        return -1;
    }
    
    // 检查缓冲区是否已经排队
    if (buffers_[index].queued) {
        fprintf(stderr, "[CameraController][Warning] returnBuffer: buffer %d already queued\n", index);
        return -1;
    }
    
    return enqueueBuffer(index);
}

void CameraController::Impl::reclaimAllBuffers()
{
    // 尝试取回所有可能卡在驱动中的缓冲区
    // 3次尝试
    int attempts = 3;
    while (attempts--){
        bool all_reclaimed = true;
        // 检查所有缓冲区
        for (int i = 0; i < buffers_.size(); i++) {
            if (true == buffers_[i].queued) continue; // 已归还(在内核排队)
            
            if (0 == returnBuffer(i) || EEXIST == errno){
                // 归还成功
                continue;
            } else if (EAGAIN == errno) {
                // 需要重试(内核未准备好)
                all_reclaimed = false;
            } else {
                perror("Error during buffer reclaim");
                all_reclaimed = false;
            }
        }
        
        if (true == all_reclaimed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 强制标记所有缓冲区为排队状态(如果依旧失败交由releaseBuffers释放内存)
    for (auto& buffer : buffers_) {
        buffer.queued = true;
    }
}

void CameraController::Impl::releaseBuffers()
{
    // 清空 vector,自动触发每个 Buffer 析构
    auto temp = std::vector<Buffer>();
    buffers_.swap(temp);
}

DmaBufferPtr CameraController::Impl::createDmabuf(__u32 width, __u32 height, size_t needed_size, uint32_t offset, uint32_t planeIndex) {
    DmaBufferPtr buf = nullptr;
    // 获取实际格式 (V4L2_PIX_FMT_NV12 -> DRM_FORMAT_NV12)
    auto currentformat = convertV4L2ToDrmFormat(config_.format);
    if (-1 == currentformat){
        throw V4L2Exception("Unsupported V4L2 -> DRM format: " + std::to_string(config_.format));
    }

    // 根据实际长宽分配内存(NV12等每个平面宽高不一致需要特殊处理)
    buf = DmaBuffer::create(width, height, currentformat, needed_size, offset, planeIndex);
    if (nullptr == buf) {
        throw V4L2Exception("create DmaBuffer failed.");
    }

    // 多平面时做 planes_length 校验
    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
        if (needed_size > buf->size()) {
            throw V4L2Exception("Allocated dmabuf too small: " +
                std::to_string(buf->size()) + " < required " + std::to_string(needed_size));
        }
    }
    return buf;
}

bool CameraController::Impl::getFormatSize(uint32_t& width, uint32_t& height, uint32_t planeIndex) {
    auto it = FormatTool::formatPlaneMap.find(config_.format);
    if (FormatTool::formatPlaneMap.end() == it) {
        fprintf(stderr, "[CameraController][ERROR] Unsupported format in getFormatSize()\n");
        return false; // 未知格式不处理
    }

    const auto& scales = it->second;
    if (planeIndex >= scales.size()) {
        fprintf(stderr, "[CameraController][ERROR] Invalid plane index %u for format %u\n", planeIndex, config_.format);
        return false; // 无效平面索引
    }

    width  = static_cast<uint32_t>(width  * scales[planeIndex].width_scale);
    height = static_cast<uint32_t>(height * scales[planeIndex].height_scale);
    return true;
}

void CameraController::Impl::setThreadAffinity(int cpu_core)
{
    // 在内部处理线程中设置亲和性
    if(capture_thread_.joinable()) {
        ThreadUtils::safeBindThread(capture_thread_, cpu_core);
    }
}

void CameraController::Impl::setFrameCallback(FrameCallback &&enqueueCallback)
{
    enqueueCallback_ = std::move(enqueueCallback);
}

// False:next loop | True:keep on
bool CameraController::Impl::waitForFrameReady() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_.get(), &fds);
    timeval tv = {1, 0}; // 1秒超时
    int ret = select(fd_.get() + 1, &fds, nullptr, nullptr, &tv);
    if (ret < 0) {
        if (errno == EINTR) return false;
        throw V4L2Exception("select failed", errno);
    }
    return (ret > 0);
}

void CameraController::Impl::calculateFps(uint64_t timestamp) {
    static int frame_count = 0;
    static constexpr int FPS_WINDOW = 30;
    static std::deque<uint64_t> timestamps;

    timestamps.push_back(timestamp);
    if (timestamps.size() > FPS_WINDOW) {
        timestamps.pop_front();
    }

    if (timestamps.size() >= 2) {
        double avg_fps = (timestamps.size() - 1) * 1e6 / 
                        (timestamps.back() - timestamps.front());
        if (frame_count % FPS_WINDOW == 0) {
            fprintf(stdout, "Avg fps: %.1f\n", avg_fps);
        }
        frame_count++;
    }
}

bool CameraController::Impl::dequeueBuffer(v4l2_buffer& buf, v4l2_plane* planes) {
    buf.type = buf_type_;
    buf.memory = memory_type_;
    // 多平面需要指定 plane 的 length 和m.planes 或者 fd
    // 根据缓冲区类型进行不同设置
    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
        // 多平面处理
        buf.length = buffers_[0].planes.size();   // 最大平面数
        buf.m.planes = planes;                    // 指向平面数组
    } else { // 单平面处理
        // 对于DMABUF需要设置长度
        if (V4L2_MEMORY_DMABUF == memory_type_) {
            buf.length = buffers_[0].state->length;
        }
    }
    
    std::lock_guard<std::mutex> lock(_mutex_);
    if (ioctl(fd_.get(), VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) 
            return false;
        throw V4L2Exception("VIDIOC_DQBUF failed", errno);
    }
    /* 自动释放锁
     * 为什么这里就释放了锁?
     * 在我的预想里回调函数应该是一个打包的入队函数
     * 但是不保证所有人对该函数的理解一致,可能在回调函数内调用 returnBuffer 函数
     * 若不释放锁,在这样的情况下将会死锁 (在锁的范围里去抢锁,但是抢不到,欸,就死了)
     */
    return true;
}

FramePtr CameraController::Impl::makeFrame(const v4l2_buffer& buf, uint64_t timestamp) {
    static uint64_t frame_id = 0;
    // 这里的 frame 仅仅是指针或者文件描述符的引用,并未持有实际数据,并非深拷贝,并且Frame禁用了拷贝构造,仅保留移动构造
    // 先声明后构造(少一次默认构造)
    FramePtr frame_opt;
    std::vector<std::shared_ptr<SharedBufferState>> plane_states;
    switch (buf_type_) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
        // --- 采用单平面连续物理内存的思想处理多平面
        if (buf.length < 1) {
            throw V4L2Exception("Invalid number of planes", EINVAL);
        }
        // 该部分基于一个理论实现(不保证全部平台支持)(连续内存区域不需要传递所有平面指针) 见 utils/rga/rgaConverter.h 说明
        // void* plane0_ptr = buffers_[buf.index].planes[0].start;
        // int plane0_fd = buffers_[buf.index].planes[0].dmabuf_fd;
        for (uint32_t p = 0; p < buf.length; ++p) {
            // 当前 plane 大小
            buffers_[buf.index].planes[p].state->length = buf.m.planes[p].length;
            plane_states.push_back(buffers_[buf.index].planes[p].state);
        }
        // 构造frame
        frame_opt = std::make_unique<Frame>( std::move(plane_states) );
        frame_opt->meta = {frame_id++, timestamp, static_cast<int>(buf.index), config_.width, config_.height};
        break;
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        // DMA/MMAP 交由 SharedBufferState 管理
        buffers_[buf.index].state->length = buf.bytesused;
        frame_opt = std::make_unique<Frame>( buffers_[buf.index].state );
        frame_opt->meta = {frame_id++, timestamp, static_cast<int>(buf.index), config_.width, config_.height};
        break;

    default:
        frame_opt = std::make_unique<Frame>();
        break;
    }
    // 到达最大值
    // (9.741e9年(97亿年),真的假的?从大爆炸至今,宇宙大约也只有 138亿 年历史)
    if (frame_id == UINT64_MAX) {
        frame_id = 0;
    }
    frame_opt->setReleaseCallback([this](int index){
        (void)this->returnBuffer(index); // 忽略返回值
    });
    return std::move(frame_opt);
}

void CameraController::Impl::captureLoop() {
    std::cout << "V4L2 capture thread TID: " << syscall(SYS_gettid) << "\n";

    try
    {
    while (running_) {
        if (true == paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (false == running_) break;
            continue;
        }
        // 等待帧
        if (false == waitForFrameReady()) continue;

        // 出队缓冲区
        v4l2_buffer buf = {};
        v4l2_plane  planes[VIDEO_MAX_PLANES];  // 为多平面准备数组
        dequeueBuffer(buf, planes);
        // 标记缓冲区已出队
        buffers_[buf.index].queued = false;

        /* 笔记笔记!!!!
         * 为什么运行到这后,buf.m.planes.lenth会变化?从3801600变成127
         * 为什么?这个函数和buf可没有任何的关系啊?!!!
         * 在历经几个小时的debug后发现,因为buf.m.plane是由dequeueBuffer内部赋值的!
         * 而函数里的v4l2_plane planes作为栈上的临时变量,离开作用域后就会随时被修改
         * 导致buf.m.planes的值被修改为一堆不知道哪来的数值
         * 解决方案:将planes拿出来,放在主循环,直到帧完整构建并入队后才销毁(生命周期的问题)*/
        uint64_t t0; // 出队时间(us)
        mk::makeTimestamp(t0);

        // 构造 Frame
        /* 有点抽象,猜测是因为不允许复制uniqueptr,所以使用右值传递? */
        FramePtr frame_opt = std::move(makeFrame(buf, t0));

        // 回调传递
        if (enqueueCallback_ && 0 <= frame_opt->index()) {
            enqueueCallback_(std::move(frame_opt)); // 传递指针
        } else returnBuffer(buf.index); // 未设置正确的回调时需要手动回收缓冲区
        
        // 帧率计算
        // uint64_t sensor_us = static_cast<uint64_t>(buf.timestamp.tv_sec) * 1000000ULL + buf.timestamp.tv_usec;
        // calculateFps(sensor_us);
    }
    } catch (const std::exception& e) {
        fprintf(stderr, "[CameraController][ERROR] Capture loop error: %s\n", e.what());
        running_ = false;
    }
}

int CameraController::Impl::changeConfig(const Config &config)
{
    /* 关停线程
     * 回收所有出队的帧
     * 停止视频流,驱动会自动 dequeue 内核持有的所有缓冲区
     * 清空缓存队列
     */
    stop();
    
    // 重新申请新缓冲
    /* 设备节点不变
     * 对应的设备类型(单/多平面)不会改变
     * 分辨率.像素格式,缓冲区长度将会自动调整
     */
    config_ = config;
    // 只需要查询方法是否变化
    memory_type_ = config_.use_dmabuf ? V4L2_MEMORY_DMABUF : V4L2_MEMORY_MMAP;
    // 5.重新分配空间(映射)
    init();
    // 6.再次开启流
    startStreaming();
    // 7.再开启线程
    start();
    return 0;
}


/* --- CameraController 公共接口实现 --- */
CameraController::CameraController(const Config& config)
    : impl_(std::make_unique<Impl>(config)) {}

CameraController::~CameraController() = default;

void CameraController::start() { impl_->start(); }
void CameraController::stop() { impl_->stop(); }
void CameraController::pause(){ impl_->pause(); }

void CameraController::setThreadAffinity(int cpu_core) { impl_->setThreadAffinity(cpu_core); }
void CameraController::setFrameCallback(FrameCallback &&callback){ impl_->setFrameCallback(std::move(callback)); }

int CameraController::getDeviceFd() const { return impl_->getDeviceFd(); }

void CameraController::returnBuffer(int index){ impl_->returnBuffer(index); }
