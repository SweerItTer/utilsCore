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

#include <xf86drm.h>         // DRM 核心功能
#include <xf86drmMode.h>     // DRM 模式设置功能
#include <drm/drm.h>         // DRM 通用定义 (可选但推荐)
#include <drm/drm_mode.h>    // DRM 模式结构体定义

#include "v4l2/camera_controller.h"
#include "v4l2/v4l2_exception.h"

#include "fdWrapper.h"

inline uint32_t get_bpp(uint32_t pixelformat) {
    switch (pixelformat) {
        case V4L2_PIX_FMT_NV12:
            // 对应 YUV420 半平面,理论 ~12bpp,但这里填 16 以保证整数,且符合 DRM 要求
            return 16;

        case V4L2_PIX_FMT_NV16:
            // 对应 YUV422 半平面,约 16bpp
            return 16;

        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
            // YUV422 打包格式
            return 16;

        case V4L2_PIX_FMT_RGB24:
            return 24;

        case V4L2_PIX_FMT_BGR32:
        case V4L2_PIX_FMT_RGB32:
            return 32;

        default:
            throw std::runtime_error("Unsupported pixel format in get_bpp");
    }
}

// 具体功能实现在Impl类
class CameraController::Impl {
public:
    // 配置文件
    Impl(const Config& config);
    ~Impl();

    void start();
    void stop();
    
    void setFrameCallback(FrameCallback&& callback);
    
private:
    void init();
    void setupFormat();
    void requestBuffers();
    void mapBuffers();
    void allocateDMABuffers();
    void startStreaming();
    void captureLoop();
    int returnBuffer(int index);

    void reclaimAllBuffers();
    void releaseBuffers();
    
    Config config_;
    FdWrapper fd_; // video 设备描述符
    // int fd_ = -1; 
    
    v4l2_buf_type buf_type_;  // V4L2_BUF_TYPE_VIDEO_CAPTURE || V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
    v4l2_memory memory_type_; // V4L2_MEMORY_DMABUF || V4L2_MEMORY_MMAP
    
    // 多平面
    struct Plane {
        int dmabuf_fd = -1;
        void* start = nullptr;
        size_t length = 0;
    
        // RAII
        ~Plane() {
            if (start) {
                fprintf(stderr, "Unmapping plane at %p, length %zu\n", start, length);
                munmap(start, length);
                start = nullptr;
            }
        }
    };    
    // 缓冲区管理
    struct Buffer {
        int dmabuf_fd = -1;         // DMABUF fd
        void* start = nullptr;      // MMAP start*
        size_t length = 0;

        std::vector<Plane> planes;  // 多平面用
        bool queued = false;

        ~Buffer() {
            if (nullptr != start) {
                fprintf(stderr, "Unmapping single-plane buffer at %p, length %zu\n", start, length);
                munmap(start, length);
                start = nullptr;
            }
            if (0 <= dmabuf_fd) {
                fprintf(stderr, "Closing dmabuf fd %d\n", dmabuf_fd);
                close(dmabuf_fd);
                dmabuf_fd = -1;
            }
            // planes 自动析构,不需要额外做
        }
    };
    std::vector<Buffer> buffers_;
    
    /* 其实建议改成双线程,该部分只出队 buffer 
     * 消费者线程只做数据处理,直接调用 returnBuffer 入队 buffer
     */
    FrameCallback frame_callback_;
    
    std::atomic<bool> running_{false};
    std::thread capture_thread_;
};

CameraController::Impl::Impl(const Config& config) 
    : config_(config) {
    
    // 打开 video 设备
    fd_ = FdWrapper(open(config_.device.c_str(), O_RDWR | O_NONBLOCK));
    if (fd_.get() < 0) {
        throw V4L2Exception("Failed to open device: " + config_.device, errno);
    }
    
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
    // 初始化设备
    init();
}

CameraController::Impl::~Impl() {
    stop();
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

void CameraController::Impl::setupFormat() {
    v4l2_format fmt = {};
    fmt.type = buf_type_;
    // 配置格式
    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
        fmt.fmt.pix_mp.width = config_.width;
        fmt.fmt.pix_mp.height = config_.height;
        fmt.fmt.pix_mp.pixelformat = config_.format;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        // 根据驱动需求修改
        // fmt.fmt.pix_mp.num_planes = config_.plane_count;
    } else {
        fmt.fmt.pix.width = config_.width;
        fmt.fmt.pix.height = config_.height;
        fmt.fmt.pix.pixelformat = config_.format;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }
    
    if (ioctl(fd_.get(), VIDIOC_S_FMT, &fmt) < 0) {
        throw V4L2Exception("VIDIOC_S_FMT failed", errno);
    }
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

void CameraController::Impl::mapBuffers() {
    for (int i = 0; i < buffers_.size(); i++) {
        v4l2_buffer buf = {};
        buf.type = buf_type_;
        buf.memory = V4L2_MEMORY_MMAP; // memory_type_
        buf.index = i;
        
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type_) {            
            if (ioctl(fd_.get(), VIDIOC_QUERYBUF, &buf) < 0) {
                throw V4L2Exception("VIDIOC_QUERYBUF failed", errno);
            }
            // nullptr - 系统自动选定地址
            void* start = mmap(nullptr, buf.length, 
                               PROT_READ | PROT_WRITE, 
                               MAP_SHARED, fd_.get(), buf.m.offset);
            
            if (MAP_FAILED == start) {
                throw V4L2Exception("mmap failed", errno);
            }
            
            buffers_[i].start = start;
            buffers_[i].length = buf.length;
        } else { // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
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
            // 根据实际情况分配栈(临时)内存
            std::vector<Plane> tmp_planes(buf.length);

            for (int p = 0; p < buf.length; ++p) {
                void* start = mmap(nullptr,
                                   buf.m.planes[p].length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED,
                                   fd_.get(),
                                   buf.m.planes[p].m.mem_offset);
                if (MAP_FAILED == start) {
                /* 这里若 throw 将会自动调用 tmp_planes 的析构
                 * 自动释放内存解决了需要回滚释放的问题
                 */  
                 throw V4L2Exception("mmap failed", errno);
                }
                tmp_planes[p].start = start;
                tmp_planes[p].length = buf.m.planes[p].length;
            }
            // 只有所有平面都mmap成功才将平面给buffers_
            buffers_[i].planes = std::move(tmp_planes);
        }
    }
}

