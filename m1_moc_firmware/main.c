#include "mico.h"
#include "m1_captive_portal.h"
#include "m1_ha_mqtt.h"
#include "m1_sensor_uart.h"
#include "m1_settings.h"
#include "m1_wifi_scan.h"

/*
 * UART 回调仅提交最新读数给 MQTT 线程；绝不向 MICO_UART_1 写调试文本。
 */
static void m1_sensor_frame_received(const m1_sensor_frame_t *frame,
                                     const m1_sensor_sample_t *sample)
{
    (void)frame;
    if (sample != 0) {
        m1_ha_mqtt_submit_sample(sample);
    }
}

int main(void)
{
    mico_Context_t *context;
    uint8_t has_runtime_mqtt;

    context = mico_system_context_init(M1_SETTINGS_STORAGE_SIZE);
    if (context == 0) {
        return -1;
    }
    if (m1_settings_init(context) != kNoErr) {
        return -1;
    }
    /*
     * 原厂参数区可能已有某个家庭 Wi-Fi，但首刷公共固件还没有用户自己的
     * MQTT 目的地。此时只在 RAM 中进入 SoftAP；不擦除旧参数，网页保存
     * 成功时再一次性替换。
    */
    has_runtime_mqtt = m1_settings_mqtt_is_configured();
    if (has_runtime_mqtt == 0U) {
        context->micoSystemConfig.configured = unConfigured;
    }
    if (mico_system_init(context) != kNoErr) {
        return -1;
    }
    /*
     * 首配门户也需要扫描服务来响应 /m1-wifi-scan；它只注册扫描回调，
     * 不创建会周期性切换 WLAN 模式的恢复线程。
     */
    (void)m1_wifi_scan_start();

    /*
     * SoftAP 首次配网时不能启动主动恢复线程：该线程的周期扫描会改变
     * WLAN 工作模式，可能使用户刚看到的 M1-Setup 热点消失。只有已有
     * MQTT 设置、即设备已完成首配时，才启用 BSSID/信道恢复连接逻辑。
     */
    if (has_runtime_mqtt != 0U) {
        (void)m1_wifi_recovery_start(context);
    }
    (void)m1_sensor_uart_start(m1_sensor_frame_received);
    (void)m1_ha_mqtt_start();
    return 0;
}

/* SDK 在 SoftAP 启动前、网络栈已就绪时调用此应用钩子。 */
void mico_system_delegate_config_will_start(void)
{
    (void)m1_captive_portal_start();
}
