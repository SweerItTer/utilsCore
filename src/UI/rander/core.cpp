/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-15 03:29:56
 * @FilePath: /EdgeVision/src/UI/rander/core.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
/* 需要明确的是,这里获取 eglDisplay_/eglCtx_ 本质上和qt毫无关系
* 因为该类目前仅仅是需要实现DMABUF->EGL,而EGLimage可以跨上下文
* display 的作用也只是因为 image 依赖它的生命周期
* 所以本质上完全可以全权使用EGL原生接口!!!!
* 那么现在的唯一合理解释就是因为qt不支持导入原生FBO
* 如果是原生FBO, 就无法通过QPainter修改内容
* 并且原生FBO的修改依赖上下文, 我如果这里不使用qt提供的上下文接口
* 哪怕可以修改FBO,上下文不一致又会无法修改
* 总而言之, 如果不是为了方便使用QPainter, 完全不需要使用Qt给的接口
*/
#include "rander/core.h"
#include <csignal>
#include <sys/mman.h>
#include <cstring>
#include <unistd.h>

#include <QVariant>
#include <QImage>
#include <QtPlatformHeaders/QEGLNativeContext>

static void saveFBO(GLuint Fbo, uint32_t width, uint32_t height, const QString& path){
    // 保存 blitfbo 内容
    glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
    std::vector<uint8_t> pixels(width * height * 4);
    glReadPixels(0, 0,
        width,
        height,
        GL_RGBA, GL_UNSIGNED_BYTE,
        pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // 保存成 PNG 或 BMP 验证
    QImage bimg(pixels.data(),
        width,
        height,
        QImage::Format_RGBA8888);
    if (!bimg.save(path)){
        fprintf(stderr, "Failed to save fbo texture\n");
    } else {
        fprintf(stdout, "Save fbo texture as %s\n", path.toStdString().c_str());
    }
}

Core& Core::instance()
{
    static Core core;
    return core;
}

Core::Core()
{
    // 初始化qt上下文
    if (false == initQContext()) {
        fprintf(stderr, "[Core] initContext failed.\n");
    }
    // 获取扩展函数指针
    if (initializeExtensions()) {
        // queryAllFormats();
    }
}

void Core::shutdown() {
    std::lock_guard<std::mutex> slotLock(slotMutex);
    // 清理所有资源池
    slots_.clear();

    // 清理所有依赖 Qt/EGL 的资源
    glContext_.reset();
    offscreenSurface_.reset();
    fprintf(stderr, "[Core] shutdown complete\n");
}


Core::~Core() {
    // 覆盖上下文和display(qt回收)
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglCtx_ = EGL_NO_CONTEXT;
        eglDisplay_ = EGL_NO_DISPLAY;
    }
}

bool Core::initQContext() {
    // 创建Qt的OpenGL上下文, 使用原生EGL资源
    glContext_ = std::make_unique<QOpenGLContext>();
    if (!glContext_) {
        fprintf(stderr, "[Core] Failed to create Qt OpenGL context object\n");
        return false;
    }
    // 设置格式
    QSurfaceFormat format;
    format.setVersion(2, 0);
    format.setProfile(QSurfaceFormat::NoProfile);
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setSwapBehavior(QSurfaceFormat::SingleBuffer);
    glContext_->setFormat(format);
    
    // 创建离屏表面
    offscreenSurface_ = std::make_unique<QOffscreenSurface>();
    offscreenSurface_->setFormat(format);
    offscreenSurface_->create();
    
    if (!offscreenSurface_->isValid()) {
        fprintf(stderr, "[Core] Failed to create Qt offscreen surface\n");
        return false;
    }
    fprintf(stdout, "[Core] Qt offscreen surface created successfully\n");
    
    // 创建Qt OpenGL上下文
    if (!glContext_->create()) {
        fprintf(stderr, "[Core] Failed to create Qt OpenGL context\n");
        return false;
    }
    fprintf(stdout, "[Core] Qt OpenGL context created successfully\n");
    
    // 验证Qt上下文是否使用了我们的EGL资源
    if (!makeQCurrent()) {
        fprintf(stderr, "[Core] Failed to make Qt context current\n");
        return false;
    }
    fprintf(stdout, "[Core] Qt context made current successfully\n");

    eglDisplay_ = eglGetCurrentDisplay();
    eglCtx_ = eglGetCurrentContext();
    if (eglDisplay_ == EGL_NO_DISPLAY || eglCtx_ == EGL_NO_CONTEXT) {
        fprintf(stderr, "[Core] Cannot get native EGL resources form qt.\n");
    }
    fprintf(stdout, "[Core] EGL resources (display: %p, context: %p)\n", eglDisplay_, eglCtx_);

    fprintf(stdout, "[Core] Context initialization completed.\n");
    doneQCurrent();
    return true;
}

