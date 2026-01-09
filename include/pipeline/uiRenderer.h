/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-13 05:24:05
 * @FilePath: /EdgeVision/include/pipeline/uiRenderer.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <QTimer>

#include "displayManager.h" // drm显示
#include "m_types.h"  // 模型输出

#include "rander/core.h"    // 绘制核心
#include "rander/draw.h"    // 绘制接口

// UI 界面
#include "ConfigInterface/maininterface.h"

class UIRenderer {
public:
    using fpsUpdater=std::function<float(void)>;
    UIRenderer(const std::string& slotTypeName="default");
    ~UIRenderer();
    
    // 初始化 QWidget 和 QOpenGLContext
    void init();
    
    // 启动线程
    void start();
    // 退出线程
    void stop();
    // 带刷新标志的暂停
    void pause(bool refreshing=false);
    // 唤醒
    void resume();

    // 重置屏幕区域
    void resetTargetSize(const std::pair<uint32_t, uint32_t>& size);
    // 重置planeHandle
    void resetPlaneHandle(const DisplayManager::PlaneHandle& handle);
    // 重置Widget绘制位置
    void resetWidgetTargetRect(const DrawRect& targetRect);

    // 绑定显示器
    void bindDisplayer(std::weak_ptr<DisplayManager> displayer);
    // 获取当前widget(非语义安全, 若获取后对UI做了未知操作可能出现问题, 仅作为"偷懒"注册信号用)
    const MainInterface* getCurrentWidgetUnsafe() const;
    
    // 加载光标
    void loadCursorIcon(const std::string& iconPath);
    
    // 绘制文本
    void drawText(const QString &text, const QPointF &pos, const QColor &color, int fontSize);
    // 更新Yolo结果
    void updateBoxs(object_detect_result_list&& ret);
    // 更新fps
    void setFPSUpdater(const fpsUpdater& cb);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};