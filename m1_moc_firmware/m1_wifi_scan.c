#include "m1_wifi_scan.h"

#include <string.h>

static m1_wifi_scan_results_t g_results;
static mico_mutex_t g_results_lock;
static uint8_t g_started;
static uint32_t g_scan_started_at;
#define M1_WIFI_SCAN_TIMEOUT_MS 20000U
static mico_Context_t *g_recovery_context;
static mico_thread_t g_recovery_thread;
static volatile uint8_t g_recovery_connected;
static volatile uint8_t g_recovery_waiting_result;
static volatile uint8_t g_recovery_rescan_requested;

#define M1_WIFI_RECOVERY_INITIAL_DELAY_SECONDS 8U
#define M1_WIFI_RECOVERY_RESULT_TIMEOUT_SECONDS 20U
#define M1_WIFI_RECOVERY_RESCAN_SECONDS         5U
#define M1_WIFI_RECOVERY_THREAD_STACK           1024U

static uint8_t m1_wifi_ssid_matches(const char ap_ssid[32], const char *saved_ssid)
{
    size_t saved_length;

    if (saved_ssid == 0) {
        return 0U;
    }
    saved_length = strlen(saved_ssid);
    if ((saved_length == 0U) || (saved_length > 32U)) {
        return 0U;
    }
    if (memcmp(ap_ssid, saved_ssid, saved_length) != 0) {
        return 0U;
    }
    return (saved_length == 32U) || (ap_ssid[saved_length] == '\0');
}

/*
 * 配网时常把“ 2.4G”当成 SSID 名称的一部分。它不是协议自动转换规则：仅在
 * 精确 SSID 完全未被扫描到时，才把这个明确的频段说明当作后缀忽略一次。
 */
static uint8_t m1_wifi_ssid_matches_2g_label(const char ap_ssid[32], const char *saved_ssid)
{
    static const char label[] = " 2.4G";
    size_t saved_length;
    size_t label_length = sizeof(label) - 1U;
    size_t prefix_length;

    if (saved_ssid == 0) {
        return 0U;
    }
    saved_length = strlen(saved_ssid);
    if ((saved_length <= label_length) || (saved_length > 32U) ||
        (memcmp(saved_ssid + saved_length - label_length, label, label_length) != 0)) {
        return 0U;
    }
    prefix_length = saved_length - label_length;
    if (memcmp(ap_ssid, saved_ssid, prefix_length) != 0) {
        return 0U;
    }
    return (prefix_length == 32U) || (ap_ssid[prefix_length] == '\0');
}

static void m1_wifi_recovery_connect(const ScanResult_adv *scan_result)
{
    network_InitTypeDef_adv_st config;
    int ap_index;
    int fallback_index = -1;
    const mico_sys_config_t *saved;

    if ((g_recovery_context == 0) || (scan_result == 0) ||
        (scan_result->ApList == 0) || (scan_result->ApNum <= 0) ||
        (g_recovery_connected != 0U) || (g_recovery_waiting_result != 0U)) {
        return;
    }

    saved = &g_recovery_context->micoSystemConfig;
    for (ap_index = 0; ap_index < (int)scan_result->ApNum; ++ap_index) {
        if (m1_wifi_ssid_matches(scan_result->ApList[ap_index].ssid, saved->ssid)) {
            fallback_index = ap_index;
            break;
        }
        if ((fallback_index < 0) &&
            m1_wifi_ssid_matches_2g_label(scan_result->ApList[ap_index].ssid, saved->ssid)) {
            fallback_index = ap_index;
        }
    }

    if (fallback_index >= 0) {
        ap_index = fallback_index;

        memset(&config, 0, sizeof(config));
        memcpy(config.ap_info.ssid, scan_result->ApList[ap_index].ssid,
               sizeof(config.ap_info.ssid));
        memcpy(config.ap_info.bssid, scan_result->ApList[ap_index].bssid,
               sizeof(config.ap_info.bssid));
        config.ap_info.channel = (uint8_t)scan_result->ApList[ap_index].channel;
        config.ap_info.security = scan_result->ApList[ap_index].security;
        memcpy(config.key, saved->user_key, sizeof(config.key));
        config.key_len = saved->user_keyLength;
        config.dhcpMode = saved->dhcpEnable ? DHCP_Client : DHCP_Disable;
        memcpy(config.local_ip_addr, saved->localIp, sizeof(config.local_ip_addr));
        memcpy(config.net_mask, saved->netMask, sizeof(config.net_mask));
        memcpy(config.gateway_ip_addr, saved->gateWay, sizeof(config.gateway_ip_addr));
        memcpy(config.dnsServer_ip_addr, saved->dnsServer, sizeof(config.dnsServer_ip_addr));
        config.wifi_retry_interval = 1000;

        g_recovery_waiting_result = 1U;
        (void)micoWlanStartAdv(&config);
    }
}

