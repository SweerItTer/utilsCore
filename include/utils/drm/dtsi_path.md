<!--
 * @FilePath: /EdgeVision/include/utils/hdmi/dtsi_path.md
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
`&hdmi_in_vp1`:禁用 VP1，避免冲突。
`&route_hdmi`:指定 HDMI 连接到 VP0 输出。