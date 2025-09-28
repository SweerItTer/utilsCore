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
#include "dma/dmaBuffer.h"    // 包含 drm 头文件

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
    
    void setFrameCallback(FrameCallback&& callback);
    int returnBuffer(int index);

    int getDeviceFd() const;
private:
    void init();
    void setupFormat();
    void requestBuffers();
    void mapBuffers();
    void allocateDMABuffers();
    void startStreaming();
    void captureLoop();

    void reclaimAllBuffers();
    void releaseBuffers();
    
    __u32 currentWidth;
    __u32 currentHeight;

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
        DmaBufferPtr bufptr;
        
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
            if (start) {
                fprintf(stdout, "Unmapping plane at %p, length %zu\n", start, length);
                munmap(start, length);
                start = nullptr;
            }
            // 在版本 86766c63 没有使用 DmaBuffer 封装时使用的 
            if (0 <= dmabuf_fd) {
                fprintf(stdout, "Closing plane dmabuf fd %d\n", dmabuf_fd);
                close(dmabuf_fd);
                dmabuf_fd = -1;
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


        Buffer() = default;
        // 避免拷贝多次析构
        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&&) = default;
        Buffer& operator=(Buffer&&) = default;

        ~Buffer() {
            if (nullptr != start) {
                fprintf(stderr, "Unmapping single-plane buffer at %p, length %zu\n", start, length);
                munmap(start, length);
                start = nullptr;
            }
            if (0 <= dmabuf_fd) {
                fprintf(stderr, "Closing single-plane dmabuf fd %d\n", dmabuf_fd);
                close(dmabuf_fd);
                dmabuf_fd = -1;
            }
            // planes 自动析构,不需要额外做
        }
    };
    std::vector<Buffer> buffers_;
    
    std::mutex _mutex_; // 添加互斥锁
    FrameCallback frame_callback_; // 回调函数 建议使用队列,直接使用数据处理函数容易出现调用混乱
    
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::thread capture_thread_;
};

CameraController::Impl::Impl(const Config& config) 
    : config_(config) {
    try{
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
    } catch (const V4L2Exception& ex) {
        fprintf(stderr, "CameraController : %s\n", ex.what());
        throw;  // 也可以选择不向上传递，或者改成其他处理
    }
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

int CameraController::Impl::getDeviceFd() const {
    return fd_.get();
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
        fmt.fmt.pix_mp.num_planes = config_.plane_count;
    } else {
        fmt.fmt.pix.width = config_.width;
        fmt.fmt.pix.height = config_.height;
        fmt.fmt.pix.pixelformat = config_.format;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }
    
    if (ioctl(fd_.get(), VIDIOC_S_FMT, &fmt) < 0) {
        throw V4L2Exception("VIDIOC_S_FMT failed", errno);
    }

    currentWidth = config_.width;
	currentHeight = config_.height;
    switch (buf_type_)
    {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
        if (fmt.fmt.pix_mp.width != config_.width || fmt.fmt.pix_mp.height != config_.height){
            currentWidth = fmt.fmt.pix_mp.width;
            currentHeight = fmt.fmt.pix_mp.height;
        }
        break;
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
        if(fmt.fmt.pix.width != config_.width || fmt.fmt.pix.height != config_.height){
            currentWidth = fmt.fmt.pix.width;
            currentHeight = fmt.fmt.pix.height;
        }
        break;
    default:
        return;
    }

    fprintf(stderr, "Warning: driver changed resolution to %dx%d\n",
        currentWidth, currentHeight);
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
        buffers_[i].queued = true; // 映射成功后标记入队
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
        size_t plane_num = 1; // 实际 planes 个数
        size_t planes_length = 0; // 实际 v4l2 需求的 planes 长度
        // 判断是否多平面格式
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
            v4l2_buffer buf = {};
            v4l2_plane planes[VIDEO_MAX_PLANES] = {};

            buf.type = buf_type_;
            buf.memory = V4L2_MEMORY_DMABUF;
            buf.index = i;
            buf.m.planes = planes;
            buf.length = config_.plane_count; // 先手动设置平面个数

            if (0 > ioctl(fd_.get(), VIDIOC_QUERYBUF, &buf)) {
                throw V4L2Exception("VIDIOC_QUERYBUF failed", errno);
            }
            
            plane_num = buf.length; // 查询v4l2实际分配平面个数
            if (plane_num == 0 || plane_num > VIDEO_MAX_PLANES) {
                throw V4L2Exception("Invalid number of planes reported by VIDIOC_QUERYBUF", EINVAL);
            }
            // 获取实际需求长度
            planes_length = buf.m.planes[0].length;
            // 修改 buffers_.planes 容器大小为实际 planes 个数
            buffers_[i].planes.resize(plane_num);
        } else {
            // 单平面情况下只需要一个
            plane_num = 1;
            buffers_[i].planes.resize(1);
        }

    // --- 创建DMABUF缓冲区 ---
