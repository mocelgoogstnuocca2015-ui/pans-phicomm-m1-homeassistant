#ifndef M1_WIFI_SCAN_H
#define M1_WIFI_SCAN_H

#include "mico.h"

#define M1_WIFI_SCAN_MAX_APS 64U

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    wlan_sec_type_t security;
    int16_t rssi;
} m1_wifi_ap_t;

typedef struct {
    uint8_t count;
    uint8_t scanning;
    uint8_t limited;
    uint16_t source_count;
    uint32_t generation;
    m1_wifi_ap_t ap[M1_WIFI_SCAN_MAX_APS];
} m1_wifi_scan_results_t;

/*
 * 启动无线并注册高级扫描通知。该函数不连接 AP、不保存凭据。
 * 调用成功后，使用 m1_wifi_scan_request() 发起一次异步 2.4 GHz 扫描。
 */
OSStatus m1_wifi_scan_start(void);

/* 触发一次异步扫描；完成结果由 m1_wifi_scan_get_results() 读取。 */
OSStatus m1_wifi_scan_request(void);

/* 复制最近一次已完成的扫描快照；返回 kNoErr 不代表一定发现 AP。 */
OSStatus m1_wifi_scan_get_results(m1_wifi_scan_results_t *out_results);

/*
 * 连接恢复：保留 MiCO 自带的首次连接；若它未能关联，则扫描已保存的 SSID，
 * 用扫描结果中的 BSSID、信道与安全类型进行定向重试。凭据仅从参数区读取，
 * 不会编入固件或由本模块写回 Flash。
 */
OSStatus m1_wifi_recovery_start(mico_Context_t *context);

#endif