// TODO:需要根据多平面独立分配fd
void CameraController::Impl::allocateDMABuffers() {
    { // 这部分好像没啥用        
        // 清空现有MMAP缓冲区(如果有)
        for (auto& buf : buffers_) {
            if (buf.start) {
                munmap(buf.start, buf.length);
                buf.start = nullptr;
            }
        }
    }

    // 使用DRM API创建DUMB缓冲区
    FdWrapper drm_fd(open("/dev/dri/card0", O_RDWR | O_CLOEXEC));
    if (drm_fd.get() < 0) {
        throw V4L2Exception("Failed to open DRM device", errno);
    }
    // 创建DMABUF缓冲区
    for (int i = 0; i < config_.buffer_count; i++) {
        // 创建 dumb buffer(这里只是内存上的缓冲区,无法交由其他线程或进程直接访问)
        drm_mode_create_dumb create_arg = {};
        create_arg.width = config_.width;
        create_arg.height = config_.height;
        create_arg.bpp = get_bpp(config_.format);
        
        // 如果创建成功将返回 create_arg.handle create_arg.size
        if (ioctl(drm_fd.get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) < 0) {
            throw V4L2Exception("DRM_IOCTL_MODE_CREATE_DUMB failed", errno);
        }

        // 导出 dumb buffer 句柄为 dma-buf fd 以提供给其他线程或进程
        drm_prime_handle prime_arg = {};
        prime_arg.handle = create_arg.handle; // 指定内存 handle
        prime_arg.flags = DRM_CLOEXEC | DRM_RDWR;
    
        if (ioctl(drm_fd.get(), DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_arg) < 0) {
            // 创建失败,要销毁 dumb buffer (handle)
            drm_mode_destroy_dumb destroy_arg = {};
            destroy_arg.handle = create_arg.handle;
            ioctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
    
            throw V4L2Exception("DRM_IOCTL_PRIME_HANDLE_TO_FD failed", errno);
        }
    
        // 成功后保存文件描述符 prime_arg.fd
        buffers_[i].dmabuf_fd = prime_arg.fd;
        buffers_[i].length = create_arg.size;
    
        /* 不再需要使用 handle, 可以销毁 dumb buffer 
         * 这只代表单前的 handle 不再可以使用,不影响导出的 dmabuf fd 
         * 若需要释放物理内存需要释放 dmabuf fd
        */ 
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = create_arg.handle;
        ioctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
    }
    // fd 出作用域自动释放,包括 throw 时 (RAII)    
}

void CameraController::Impl::startStreaming() {
    for (int i = 0; i < buffers_.size(); i++) {
        v4l2_buffer buf = {};
        buf.type = buf_type_;
        buf.index = i;
        buf.memory = memory_type_;

        if (V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type_) {
            if (V4L2_MEMORY_DMABUF == memory_type_) {
                buf.m.fd = buffers_[i].dmabuf_fd;
            }
            // MMAP 不需要填 fd,直接 queue
        } else { // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
            v4l2_plane planes[VIDEO_MAX_PLANES] = {};
            buf.length = buffers_[i].planes.size();
            buf.m.planes = planes;

            if (V4L2_MEMORY_DMABUF == memory_type_) {
                // DMABUF: 每个 plane 填 fd
                for (size_t p = 0; p < buffers_[i].planes.size(); ++p) {
                    planes[p].m.fd = buffers_[i].planes[p].dmabuf_fd;
                    planes[p].length = buffers_[i].planes[p].length;
                }
            } else {
                // MMAP: 不需要额外填 fd,内核知道 offset
                for (size_t p = 0; p < buffers_[i].planes.size(); ++p) {
                    planes[p].length = buffers_[i].planes[p].length;
                }
            }
        }

        if (ioctl(fd_.get(), VIDIOC_QBUF, &buf) < 0) {
            throw V4L2Exception("VIDIOC_QBUF failed", errno);
        }
    }

    // 启动流
    enum v4l2_buf_type type = buf_type_;
    if (ioctl(fd_.get(), VIDIOC_STREAMON, &type) < 0) {
        throw V4L2Exception("VIDIOC_STREAMON failed", errno);
    }
}

