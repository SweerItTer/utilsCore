/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-15 03:05:23
 * @FilePath: /EdgeVision/include/UI/rander/core.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
// 该类主要实现GPU上下文管理和DMABUF到EGLImage的导入
#ifndef CORE_H
#define CORE_H

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <EGL/egl.h>
#include <EGL/eglext.h>     // EGL 扩展

#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>         // Qt 封装的 OpenGL 基础函数
#include <QOpenGLFramebufferObject>
#include <QOpenGLExtraFunctions>    // OpenGL 扩展功能(glEGLImageTargetTexture2DOES)

#include "dma/dmaBuffer.h"
#include "objectsPool.h"

static void printGLESInfo() {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version  = glGetString(GL_VERSION);
    const GLubyte* vendor   = glGetString(GL_VENDOR);
    const GLubyte* shading  = glGetString(GL_SHADING_LANGUAGE_VERSION);

    printf("[Core] OpenGL ES info:\n");
    printf("\tRenderer: %s\n", renderer);
    printf("\tVersion : %s\n", version);
    printf("\tVendor  : %s\n", vendor);
    printf("\tGLSL    : %s\n", shading);
}

static void printEglError(const char* where) {
    EGLint err = eglGetError();
    fprintf(stderr, "[Core]\t[EGL] %s eglGetError: 0x%04x\n", where, err);
}

static void printGlError(const char* where) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        fprintf(stderr, "[Core]\t[GL ERROR] %s: 0x%x\n", where, err);
    }
}

static void cpuFull(DmaBufferPtr newBuf){
    uint8_t* data = newBuf->map();
    if (nullptr != data) {
        // 假设填充 RGBA 或 XR24
        for (uint32_t y = 0; y < newBuf->height(); ++y) {
            for (uint32_t x = 0; x < newBuf->width(); ++x) {
                uint32_t idx = y * newBuf->pitch() + x * 4; // 4 字节 per pixel
                data[idx + 0] = 255; // R
                data[idx + 1] = 0;   // G
                data[idx + 2] = 0;   // B
                data[idx + 3] = 255; // A
            }
        }
        newBuf->unmap(); // 解除映射
    }
}

/**
 * @brief 核心 GPU 上下文管理类
 * 
 * 管理 OpenGL 上下文, 离屏 surface, FBO 与 DMABUF 的映射
 * 支持多缓冲循环使用
 */
class Core {
    // 拓展函数
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
    PFNEGLQUERYDMABUFFORMATSEXTPROC eglQueryDmaBufFormatsEXT;
    PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT;
    PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR = nullptr;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID = nullptr;
    PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR = nullptr;
    
public:
    struct resourceSlot;
    static Core& instance();
    void shutdown();

    bool queryAllFormats(uint32_t targetFmt);
    // 注册具用途的 dmabuf, EGLImage 并 attach 纹理到 FBO (传入临时模板,生命周期非常短)
    bool registerResSlot(const std::string& type, size_t poolSize, DmaBufferPtr &&bufTemplate);
    bool registerResSlot(const std::string& type, size_t poolSize, 
        uint32_t width, uint32_t height, uint32_t format, uint32_t required_size, uint32_t offset);
        
    // 取出一个 slot
    std::shared_ptr<resourceSlot> acquireFreeSlot(const std::string &type, int timeout_ms=10);
    // 回收 slot
    void releaseSlot(const std::string& type, std::shared_ptr<resourceSlot>& slot);

    EGLDisplay getEglDisplay() const { return eglDisplay_; }
        
    // 获取当前OpenGL上下文
    QOpenGLContext* getGLContext() const { return glContext_.get(); }

    /* 何意味?
     * 因为上下文的makeCurrent是和线程绑定的
     * 外界要是想使用 core 创建的上下文和离屏渲染平面需要再调用一次makeCurrent
     */
    // 绑定
    bool makeQCurrent() const { return glContext_->makeCurrent(offscreenSurface_.get()); }
    
    // 解绑 (用途: 明确不需要是情况, 比如上下文切换)
    void doneQCurrent() const { glContext_->doneCurrent(); }

private:
    Core();
    ~Core() = default;
    bool initQContext();
    bool initializeExtensions();
    resourceSlot createSlot(DmaBufferPtr&& bufPtr);

private:
    // EGL 原生资源(统一上下文实现DMABUF->EGLImage)
    EGLDisplay eglDisplay_  = EGL_NO_DISPLAY;
    EGLContext eglCtx_      = EGL_NO_CONTEXT;
    EGLSurface eglSurface_  = EGL_NO_SURFACE;
    // qt 统一上下文
    std::unique_ptr<QOpenGLContext> glContext_;
    std::unique_ptr<QOffscreenSurface> offscreenSurface_;
    
    // 管理不同用处的 slot
    std::mutex slotMutex;
    using slotPool = ObjectPool<std::shared_ptr<resourceSlot>>;
    std::unordered_map<std::string, slotPool> slots_;
};

struct Core::resourceSlot {
    DmaBufferPtr dmabufPtr = nullptr;
    EGLImageKHR eglImage = EGL_NO_IMAGE_KHR;
    GLuint textureId = 0;   
    GLuint blitFbo = 0;     // 直接作为绘制目标

    // [修改] 移除了 qfbo
    // std::shared_ptr<QOpenGLFramebufferObject> qfbo;

    bool getSyncFence(int& fence);    
    uint32_t width() const { return dmabufPtr ? dmabufPtr->width() : 0; }
    uint32_t height() const { return dmabufPtr ? dmabufPtr->height() : 0; }
    
    bool valid() const { 
        return EGL_NO_IMAGE_KHR != eglImage && blitFbo != 0 && textureId != 0;
    }
    
    ~resourceSlot() { cleanup(); }
    resourceSlot() = default;
    resourceSlot(const resourceSlot&) = delete;
    resourceSlot& operator=(const resourceSlot&) = delete;
    
    resourceSlot(resourceSlot&& other) noexcept {
        *this = std::move(other);
    }
    
    resourceSlot& operator=(resourceSlot&& other) noexcept {
        if (this != &other) {
            cleanup();
            dmabufPtr = std::move(other.dmabufPtr);
            eglImage = other.eglImage;
            textureId = other.textureId;
            blitFbo = other.blitFbo;
            // qfbo = std::move(other.qfbo); // 移除
            
            other.eglImage = EGL_NO_IMAGE_KHR;
            other.textureId = 0;
            other.blitFbo = 0;
        }
        return *this;
    }
private:
    void cleanup();
};

#endif // CORE_H