#ifdef _XF86DRM_H_
        /* V4L2 内核驱动可能做了对齐或 stride 扩展
         * 导致需要的 plane.length 比通过 currentWidth * currentHeight * bpp 估算的分配的大
         */
        for (size_t p = 0; p < plane_num; ++p) {
            auto currentformat = convertV4L2ToDrmFormat(config_.format);
            if (-1 == currentformat){
                throw V4L2Exception("Unsupported V4L2 -> DRM format: " + std::to_string(config_.format));
            }

            // 根据实际长宽分配内存
            DmaBufferPtr buf = DmaBuffer::create(currentWidth, currentHeight, currentformat, planes_length);
            if (nullptr == buf) {
                throw V4L2Exception("create DmaBuffer failed.");
            }

            // 多平面时做 planes_length 校验
            if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
                if (planes_length > buf->size()) {
                    throw V4L2Exception("Allocated dmabuf too small: " +
                        std::to_string(buf->size()) + " < required " + std::to_string(planes_length));
                }
            }

            buffers_[i].planes[p].dmabuf_fd = buf->fd();
            buffers_[i].planes[p].length = buf->size();
            buffers_[i].length += buffers_[i].planes[p].length;
            /* 需要移动保证生命周期
             * shared_ptr 的意义不在这,意义在于给其他硬件共享 prime fd
             * 如果在这里引用 +1 后又 -1 ,白白浪费了一次拷贝销耗的性能
             */
            buffers_[i].planes[p].bufptr = std::move(buf); 
        }
#else // 如果未定义 _XF86DRM_H_ 则保留内核级 ioctl 版本
        for (size_t p = 0; p < plane_num; ++p) {
            // 创建 dumb buffer(这里只是内存上的缓冲区,无法交由其他线程或进程直接访问)
            // 每一个平面都需要创建
            drm_mode_create_dumb create_arg = {};
            create_arg.width = config_.width;
            create_arg.height = config_.height;
            __u32 bpp = get_bpp(config_.format);
            if (-1 == bpp) throw V4L2Exception("Unsupported pixel format in get_bpp (only NV12 and NV16 supported)", errno);
            // 根据格式计算 bpp(bits per pixel),需根据格式对不同 plane 做精细化
            create_arg.bpp = bpp;
            // 如果创建成功将返回 create_arg.handle create_arg.size
            if (0 > ioctl(DmaBuffer::drm_fd.get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg)) {
                throw V4L2Exception("DRM_IOCTL_MODE_CREATE_DUMB failed", errno);
            }

            // 导出 dumb buffer 句柄为 dma-buf fd 以提供给其他线程或进程
            drm_prime_handle prime_arg = {};
            prime_arg.handle = create_arg.handle; // 指定内存 handle
            prime_arg.flags = DRM_CLOEXEC | DRM_RDWR;

            if (0 > ioctl(DmaBuffer::drm_fd.get(), DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime_arg)) {
                // 若创建失败,要销毁 dumb buffer (handle)
                drm_mode_destroy_dumb destroy_arg = {};
                destroy_arg.handle = create_arg.handle;
                ioctl(DmaBuffer::drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);

                throw V4L2Exception("DRM_IOCTL_PRIME_HANDLE_TO_FD failed", errno);
            }

            // 保存当前平面文件描述符 prime_arg.fd
            buffers_[i].planes[p].dmabuf_fd = prime_arg.fd;
            buffers_[i].planes[p].length = create_arg.size;

            // 对于多 plane 的总长度,可以在外面累计
            buffers_[i].length += create_arg.size;
            /* 不再需要使用 handle, 可以销毁 dumb buffer 
            * 这只代表单前的 handle 不再可以使用,不影响导出的 dmabuf fd 
            * 若需要释放物理内存需要释放 dmabuf fd
            */ 
            drm_mode_destroy_dumb destroy_arg = {};
            destroy_arg.handle = create_arg.handle;
            ioctl(DmaBuffer::drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        }
#endif
        // 如果是单平面 plane 数为 1,之执行一次,当个 length 即为总和
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type_) {
            buffers_[i].dmabuf_fd = std::move(buffers_[i].planes[0].dmabuf_fd);
        }
        // 分配成功后标记入队
        buffers_[i].queued = true;
    }
    // fd 出作用域自动释放,包括 throw 时 (RAII)    
}