static void m1_wifi_scan_completed(ScanResult_adv *scan_result, void *arg)
{
    int ap_index;
    uint32_t generation;

    (void)arg;
    if (mico_rtos_lock_mutex(&g_results_lock) != kNoErr) {
        return;
    }

    /* 完成序号必须跨扫描递增；不能随着扫描快照一起清零。 */
    generation = g_results.generation + 1U;
    memset(&g_results, 0, sizeof(g_results));
    g_results.generation = generation;

    if ((scan_result != 0) && (scan_result->ApList != 0) &&
        ((uint8_t)scan_result->ApNum > 0U)) {
        g_results.source_count = (uint8_t)scan_result->ApNum;
        for (ap_index = 0; ap_index < (int)g_results.source_count; ++ap_index) {
            m1_wifi_ap_t candidate;
            uint8_t index;
            uint8_t weakest = 0U;

            memset(&candidate, 0, sizeof(candidate));
            memcpy(candidate.ssid, scan_result->ApList[ap_index].ssid, 32U);
            if (candidate.ssid[0] == '\0') continue;
            memcpy(candidate.bssid, scan_result->ApList[ap_index].bssid, 6U);
            candidate.channel = (uint8_t)scan_result->ApList[ap_index].channel;
            candidate.security = scan_result->ApList[ap_index].security;
            candidate.rssi = scan_result->ApList[ap_index].rssi;

            /* 先合并同名 AP，再占用名额，避免 Mesh/多 AP 挤掉其它 SSID。 */
            for (index = 0U; index < g_results.count; ++index) {
                if (strcmp(g_results.ap[index].ssid, candidate.ssid) == 0) break;
                if (g_results.ap[index].rssi < g_results.ap[weakest].rssi) weakest = index;
            }
            if (index < g_results.count) {
                if (candidate.rssi > g_results.ap[index].rssi) g_results.ap[index] = candidate;
            } else if (g_results.count < M1_WIFI_SCAN_MAX_APS) {
                g_results.ap[g_results.count++] = candidate;
            } else {
                g_results.limited = 1U;
                if (candidate.rssi > g_results.ap[weakest].rssi) g_results.ap[weakest] = candidate;
            }
        }
    }

    /* 保证接口本身按信号降序；容量满时也保留最强的网络。 */
    for (ap_index = 1; ap_index < (int)g_results.count; ++ap_index) {
        m1_wifi_ap_t candidate = g_results.ap[ap_index];
        int index = ap_index;
        while ((index > 0) && (g_results.ap[index - 1].rssi < candidate.rssi)) {
            g_results.ap[index] = g_results.ap[index - 1];
            --index;
        }
        g_results.ap[index] = candidate;
    }
    (void)mico_rtos_unlock_mutex(&g_results_lock);

    m1_wifi_recovery_connect(scan_result);
}

static void m1_wifi_recovery_status_changed(WiFiEvent event, void *arg)
{
    (void)arg;
    if (event == NOTIFY_STATION_UP) {
        g_recovery_connected = 1U;
        g_recovery_waiting_result = 0U;
    } else if (event == NOTIFY_STATION_DOWN) {
        g_recovery_connected = 0U;
    }
}

static void m1_wifi_recovery_connect_failed(OSStatus err, void *arg)
{
    (void)err;
    (void)arg;
    g_recovery_waiting_result = 0U;
    g_recovery_rescan_requested = 1U;
}

