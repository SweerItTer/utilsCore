<!--
 * @FilePath: /include/utils/drm/dtsi_path.md
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-11 18:23:55
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
-->
- 设备树路径
`/path/to/kernel/arch/arm64/boot/dts/rockchip/rk3568-evb.dtsi`

- 修改hdmi相关配置
```dtsi
/* 使能 VP0 */
&hdmi_in_vp0 {
    status = "okay";
};

/* 禁用 VP1 */
&hdmi_in_vp1 {
    status = "disabled";
};

/* 配置 HDMI 使用 VP0 输出 */
&route_hdmi {
    status = "okay";
    connect = <&vp0_out_hdmi>;
};

```

`&hdmi_in_vp0`:启用 VP0 作为 HDMI 输入。
`&hdmi_in_vp1`:禁用 VP1, 避免冲突。
`&route_hdmi`:指定 HDMI 连接到 VP0 输出。

<!-- 
./kernel/arch/arm64/boot/dts/rockchip/rk3568-screen_choose.dtsi

//#define ATK_LCD_TYPE_MIPI_720P		    // 从VP1输入
//#define ATK_LCD_TYPE_MIPI_1080P	        // 从VP1输入
//#define ATK_LCD_TYPE_MIPI_10P1_800X1280	// 从VP1输入 
-->
<!-- 该文件配置了宏导致在rk3568-lcds.dtsi初始化LCD屏幕(哪怕没有接) -->

```bash
#!/bin/bash

INTERVAL=2  # 监控间隔(秒)

echo "开始监控 DRM 资源使用情况..."
echo "按 Ctrl+C 停止"

while true; do
    clear
    echo "=== $(date) ==="
    echo ""
    
    # 查看系统内存状态
    echo "--- 系统内存状态 ---"
    free -h
    
    echo ""
    echo "--- DRM 状态 ---"
    
    # 检查 debugfs 是否挂载
    if mount | grep -q debugfs; then
        # Framebuffer 信息
        if [ -f /sys/kernel/debug/dri/0/framebuffer ]; then
            echo "Framebuffers:"
            cat /sys/kernel/debug/dri/0/framebuffer | grep -E "(fb_id|size|refcount)"
        fi
        
        # GEM 对象信息
        if [ -f /sys/kernel/debug/dri/0/gem ]; then
            echo ""
            echo "GEM 对象:"
            cat /sys/kernel/debug/dri/0/gem | head -10
        fi
        
        # Plane 状态
        if [ -f /sys/kernel/debug/dri/0/planes ]; then
            echo ""
            echo "Plane 状态:"
            cat /sys/kernel/debug/dri/0/planes
        fi
    else
        echo "debugfs 未挂载, 无法获取详细 DRM 信息"
        echo "挂载命令: sudo mount -t debugfs none /sys/kernel/debug"
    fi
    
    echo ""
    echo "--- 进程资源使用 ---"
    # 查看当前进程的资源使用
    if pgrep -x "utils_test"; then
        PID=$(pgrep -x "utils_test")
        echo "进程 $PID 资源使用:"
        ps -p $PID -o pid,ppid,rss,vsz,pcpu,pmem,cmd
    fi
    
    sleep $INTERVAL
    clear
done
```