void CameraController::Impl::startStreaming() {
    for (int i = 0; i < buffers_.size(); i++) {
        v4l2_buffer buf = {};
        v4l2_plane planes[VIDEO_MAX_PLANES] = {};
        
        buf.type = buf_type_;
        buf.index = i;
        buf.memory = memory_type_;

        if (V4L2_BUF_TYPE_VIDEO_CAPTURE == buf_type_) {
            if (V4L2_MEMORY_DMABUF == memory_type_) {
                buf.m.fd = buffers_[i].dmabuf_fd;
            }
            // MMAP 不需要填 fd,直接 queue
        } else { // V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
            buf.length = buffers_[i].planes.size();
            buf.m.planes = planes;

            if (V4L2_MEMORY_DMABUF == memory_type_) {
                // DMABUF: 每个 plane 填 fd
                for (size_t p = 0; p < buffers_[i].planes.size(); ++p) {
                    planes[p].m.fd = buffers_[i].planes[p].dmabuf_fd;
                    planes[p].length = buffers_[i].planes[p].length;
                    planes[p].bytesused = buffers_[i].planes[p].length;
                }
            } 
            else {
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
    if (true == paused_){
        paused_ = false;
        return;
    } else if (false == running_){
        running_ = true;
        startStreaming();
        capture_thread_ = std::thread(&Impl::captureLoop, this);
    } else return;    
    // fprintf(stdout, "MM_type:%s\n", memory_type_ == V4L2_MEMORY_MMAP ? "MMAP" : "DMABUF");
    // fprintf(stdout, "Mplanes:%s\n", buf_type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? "y":"n");
}

void CameraController::Impl::pause(){
    paused_ = true;
}

void CameraController::Impl::stop() {
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

void CameraController::Impl::captureLoop() {
    // static int frame_count = 0;
    // double current_fps_ = 0.0;
    // uint64_t time_span = 0;
    // uint64_t frame_times_old = 0;
    // uint64_t frame_times_now = 0;
    try
    {
    while (running_) {
        if (true == paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (false == running_) break;
            continue;
        }
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd_.get(), &fds);
        
        timeval tv = {1, 0}; // 1秒超时
        int ret = select(fd_.get() + 1, &fds, nullptr, nullptr, &tv);
        
        if (false == running_) break;
        
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
        v4l2_plane planes[VIDEO_MAX_PLANES] = {};  // 为多平面准备数组
        buf.type = buf_type_;
        buf.memory = memory_type_;
        // 多平面需要指定 plane 的 length 和m.planes 或者 fd
        // 根据缓冲区类型进行不同设置
        if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
            // 多平面处理
            buf.length = buffers_[0].planes.size();     // 最大平面数
            buf.m.planes = planes;                      // 指向平面数组
        } else {
            // 单平面处理
            // 对于DMABUF需要设置长度
            if (V4L2_MEMORY_DMABUF == memory_type_) {
                buf.length = buffers_[0].length;
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(_mutex_);
            if (ioctl(fd_.get(), VIDIOC_DQBUF, &buf) < 0) {
                if (errno == EAGAIN) continue;
                throw V4L2Exception("VIDIOC_DQBUF failed", errno);
            }
            // 标记缓冲区已出队
            buffers_[buf.index].queued = false;
        }
        /* 自动释放锁
         * 为什么这里就释放了锁?
         * 在我的预想里回调函数应该是一个打包的入队函数
         * 但是不保证所有人对该函数的理解一致,可能在回调函数内调用 returnBuffer 函数
         * 若不释放锁,在这样的情况下将会死锁 (在锁的范围里去抢锁,但是抢不到,欸,就死了)
         */

        uint64_t ts = static_cast<uint64_t>(buf.timestamp.tv_sec) * 1000000ULL + buf.timestamp.tv_usec;

// 这里的 frame 仅仅是指针或者文件描述符的引用,并未持有实际数据,并非深拷贝,并且Frame禁用了拷贝构造,仅保留移动构造
        // 先声明后构造(美观而已,用多层if效果一样)
        std::unique_ptr<Frame> frame_opt;

        if (false == config_.use_dmabuf) {
            if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
                if (buf.length < 1) {
                    throw V4L2Exception("Invalid number of planes", EINVAL);
                }
                // 该部分基于一个理论实现(不保证全部平台支持)(连续内存区域不需要传递所有平面指针) 见 utils/rga/rgaConverter.h L27
                void* plane0_ptr = buffers_[buf.index].planes[0].start;
            
                size_t total_size = 0;
                for (uint32_t i = 0; i < buf.length; ++i) {
                    // 获取所有平面占用空间总和
                    total_size += buf.m.planes[i].length;
                }
                frame_opt = std::make_unique<Frame>(plane0_ptr, total_size, ts, buf.index);
            } else { // 单平面
                frame_opt = std::make_unique<Frame>(
                    buffers_[buf.index].start,
                    buf.bytesused,
                    ts,
                    buf.index
                );
            }
        } else {
            if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == buf_type_) {
                // --- 采用单平面连续物理内存的思想处理多平面
                if (buf.length < 1) {
                    throw V4L2Exception("Invalid number of planes", EINVAL);
                }
                int plane0_fd = buffers_[buf.index].planes[0].dmabuf_fd;
            
                size_t total_size = 0;
                for (uint32_t i = 0; i < buf.length; ++i) {
                    // 获取所有平面占用空间总和
                    total_size += buf.m.planes[i].length;
                }
                frame_opt = std::make_unique<Frame>(plane0_fd, total_size, ts, buf.index);
            } else {
                frame_opt = std::make_unique<Frame>(
                    buffers_[buf.index].dmabuf_fd,
                    buf.bytesused,
                    ts,
                    buf.index
                );
            }
        }
        // 回调传递
        if (frame_callback_ && frame_opt) {
            frame_callback_(std::move(*frame_opt));
        } else returnBuffer(buf.index); // 未设置正确的回调时需要手动回收缓冲区
        
        // // 帧率计算
        // frame_times_now = ts;
        // time_span = frame_times_now - frame_times_old;
        // if (time_span > 0) {
        //     current_fps_ = 1000000.0 / static_cast<double>(time_span);
        // }
        // if (++frame_count % 30 == 0) {
        //     fprintf(stdout, "v4l2 fps:%.1f\n", current_fps_);
        //     frame_count = 0;
        // }
        // frame_times_old = frame_times_now;
    }
    } catch (const std::exception& e) {
        fprintf(stderr, "Capture loop error: %s\n", e.what());
        running_ = false;
    }
}

int CameraController::Impl::returnBuffer(int index) {
    std::lock_guard<std::mutex> lock(_mutex_);
    v4l2_buffer buf = {};
    v4l2_plane planes[VIDEO_MAX_PLANES] = {}; // 栈上分配固定大小数组

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
    } else { // V4L2_BUF_TYPE_VIDEO_CAPTURE
        if (V4L2_MEMORY_DMABUF == memory_type_) {
            buf.m.fd = buffers_[index].dmabuf_fd;
        }
    }
    
    int ret = ioctl(fd_.get(), VIDIOC_QBUF, &buf);
    if (0 > ret) {
        fprintf(stderr, "VIDIOC_QBUF failed in returnBuffer (errno=%d): %s\n", errno, strerror(errno));
    }
    // 当操作成功即入队
    buffers_[index].queued = (0 == ret);
    return ret;
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
    // v4l2_requestbuffers req = {};
    // req.count = 0;
    // req.type = buf_type_;
    // req.memory = memory_type_;

    // if (0 > ioctl(fd_.get(), VIDIOC_REQBUFS, &req)) {
    //     perror("VIDIOC_REQBUFS(0) failed");
    // }

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
void CameraController::pause(){ impl_->pause(); }

void CameraController::setFrameCallback(FrameCallback &&callback){ impl_->setFrameCallback(std::move(callback)); }

int CameraController::getDeviceFd() const { return impl_->getDeviceFd(); }

void CameraController::returnBuffer(int index){ impl_->returnBuffer(index); }
