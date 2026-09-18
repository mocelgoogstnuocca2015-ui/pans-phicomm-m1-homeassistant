# 第三方来源与发布范围

本仓库只发布 M1 应用层实现、测试、工具与文档。公开可访问不等于获得任意再分发许可。

## MXCHIP MiCO

- 来源：[MXCHIP/mico-os](https://github.com/MXCHIP/mico-os)。开发基线提交为 `9b09de78164940ff3876d2053f8e7dd42ca2b8ba`。
- MiCO 包含不同许可的文件，不能整体按本项目许可重新授权。部分系统源码明确标记 `UNPUBLISHED PROPRIETARY SOURCE CODE`，README 也有 `Internal use only`。
- 本库不附带 SDK、MiCoder 工具链、Bootloader、无线内核或完整链接后二进制。使用者需自行确认合法获取和使用条件。
- `libraries/daemons/ota_server` 的文件头包含 MXCHIP 的 MIT 授权；SDK 的 MQTT 源码文件头注明 IBM / Eclipse Paho 及 EPL-1.0、EDL-1.0。当前这些依赖源码均未复制进本库。
- 所需 SDK 适配以检查工具和文字清单说明，不通过删除原版权头来规避许可问题。

## zM1 与协议研究

- 来源：[a2633063/zM1](https://github.com/a2633063/zM1)，提供固件发布、讨论、接线和协议资料。
- 其作者声明包含商业用途限制；本库不重新分发 zM1 固件、原作者图片、PDF 或逆向数据库，也不把它们声明为 MIT。
- 应用是在既有模块 ABI 上重新实现；通讯帧和内核适配来自兼容性研究。`patch_captive_dhcp_kernel.py` 是针对唯一哈希基线的本地修改工具，不是原版完整固件，也不是再分发授权。
- 补丁输出及打包生成的兼容 OTA 仍包含第三方内核；**生成成功不等于可以公开再分发**。发布二进制之前必须单独确认权利。

## 其他项目与商标

Home Assistant MQTT Discovery / Update 文档用于接口兼容。README 中引用的其他 M1 项目仅用于公平对比，并不暗示合作、认可或代码来源。

斐讯、MXCHIP、MiCO、Tasmota、Home Assistant 等名称属于各自权利人。本项目为社区独立开发，不是官方固件。原创代码的许可见根目录 LICENSE，且不扩展到上述第三方材料。
