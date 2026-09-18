#ifndef M1_OTA_H
#define M1_OTA_H

#include "mico.h"

/* MQTT OTA 命令只接受 http://URL|32 位 MD5，并在重启前完成设备端镜像校验。 */
/* 镜像与更新地址不绑定某一用户的 Wi-Fi 密钥，便于发布通用兼容包。 */
OSStatus m1_ota_start(const char *url, const char *md5_hex);
void m1_ota_get_status(uint8_t *out_state, uint8_t *out_progress, int *out_error);

#endif
