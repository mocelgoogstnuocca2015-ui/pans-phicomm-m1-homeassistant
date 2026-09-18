#include "mico.h"
#include "MQTTClient.h"

#include "m1_ha_mqtt.h"
#include "m1_firmware_version.h"
#include "m1_ota.h"
#include "m1_sensor_uart.h"
#include "m1_settings.h"

#include <stdio.h>
#include <string.h>

#define M1_HA_THREAD_STACK       3072U
#define M1_HA_RETRY_SECONDS      5U
#define M1_HA_PUBLISH_SECONDS    5U
#define M1_HA_KEEPALIVE_SECONDS  10U

static mico_mutex_t g_sample_lock;
static m1_sensor_sample_t g_latest_sample;
static uint8_t g_have_sample;
static uint8_t g_started;
static volatile uint8_t g_brightness = 4U;
static volatile uint16_t g_ota_command_received;
static volatile uint16_t g_ota_command_format_rejected;
static volatile uint16_t g_ota_command_started;

static void m1_topic_id(char *out, size_t out_size, const IPStatusTypedef *ip)
{
    size_t i;
    size_t used = 0U;

    (void)snprintf(out, out_size, "m1_");
    used = strlen(out);
    for (i = 0U; (i < strlen(ip->mac)) && (used + 1U < out_size); ++i) {
        const char c = ip->mac[i];
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f')) {
            out[used++] = (char)((c >= 'A' && c <= 'F') ? (c + ('a' - 'A')) : c);
        }
    }
    out[used] = '\0';
}

static int m1_publish(Client *client, const char *topic, const char *payload, int retained)
{
    MQTTMessage message = MQTTMessage_publishData_initializer;

    message.qos = QOS0;
    message.retained = (char)retained;
    message.payload = (void *)payload;
    message.payloadlen = strlen(payload);
    return MQTTPublish(client, topic, &message);
}

static void m1_frame_hex(char *out, size_t out_size, const m1_sensor_uart_stats_t *stats)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t i;

    if ((out == 0) || (out_size < (M1_SENSOR_FRAME_SIZE * 2U + 1U)) ||
        (stats == 0) || (stats->has_last_frame == 0U)) {
        if ((out != 0) && (out_size > 0U)) {
            out[0] = '\0';
        }
        return;
    }
    for (i = 0U; i < M1_SENSOR_FRAME_SIZE; ++i) {
        out[i * 2U] = hex[(stats->last_frame[i] >> 4) & 0x0fU];
        out[i * 2U + 1U] = hex[stats->last_frame[i] & 0x0fU];
    }
    out[M1_SENSOR_FRAME_SIZE * 2U] = '\0';
}

static void m1_send_brightness(uint8_t brightness)
{
    if (brightness > 4U) {
        brightness = 4U;
    }
    if (m1_sensor_uart_set_brightness(brightness) == kNoErr) {
        g_brightness = brightness;
    }
}

static void m1_brightness_command(MessageData *data)
{
    char input[8];
    size_t size;
    unsigned int value = 0U;

    if ((data == 0) || (data->message == 0) || (data->message->payload == 0)) {
        return;
    }
    size = data->message->payloadlen;
    if (size >= sizeof(input)) {
        return;
    }
    memcpy(input, data->message->payload, size);
    input[size] = '\0';
    if (sscanf(input, "%u", &value) == 1) {
        m1_send_brightness((uint8_t)value);
    }
}

static void m1_ota_command(MessageData *data)
{
    char input[280];
    char *md5_hex;
    size_t size;

    ++g_ota_command_received;
    if ((data == 0) || (data->message == 0) || (data->message->payload == 0)) {
        ++g_ota_command_format_rejected;
        return;
    }
    size = data->message->payloadlen;
    if (size >= sizeof(input)) {
        ++g_ota_command_format_rejected;
        return;
    }
    memcpy(input, data->message->payload, size);
    input[size] = '\0';
    md5_hex = strchr(input, '|');
    if (md5_hex == 0) {
        ++g_ota_command_format_rejected;
        return;
    }
    *md5_hex++ = '\0';
    if (strchr(md5_hex, '|') != 0) {
        ++g_ota_command_format_rejected;
        return;
    }
    ++g_ota_command_started;
    (void)m1_ota_start(input, md5_hex);
}