void CameraController::Impl::start() {
    if (running_) return;
    
    running_ = true;
    startStreaming();
    capture_thread_ = std::thread(&Impl::captureLoop, this);
}

void CameraController::Impl::stop() {
    if (false == running_) {
        return;
    }
    
    running_ = false;

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

void CameraController::Impl::captureLoop() {
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_.get(), &fds);
        
        timeval tv = {1, 0}; // 1秒超时
        int ret = select(fd_.get() + 1, &fds, nullptr, nullptr, &tv);
        
        if (!running_) break;
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            throw V4L2Exception("select failed", errno);
        }
        
        if (ret == 0) {
            // 超时
            continue;
        }
        
        // 取出缓冲区
        v4l2_buffer buf = {};
        buf.type = buf_type_;
        buf.memory = memory_type_;
        
        if (ioctl(fd_.get(), VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            throw V4L2Exception("VIDIOC_DQBUF failed", errno);
        }
/* @TODO 加入安全队列,这样做的话没有缓冲,全是实时数据,若延时处理了,便会出现崩溃
 * 并且对于出现入队的操作需要重新思考,不能在这入队,或许可以专门调用函数returnBuffer来指定index回收
 */
// 这里的 frame 仅仅是指针或者文件描述符的引用,并未持有实际数据,并非深拷贝,并且Frame禁用了拷贝构造,仅保留移动构造
        // MMAP
        if (false == config_.use_dmabuf) {
            Frame frame(buffers_[buf.index].start, buf.bytesused,
                static_cast<uint64_t>(buf.timestamp.tv_sec) * 1000000ULL + buf.timestamp.tv_usec,
                buf.index);
            if (frame_callback_) frame_callback_(std::move(frame));
        } else { // DMA
            Frame frame(buffers_[buf.index].dmabuf_fd, buf.bytesused,
                static_cast<uint64_t>(buf.timestamp.tv_sec) * 1000000ULL + buf.timestamp.tv_usec,
                buf.index);
            if (frame_callback_) frame_callback_(std::move(frame));
        }

        // // 立即归还缓冲区
        // if (returnBuffer(buf.index) < 0){
        //     throw V4L2Exception("VIDIOC_QBUF failed", errno);
        // }
    }
}

int CameraController::Impl::returnBuffer(int index) {
    v4l2_buffer buf = {};
    buf.type = buf_type_;
    buf.index = index;
    buf.memory = memory_type_;

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
        const size_t num_planes = buffers_[index].planes.size();
        
        // 检查平面数量是否超出内核限制(因为在申请时就设置过planes的大小,大概率不会出现问题)
        if (num_planes > VIDEO_MAX_PLANES) {
            fprintf(stderr, "planes num out of kernel max planes.");
        }
        
        buf.length = num_planes;
        v4l2_plane planes[VIDEO_MAX_PLANES] = {}; // 栈上分配固定大小数组
        buf.m.planes = planes;  // 关联到v4l2_buffer

        if (V4L2_MEMORY_DMABUF == memory_type_) {
            for (size_t p = 0; p < buffers_[index].planes.size(); ++p) {
                planes[p].m.fd = buffers_[index].planes[p].dmabuf_fd;
                planes[p].length = buffers_[index].planes[p].length;
            }
        } else {
            for (size_t p = 0; p < buffers_[index].planes.size(); ++p) {
                planes[p].length = buffers_[index].planes[p].length;
            }
        }
    } else if (V4L2_MEMORY_DMABUF == memory_type_) {
        buf.m.fd = buffers_[index].dmabuf_fd;
    }
    
    if (0 > ioctl(fd_.get(), VIDIOC_QBUF, &buf)) {
        fprintf(stderr, "VIDIOC_QBUF failed in returnBuffer");
    }
    buffers_[index].queued = true;
    return 0;
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
            
            if (0 == returnBuffer(i)){
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
    v4l2_requestbuffers req = {};
    req.count = 0;
    req.type = buf_type_;
    req.memory = memory_type_;

    if (0 > ioctl(fd_.get(), VIDIOC_REQBUFS, &req)) {
        perror("VIDIOC_REQBUFS(0) failed");
    }

    // 清空 vector,自动触发每个 Buffer 析构
    auto temp = std::vector<Buffer>();
    buffers_.swap(temp);
}

void CameraController::Impl::setFrameCallback(FrameCallback&& callback) {
    frame_callback_ = std::move(callback);
}


/* --- CameraController 公共接口实现 --- */
CameraController::CameraController(const Config& config)
    : impl_(std::make_unique<Impl>(config)) {}

CameraController::~CameraController() = default;

void CameraController::start() { impl_->start(); }
void CameraController::stop() { impl_->stop(); }

void CameraController::setFrameCallback(FrameCallback&& callback) {
    impl_->setFrameCallback(std::move(callback));
}
