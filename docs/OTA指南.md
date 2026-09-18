# OTA：使用者操作与维护者准备

## 先明确当前状态

固件已实现 MQTT 触发 HTTP 下载、版本/进度上报、暂存区镜像校验和切换启动流程；提供 HA MQTT Update Discovery 配置生成器。**当前公共分支完整无线下载 → 重启 → 新版本上线 → Wi-Fi/MQTT 保留的验收仍待补齐。** 初次测试请保留本机备份和 SWD 救援条件。

不是在 HA 里装一个插件就自动收到所有 GitHub 更新。管理员必须先准备合法可用的兼容镜像、设备可访问的 HTTP 地址，并为该设备发布更新实体；不需要知道用户 Wi-Fi 密码。

## 使用者：管理员准备好之后

1. 确认 M1 在线且传感器样本在更新。
2. 在 HA 设备页打开“M1 固件更新”，核对当前与目标版本、说明及硬件适用范围。
3. 点击安装，保持供电和网络，避免重复触发、拔电或调试器复位。
4. 等待设备重启，再核对 `firmware_version` 和传感器消息。下载 100% 或 MQTT 命令送达都不能单独作为升级成功证据。

失败时先保存版本、`ota_state`、`ota_error` 等脱敏信息。应用有错误字段，不代表 HA 默认更新卡片会展示完整错误原因。

## 维护者：准备通用镜像

先阅读[开发与构建](开发与构建.md)。不要使用 SDK 自动生成的 `.ota.bin` 或 `.all.bin`。

```console
python m1_moc_firmware/tools/patch_captive_dhcp_kernel.py 原始基线.ota.bin output/captive-kernel.bin
python m1_moc_firmware/tools/pack_compatible_ota.py --kernel-prefix output/captive-kernel.bin --user-app 新应用.usr.bin --output output/m1.compat.ota.bin
```

输入基线完整 SHA-256 必须是 `20c5e6ae1692e3e047063b637b137635ab9e57711dba7c27ef0eb6cf1390887e`，脚本拒绝其他基线。这个输入来自 zM1 兼容基线，**不意味着 zM1 就是斐讯官方出厂固件**。本仓库不提供原包；获取、修改和再分发须遵守原权利人的许可。

门户前缀长 `0x75000`，MD5 为 `72adb44fcb95ca84b5729d1754c88266`。打包器校验前缀、用户应用头的长度与双 CRC，再拼接包尾 MD5。修改内核需要同时设计兼容迁移，不能只改掉校验常量让旧设备强行接受。

将镜像放在**设备可访问的可信 HTTP 服务**中。例如使用 HA 的本地静态目录时，可以将文件放在 `config/www/m1-ota/`，对应 `/local/m1-ota/`。以下 `192.0.2.10` 为文档保留地址，必须改成自己的真实地址；URL 必须直接返回镜像，不是登录页、GitHub HTML 页面或 HTTPS 跳转页。校验实际长度及下载内容。

## 维护者：发布 HA 更新实体

设备 ID 来自 `m1/<设备ID>/state` 主题，格式为 `m1_` 加 12 位小写 MAC 十六进制。示例 `m1_001122334455` 仅为占位。

```console
python m1_moc_firmware/tools/create_ha_update_discovery.py --ota output/m1.compat.ota.bin --url http://192.0.2.10:8123/local/m1-ota/m1.compat.ota.bin --version 2026.09.17.24 --device-id m1_001122334455 --release-summary "更新说明，请替换成此次真实变更" --output output/update-discovery.json
```

使用 HA 的 MQTT 发布操作或你已配置好认证的 MQTT 工具，将生成 JSON 发布到：

```text
homeassistant/update/m1_001122334455/firmware/config
```

Discovery 配置使用 `retain: true`。不要在文档、Git 仓库或终端日志中公开 Broker 密码。**实际 OTA 安装命令不要 retain**，否则重连后可能重新触发。每台设备使用自己的 ID，但兼容镜像可以相同。

HA MQTT Update 的字段含义以 [Home Assistant 官方说明](https://www.home-assistant.io/integrations/update.mqtt/) 为准。生成器设置当前版本、最新版本、安装命令和下载进度；版本上报与目标版本一致才是验收的一部分。

需要手动生成安装载荷时：

```console
python m1_moc_firmware/tools/create_ota_command.py --ota output/m1.compat.ota.bin --url http://192.0.2.10:8123/local/m1-ota/m1.compat.ota.bin
```

载荷格式为 `http://地址/文件|整个文件的32位MD5`，发布到 `m1/<设备ID>/ota/set`。不要在载荷末尾附换行。`--payload-only` 用于要求无尾部换行的管道。

## 配置保留与安全边界

- 镜像不含 Wi-Fi/MQTT 凭据，不通过用户 Wi-Fi 密码签名，不以擦除 Parameter1/Parameter2 为升级步骤。
- 底层需要写启动元数据；参数结构版本升级、迁移和降级仍需单独验收。不能声称所有未知格式都会保留。
- HA 历史数据在 HA 数据库中；应用 RAM 状态和未持久化亮度不属于保留承诺。
- MD5 / CRC 仅用于完整性检测；没有发布者数字签名或安全启动，不能抵御能控制 HTTP/MQTT 的攻击者。
- 当前不提供已验证的双镜像自动回滚、断电恢复或跨内核 OTA。下载暂存区校验成功不等于断电写入安全。

状态主题：`m1/<设备ID>/state`。`ota_state`：0 空闲、1 下载中、2 校验成功阶段、3 失败；`ota_progress` 是下载百分比，`ota_error` 是错误码。亮度与传感器使用相同状态主题。