static int m1_publish_discovery(Client *client, const char *id, const char *state_topic,
                                const char *availability_topic, const char *brightness_command_topic)
{
    static const char *names[] = { "temperature", "humidity", "formaldehyde", "pm25" };
    static const char *labels[] = { "温度", "湿度", "甲醛", "PM2.5" };
    static const char *units[] = { "°C", "%", "mg/m³", "µg/m³" };
    char topic[128];
    /* Discovery 的 device 元数据与 availability 字段较长，必须完整发布 JSON。 */
    char payload[768];
    uint8_t i;

    for (i = 0U; i < 4U; ++i) {
        (void)snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", id, names[i]);
        if (i < 2U) {
            (void)snprintf(payload, sizeof(payload),
                           "{\"name\":\"M1 %s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.%s }}\",\"unit_of_measurement\":\"%s\",\"device_class\":\"%s\",\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"斐讯 M1\",\"model\":\"EMW3080B / MX1290\",\"manufacturer\":\"斐讯\"}}",
                           labels[i], id, names[i], state_topic, names[i], units[i],
                           (i == 0U) ? "temperature" : "humidity", availability_topic, id);
        } else {
            (void)snprintf(payload, sizeof(payload),
                           "{\"name\":\"M1 %s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.%s }}\",\"unit_of_measurement\":\"%s\",\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"斐讯 M1\",\"model\":\"EMW3080B / MX1290\",\"manufacturer\":\"斐讯\"}}",
                           labels[i], id, names[i], state_topic, names[i], units[i], availability_topic, id);
        }
        if (m1_publish(client, topic, payload, 1) != MQTT_SUCCESS) {
            return MQTT_FAILURE;
        }
    }

    (void)snprintf(topic, sizeof(topic), "homeassistant/number/%s/brightness/config", id);
    (void)snprintf(payload, sizeof(payload),
                   "{\"name\":\"M1 屏幕亮度\",\"unique_id\":\"%s_brightness\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.brightness }}\",\"command_topic\":\"%s\",\"min\":0,\"max\":4,\"step\":1,\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",\"device\":{\"identifiers\":[\"%s\"]}}",
                   id, state_topic, brightness_command_topic, availability_topic, id);
    return m1_publish(client, topic, payload, 1);
}

static int m1_copy_sample(m1_sensor_sample_t *sample)
{
    int available = 0;

    if (mico_rtos_lock_mutex(&g_sample_lock) == kNoErr) {
        if (g_have_sample != 0U) {
            memcpy(sample, &g_latest_sample, sizeof(*sample));
            available = 1;
        }
        (void)mico_rtos_unlock_mutex(&g_sample_lock);
    }
    return available;
}

