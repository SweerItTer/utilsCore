#ifndef CORE_H
#define CORE_H

#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLExtraFunctions>

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

class Core {
    // 拓展函数定义保持不变
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
    bool registerResSlot(const std::string& type, size_t poolSize, DmaBufferPtr &&bufTemplate);
    bool registerResSlot(const std::string& type, size_t poolSize, 
        uint32_t width, uint32_t height, uint32_t format, uint32_t required_size, uint32_t offset);
        
    std::shared_ptr<resourceSlot> acquireFreeSlot(const std::string &type, int timeout_ms=10);
    void releaseSlot(const std::string& type, std::shared_ptr<resourceSlot>& slot);

    EGLDisplay getEglDisplay() const { return eglDisplay_; }
    QOpenGLContext* getGLContext() const { return glContext_.get(); }
    bool makeQCurrent() const { return glContext_->makeCurrent(offscreenSurface_.get()); }
    void doneQCurrent() const { glContext_->doneCurrent(); }

private:
    Core();
    ~Core();
    
    bool initQContext();
    bool initializeExtensions();
    resourceSlot createSlot(DmaBufferPtr&& bufPtr);

private:
    EGLDisplay eglDisplay_  = EGL_NO_DISPLAY;
    EGLContext eglCtx_      = EGL_NO_CONTEXT;
    std::unique_ptr<QOpenGLContext> glContext_;
    std::unique_ptr<QOffscreenSurface> offscreenSurface_;
    
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

    bool syncToDmaBuf(int& fence);    
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