bool Core::initializeExtensions() {
    makeQCurrent();
    // EGL Image 扩展
    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");

    // GL 扩展
    glEGLImageTargetTexture2DOES =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");

    // DMA-BUF 格式/修饰符查询
    eglQueryDmaBufFormatsEXT =
        (PFNEGLQUERYDMABUFFORMATSEXTPROC) eglGetProcAddress("eglQueryDmaBufFormatsEXT");
    eglQueryDmaBufModifiersEXT =
        (PFNEGLQUERYDMABUFMODIFIERSEXTPROC) eglGetProcAddress("eglQueryDmaBufModifiersEXT");

    if (!eglCreateImageKHR || !eglDestroyImageKHR || !glEGLImageTargetTexture2DOES) {
        fprintf(stderr, "[Core] Missing essential EGL/GL extension functions\n");
        return false;
    }

    if (!eglQueryDmaBufFormatsEXT || !eglQueryDmaBufModifiersEXT) {
        fprintf(stderr, "[Core] Warning: EGL does not support dma-buf format/modifier query\n");
        // 这里不能直接 return false, 因为某些驱动没实现查询接口, 但 eglCreateImageKHR 仍然能用
        // 所以只给出警告, 后续 createSlot 时依旧可以尝试
    }
    eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    eglDupNativeFenceFDANDROID = (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
    eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");

    if (!eglCreateSyncKHR || !eglDupNativeFenceFDANDROID || !eglDestroySyncKHR) {
        fprintf(stderr, "Failed to load EGL fence extensions\n");
        return false;
    }

    printGLESInfo();
    doneQCurrent();
    return true;
}

bool Core::queryAllFormats(uint32_t targetFmt) {
    // 先获取支持的格式数量
    EGLint numFormats = 0;
    eglQueryDmaBufFormatsEXT(eglDisplay_, 0, nullptr, &numFormats);
    if (numFormats <= 0) return false;

    std::vector<EGLint> formats(numFormats);
    eglQueryDmaBufFormatsEXT(eglDisplay_, numFormats, formats.data(), &numFormats);

    bool flag = false;
    for (EGLint fmt : formats) {
        std::string fmtStr = fourccToString(fmt);
        if (targetFmt != 0){
            if (targetFmt == fmt) {
                fprintf(stdout, "[core] Find matched format: %s\n", fmtStr.c_str());
                flag = true;
                break;
            }
            continue;
        }
        fprintf(stdout, "EGL supports format: %s\n", fmtStr.c_str());

        // 查询对应的 modifier
        EGLint numMods = 0;
        eglQueryDmaBufModifiersEXT(eglDisplay_, fmt, 0, nullptr, nullptr, &numMods);
        if (numMods <= 0) continue;

        std::vector<EGLuint64KHR> modifiers(numMods);
        std::vector<EGLBoolean> externalOnly(numMods);
        eglQueryDmaBufModifiersEXT(eglDisplay_, fmt, numMods, modifiers.data(), externalOnly.data(), &numMods);

        for (int i = 0; i < numMods; i++) {
            fprintf(stdout, "  modifier=0x%llX externalOnly=%d\n",
                    (unsigned long long)modifiers[i], externalOnly[i]);
        }
    }
    if (targetFmt != 0 && flag == false){
        fprintf(stdout, "[core] Not find supports format.\n");
    }
    return flag;
}

std::shared_ptr<Core::resourceSlot> Core::acquireFreeSlot(const std::string &type, int timeout_ms) {
    std::lock_guard<std::mutex> slotLock(slotMutex);
    const auto& it = slots_.find(type);
    if (slots_.find(type) == slots_.end()) return nullptr;
    std::shared_ptr<Core::resourceSlot> slot{nullptr};
    if (!it->second.tryAcquire(slot, std::chrono::milliseconds(timeout_ms))){
        fprintf(stderr, "[core] Get slot timeout.\n");
    }
    return slot;
}

void Core::releaseSlot(const std::string &type, std::shared_ptr<resourceSlot> &slot) {
    std::lock_guard<std::mutex> slotLock(slotMutex);
    const auto& it = slots_.find(type);
    if (it == slots_.end()) return;
    it->second.release(slot);
}

bool Core::registerResSlot(const std::string &type, size_t poolSize,
    uint32_t width, uint32_t height, uint32_t format, uint32_t required_size, uint32_t offset)
{
    if (poolSize <= 0) {
        fprintf(stderr, "[Core] Inviled parameter.\n");
        return false;
    }
    if (!queryAllFormats(format)) return false;
    // 创建模板buf
    DmaBufferPtr bufTemplate = DmaBuffer::create(width, height, format, required_size, offset);
    if ( nullptr == bufTemplate ){
        fprintf(stderr, "[Core] Failed to create dmabuf.\n");
        return false;
    }
    return registerResSlot(type, poolSize, std::move(bufTemplate));
}

bool Core::registerResSlot(const std::string &type, size_t poolSize, DmaBufferPtr &&bufTemplate)
{
    std::lock_guard<std::mutex> slotLock(slotMutex);
    if (poolSize <= 0 || nullptr == bufTemplate) {
        fprintf(stderr, "[Core] Inviled parameter.\n");
        return false;
    }
    if (!queryAllFormats(bufTemplate->format())) {
        fprintf(stderr, "[Core] DMABUF format not support.\n");
        return false;
    }
    if (slots_.find(type) != slots_.end()) {
        slots_.erase(type);
    }
    // buf模板并非实际存储内容
    slots_.emplace(std::piecewise_construct,
        std::forward_as_tuple(type),
        std::forward_as_tuple(poolSize, [this, bufTemplate = std::move(bufTemplate)]() {
            // 为每个slot创建新的DMA缓冲区
            DmaBufferPtr newBuf = DmaBuffer::create(
                bufTemplate->width(), 
                bufTemplate->height(), 
                bufTemplate->format(),
                bufTemplate->size(),
                bufTemplate->offset(),
                0
            );
            if (!newBuf) {
                fprintf(stderr, "[Core] Failed to create DMABUF.\n");
                return std::shared_ptr<resourceSlot>();
            }
            return std::make_shared<resourceSlot>(std::move(createSlot(std::move(newBuf))));
        })
    );
    fprintf(stdout, "[Core] Successed to create %u slot .\n", slots_.at(type).freeCount());
    return true;
}

Core::resourceSlot Core::createSlot(DmaBufferPtr&& bufPtr) {
    if (nullptr == bufPtr) {
        fprintf(stderr, "[Core] Invalid DmaBuffer\n");
        return {};
    }
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        fprintf(stderr, "[Core] No valid EGL display\n");
        return {};
    }
    resourceSlot slot;
    slot.dmabufPtr = std::move(bufPtr);

    makeQCurrent();
    // 创建 EGLImage
    EGLint attrs[] = {
        EGL_WIDTH, static_cast<EGLint>(slot.dmabufPtr->width()),
        EGL_HEIGHT, static_cast<EGLint>(slot.dmabufPtr->height()),
        EGL_DMA_BUF_PLANE0_FD_EXT, slot.dmabufPtr->fd(),
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLint>(slot.dmabufPtr->offset()),
        EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(slot.dmabufPtr->pitch()),
        EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(slot.dmabufPtr->format()),
        EGL_NONE
    };
    slot.eglImage = eglCreateImageKHR(
        eglDisplay_,
        EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        nullptr,
        attrs
    );
    if (slot.eglImage == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "[Core] Failed to create EGLImage: 0x%X\n", eglGetError());
        return {};
    }

    // 创建 GL 纹理 (RGBA)
    glGenTextures(1, &slot.textureId);
    glBindTexture(GL_TEXTURE_2D, slot.textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, slot.dmabufPtr->width(),
                 slot.dmabufPtr->height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, slot.eglImage);
    printGlError("glEGLImageTargetTexture2DOES");
    glBindTexture(GL_TEXTURE_2D, 0);

    // 创建 blitFbo 用于同步 (RGBA)
    glGenFramebuffers(1, &slot.blitFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot.blitFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.textureId, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[Core] Framebuffer incomplete: 0x%X\n", status);
        return {};
    }
    printGlError("blitfbo create.");
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // 创建 QOpenGLFramebufferObject, 指定支持 RGBA
    QOpenGLFramebufferObjectFormat fboFormat;
    fboFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil); // 可选深度/模板
    fboFormat.setInternalTextureFormat(GL_RGBA); // 关键: RGBA 支持透明
    slot.qfbo = std::make_shared<QOpenGLFramebufferObject>(slot.dmabufPtr->width(),
                                                           slot.dmabufPtr->height(),
                                                           fboFormat);

    doneQCurrent();
    fprintf(stdout, "[Core] Resource slot (RGBA) created successfully\n");
    return slot;
}