static void m1_wifi_recovery_thread_main(mico_thread_arg_t arg)
{
    uint32_t waited_seconds = 0U;

    (void)arg;
    mico_thread_sleep(M1_WIFI_RECOVERY_INITIAL_DELAY_SECONDS);

    for (;;) {
        if (g_recovery_connected != 0U) {
            mico_thread_sleep(M1_WIFI_RECOVERY_RESCAN_SECONDS);
            continue;
        }

        if ((g_recovery_waiting_result == 0U) ||
            (waited_seconds >= M1_WIFI_RECOVERY_RESULT_TIMEOUT_SECONDS) ||
            (g_recovery_rescan_requested != 0U)) {
            g_recovery_rescan_requested = 0U;
            g_recovery_waiting_result = 0U;
            waited_seconds = 0U;
            (void)m1_wifi_scan_request();
        }

        mico_thread_sleep(M1_WIFI_RECOVERY_RESCAN_SECONDS);
        waited_seconds += M1_WIFI_RECOVERY_RESCAN_SECONDS;
    }
}

OSStatus m1_wifi_scan_start(void)
{
    OSStatus err;

    if (g_started != 0U) {
        return kNoErr;
    }

    err = mico_rtos_init_mutex(&g_results_lock);
    if (err != kNoErr) {
        return err;
    }

    err = mico_system_notify_register(mico_notify_WIFI_SCAN_ADV_COMPLETED,
                                      (void *)m1_wifi_scan_completed, 0);
    if (err != kNoErr) {
        (void)mico_rtos_deinit_mutex(&g_results_lock);
        return err;
    }

    err = micoWlanPowerOn();
    if (err != kNoErr) {
        (void)mico_system_notify_remove(mico_notify_WIFI_SCAN_ADV_COMPLETED,
                                        (void *)m1_wifi_scan_completed);
        (void)mico_rtos_deinit_mutex(&g_results_lock);
        return err;
    }

    g_started = 1U;
    return kNoErr;
}

OSStatus m1_wifi_scan_request(void)
{
    if (g_started == 0U) {
        return kNotInitializedErr;
    }

    if (mico_rtos_lock_mutex(&g_results_lock) != kNoErr) return kGeneralErr;
    if ((g_results.scanning != 0U) &&
        ((uint32_t)(mico_rtos_get_time() - g_scan_started_at) < M1_WIFI_SCAN_TIMEOUT_MS)) {
        (void)mico_rtos_unlock_mutex(&g_results_lock);
        return kNoErr;
    }
    g_results.scanning = 1U;
    g_scan_started_at = mico_rtos_get_time();
    (void)mico_rtos_unlock_mutex(&g_results_lock);
    micoWlanStartScanAdv();
    return kNoErr;
}

OSStatus m1_wifi_scan_get_results(m1_wifi_scan_results_t *out_results)
{
    if (out_results == 0) {
        return kParamErr;
    }
    if (g_started == 0U) {
        return kNotInitializedErr;
    }

    if (mico_rtos_lock_mutex(&g_results_lock) != kNoErr) {
        return kGeneralErr;
    }
    memcpy(out_results, &g_results, sizeof(*out_results));
    return mico_rtos_unlock_mutex(&g_results_lock);
}

OSStatus m1_wifi_recovery_start(mico_Context_t *context)
{
    OSStatus err;

    if (context == 0) {
        return kParamErr;
    }
    if (g_recovery_context != 0) {
        return kNoErr;
    }

    err = m1_wifi_scan_start();
    if (err != kNoErr) {
        return err;
    }
    g_recovery_context = context;
    err = mico_system_notify_register(mico_notify_WIFI_STATUS_CHANGED,
                                      (void *)m1_wifi_recovery_status_changed, 0);
    if (err != kNoErr) {
        g_recovery_context = 0;
        return err;
    }
    err = mico_system_notify_register(mico_notify_WIFI_CONNECT_FAILED,
                                      (void *)m1_wifi_recovery_connect_failed, 0);
    if (err != kNoErr) {
        (void)mico_system_notify_remove(mico_notify_WIFI_STATUS_CHANGED,
                                        (void *)m1_wifi_recovery_status_changed);
        g_recovery_context = 0;
        return err;
    }

    err = mico_rtos_create_thread(&g_recovery_thread, MICO_APPLICATION_PRIORITY,
                                  "m1_wifi_recover", m1_wifi_recovery_thread_main,
                                  M1_WIFI_RECOVERY_THREAD_STACK, 0);
    if (err != kNoErr) {
        (void)mico_system_notify_remove(mico_notify_WIFI_CONNECT_FAILED,
                                        (void *)m1_wifi_recovery_connect_failed);
        (void)mico_system_notify_remove(mico_notify_WIFI_STATUS_CHANGED,
                                        (void *)m1_wifi_recovery_status_changed);
        g_recovery_context = 0;
    }
    return err;
}