static void m1_ha_mqtt_thread(mico_thread_arg_t arg)
{
    (void)arg;
    for (;;) {
        IPStatusTypedef ip;
        Network network;
        Client client = DefaultClient;
        MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
        ssl_opts ssl = { 0 };
        char id[32];
        char client_id[48];
        char state_topic[80];
        char availability_topic[80];
        char brightness_command_topic[96];
        char ota_command_topic[88];
        /* 完整状态 JSON（含 Wi-Fi 诊断与 40 字符帧）小于 400 字节。 */
        char payload[512];
        m1_sensor_sample_t sample;
        m1_sensor_uart_stats_t uart_stats;
        LinkStatusTypeDef link;
        m1_mqtt_config_t mqtt;

        memset(&ip, 0, sizeof(ip));
        if ((micoWlanGetIPStatus(&ip, Station) != kNoErr) || (ip.ip[0] == '\0') ||
            (strcmp(ip.ip, "0.0.0.0") == 0)) {
            mico_thread_sleep(M1_HA_RETRY_SECONDS);
            continue;
        }
        m1_topic_id(id, sizeof(id), &ip);
        if (strlen(id) <= 3U) {
            mico_thread_sleep(M1_HA_RETRY_SECONDS);
            continue;
        }
        if (m1_settings_get_mqtt(&mqtt) != kNoErr) {
            mico_thread_sleep(M1_HA_RETRY_SECONDS);
            continue;
        }
        (void)snprintf(client_id, sizeof(client_id), "%s_ha", id);
        (void)snprintf(state_topic, sizeof(state_topic), "m1/%s/state", id);
        (void)snprintf(availability_topic, sizeof(availability_topic), "m1/%s/availability", id);
        (void)snprintf(brightness_command_topic, sizeof(brightness_command_topic), "m1/%s/brightness/set", id);
        (void)snprintf(ota_command_topic, sizeof(ota_command_topic), "m1/%s/ota/set", id);

        if (NewNetwork(&network, mqtt.host, mqtt.port, ssl) != MQTT_SUCCESS) {
            mico_thread_sleep(M1_HA_RETRY_SECONDS);
            continue;
        }
        (void)MQTTClientInit(&client, &network, 5000U);
        options.clientID.cstring = client_id;
        if (mqtt.username[0] != '\0') options.username.cstring = mqtt.username;
        if (mqtt.password[0] != '\0') options.password.cstring = mqtt.password;
        options.keepAliveInterval = M1_HA_KEEPALIVE_SECONDS;
        options.cleansession = 1U;
        options.willFlag = 1U;
        options.will.topicName.cstring = availability_topic;
        options.will.message.cstring = "offline";
        options.will.retained = 1U;
        if (MQTTConnect(&client, &options) != MQTT_SUCCESS) {
            network.disconnect(&network);
            mico_thread_sleep(M1_HA_RETRY_SECONDS);
            continue;
        }
        if ((m1_publish(&client, availability_topic, "online", 1) != MQTT_SUCCESS) ||
            (m1_publish_discovery(&client, id, state_topic, availability_topic, brightness_command_topic) != MQTT_SUCCESS) ||
            (MQTTSubscribe(&client, ota_command_topic, QOS0, m1_ota_command) != MQTT_SUCCESS) ||
            (MQTTSubscribe(&client, brightness_command_topic, QOS0, m1_brightness_command) != MQTT_SUCCESS)) {
            (void)m1_publish(&client, availability_topic, "offline", 1);
            (void)MQTTDisconnect(&client);
            network.disconnect(&network);
            mico_thread_sleep(M1_HA_RETRY_SECONDS);
            continue;
        }

        for (;;) {
            char raw_frame[2U * M1_SENSOR_FRAME_SIZE + 1U];
            uint8_t ota_state;
            uint8_t ota_progress;
            int ota_error;

            memset(&link, 0, sizeof(link));
            (void)micoWlanGetLinkStatus(&link);

            m1_sensor_uart_get_stats(&uart_stats);
            m1_ota_get_status(&ota_state, &ota_progress, &ota_error);
            m1_frame_hex(raw_frame, sizeof(raw_frame), &uart_stats);
            if (m1_copy_sample(&sample) != 0) {
                (void)snprintf(payload, sizeof(payload),
                               "{\"firmware_version\":\"%s\",\"temperature\":%u.%01u,\"humidity\":%u.%01u,\"formaldehyde\":%u.%03u,\"pm25\":%u,\"brightness\":%u,\"wifi_ip\":\"%s\",\"wifi_rssi\":%d,\"ota_state\":%u,\"ota_progress\":%u,\"ota_error\":%d,\"ota_commands\":%u,\"ota_format_rejected\":%u,\"ota_started\":%u,\"uart_rx_bytes\":%lu,\"uart_frames\":%lu,\"uart_samples\":%lu,\"uart_type\":%u,\"uart_frame\":\"%s\"}",
                               M1_FIRMWARE_VERSION,
                               (unsigned int)(sample.temperature_milli_c / 1000U),
                               (unsigned int)((sample.temperature_milli_c % 1000U) / 100U),
                               (unsigned int)sample.humidity_integer,
                               (unsigned int)sample.humidity_tenths,
                               (unsigned int)(sample.formaldehyde_milli_mg_m3 / 1000U),
                               (unsigned int)(sample.formaldehyde_milli_mg_m3 % 1000U),
                               (unsigned int)sample.pm25_ug_m3,
                               (unsigned int)g_brightness,
                               ip.ip, link.rssi, (unsigned int)ota_state, (unsigned int)ota_progress, ota_error,
                               (unsigned int)g_ota_command_received,
                               (unsigned int)g_ota_command_format_rejected,
                               (unsigned int)g_ota_command_started,
                               (unsigned long)uart_stats.received_bytes,
                               (unsigned long)uart_stats.completed_frames,
                               (unsigned long)uart_stats.decoded_samples,
                               (unsigned int)uart_stats.last_frame_type, raw_frame);
            } else {
                (void)snprintf(payload, sizeof(payload),
                               "{\"firmware_version\":\"%s\",\"brightness\":%u,\"wifi_ip\":\"%s\",\"wifi_rssi\":%d,\"ota_state\":%u,\"ota_progress\":%u,\"ota_error\":%d,\"ota_commands\":%u,\"ota_format_rejected\":%u,\"ota_started\":%u,\"uart_rx_bytes\":%lu,\"uart_frames\":%lu,\"uart_samples\":%lu,\"uart_type\":%u,\"uart_frame\":\"%s\"}",
                               M1_FIRMWARE_VERSION,
                               (unsigned int)g_brightness,
                               ip.ip, link.rssi, (unsigned int)ota_state, (unsigned int)ota_progress, ota_error,
                               (unsigned int)g_ota_command_received,
                               (unsigned int)g_ota_command_format_rejected,
                               (unsigned int)g_ota_command_started,
                               (unsigned long)uart_stats.received_bytes,
                               (unsigned long)uart_stats.completed_frames,
                               (unsigned long)uart_stats.decoded_samples,
                               (unsigned int)uart_stats.last_frame_type, raw_frame);
            }
            if (m1_publish(&client, state_topic, payload, 0) != MQTT_SUCCESS) {
                break;
            }
            /*
             * MiCO MQTT 的非阻塞 recv 会在“这段时间没有下行报文”时错误地
             * 返回 MQTT_FAILURE；这不是 TCP/MQTT 连接失败。上一次成功的状态
             * 发布已是可靠的链路探测，下一次发布失败时才断开并重连。
             * 仍执行 Yield，以便在发布间隔内处理亮度和 OTA 订阅命令。
             */
            (void)MQTTYield(&client, M1_HA_PUBLISH_SECONDS * 1000U);
        }
        (void)m1_publish(&client, availability_topic, "offline", 1);
        (void)MQTTDisconnect(&client);
        network.disconnect(&network);
        mico_thread_sleep(M1_HA_RETRY_SECONDS);
    }
}

OSStatus m1_ha_mqtt_start(void)
{
    OSStatus err;

    if (g_started != 0U) {
        return kNoErr;
    }
    err = mico_rtos_init_mutex(&g_sample_lock);
    if (err != kNoErr) {
        return err;
    }
    err = mico_rtos_create_thread(0, MICO_APPLICATION_PRIORITY, "m1_ha_mqtt",
                                  m1_ha_mqtt_thread, M1_HA_THREAD_STACK, 0);
    if (err != kNoErr) {
        (void)mico_rtos_deinit_mutex(&g_sample_lock);
        return err;
    }
    g_started = 1U;
    return kNoErr;
}

void m1_ha_mqtt_submit_sample(const m1_sensor_sample_t *sample)
{
    if ((sample == 0) || (g_started == 0U)) {
        return;
    }
    if (mico_rtos_lock_mutex(&g_sample_lock) == kNoErr) {
        memcpy(&g_latest_sample, sample, sizeof(g_latest_sample));
        g_have_sample = 1U;
        (void)mico_rtos_unlock_mutex(&g_sample_lock);
    }
}