// 将 QtFBO 的内容同步到 dmabuf
bool Core::resourceSlot::syncToDmaBuf(int& fence) {
    if (!valid() || !qfbo) {
        return false;
    }
    fence = 0;

    auto& core = Core::instance();
    auto width = dmabufPtr->width();
    auto height = dmabufPtr->height();
    core.makeQCurrent();
    // 确保 blitFbo 存在不存在则重新创建
    if (0 == blitFbo) {
        glGenFramebuffers(1, &blitFbo);
    }
    // 重新 attach dmabuf 的 texture
    glBindFramebuffer(GL_FRAMEBUFFER, blitFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           textureId,  // 这里是保存的 dmabuf texture
                           0);
    // 检查状态
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (GL_FRAMEBUFFER_COMPLETE != status) {
        fprintf(stderr, "[FBO ERROR] blitFbo not complete after reattach: 0x%x\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        core.doneQCurrent();
        return false;
    }

    // // 保存检查
    // saveFBO(blitFbo, width, height, "/tmp/blitFbo_synccheck.png");

    // 2. 分别绑定读取和写入的 fbo
    glBindFramebuffer(GL_READ_FRAMEBUFFER, qfbo->handle());
    printGlError("bind qfbo");
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitFbo);
    printGlError("bind blitFbo");

    // 检查状态
    GLenum readStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    GLenum drawStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (GL_FRAMEBUFFER_COMPLETE != readStatus) {
        fprintf(stderr, "[FBO ERROR] qfbo tmpFbo not complete: 0x%x\n", readStatus);
    }
    if (GL_FRAMEBUFFER_COMPLETE != drawStatus) {
        fprintf(stderr, "[FBO ERROR] blitFbo not complete: 0x%x\n", drawStatus);
    }

    // 3. 执行 blit
    glBlitFramebuffer(0, 0, width, height,
                      0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    printGlError("blit framebuffer");

    // // 4. 创建 fence
    // EGLSyncKHR sync = core.eglCreateSyncKHR(core.eglDisplay_,
    //                                         EGL_SYNC_FENCE_KHR,
    //                                         nullptr);
    // glFlush();
    // printGlError("flush");
    // fence = core.eglDupNativeFenceFDANDROID(core.eglDisplay_, sync);
    // core.eglDestroySyncKHR(core.eglDisplay_, sync);

    // 5. 清理绑定
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // saveFBO(blitFbo, width, height, "/tmp/blitFbo_copycheck.png");

    core.doneQCurrent();
    return true;
}

void Core::resourceSlot::cleanup() {
    Core::instance().makeQCurrent();
    // 先释放FBO
    if (qfbo) {
        qfbo->release();
        qfbo.reset();
    }
    
    // 释放同步用 FBO
    if (blitFbo != 0) {
        glDeleteFramebuffers(1, &blitFbo);
        blitFbo = 0;
    }

    // 释放纹理
    if (textureId != 0) {
        glDeleteTextures(1, &textureId);
        textureId = 0;
    }
    
    // 销毁EGL图像
    if (EGL_NO_IMAGE_KHR != eglImage) {
        Core::instance().eglDestroyImageKHR(Core::instance().getEglDisplay(), eglImage);
        eglImage = EGL_NO_IMAGE_KHR;
    }

    // 释放DMA缓冲区
    if (nullptr != dmabufPtr) {
        dmabufPtr.reset();
        dmabufPtr = nullptr;
    }
    Core::instance().doneQCurrent();
}
