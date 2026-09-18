#include "m1_settings.h"

#include <string.h>

#define M1_SETTINGS_MAGIC   0x4d315346UL /* M1SF */
#define M1_SETTINGS_VERSION 1U
#define M1_SETTINGS_VALID   0x01U

#pragma pack(1)
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t flags;
    uint16_t mqtt_port;
    char mqtt_host[M1_MQTT_HOST_MAX];
    char mqtt_username[M1_MQTT_USERNAME_MAX];
    char mqtt_password[M1_MQTT_PASSWORD_MAX];
    uint16_t crc16;
} m1_settings_storage_t;
#pragma pack()

typedef char m1_settings_size_check[
    (sizeof(m1_settings_storage_t) == M1_SETTINGS_STORAGE_SIZE) ? 1 : -1];

static m1_settings_storage_t *g_storage;
static mico_mutex_t g_settings_lock;
static uint8_t g_settings_ready;

static uint16_t m1_settings_crc16(const uint8_t *data, uint32_t length)
{
    uint16_t crc = 0U;
    uint32_t index;
    uint8_t bit;

    for (index = 0U; index < length; ++index) {
        crc ^= (uint16_t)data[index] << 8U;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) ? (uint16_t)((crc << 1U) ^ 0x1021U) :
                                    (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

static uint8_t m1_settings_text_valid(const char *value, uint8_t allow_empty,
                                      uint8_t host_only, uint32_t capacity)
{
    uint32_t index = 0U;

    if (value == 0) return 0U;
    while (index < capacity) {
        const unsigned char c = (unsigned char)value[index];
        if (c == '\0') return (allow_empty != 0U) || (index > 0U);
        if (index + 1U >= capacity) return 0U;
        if ((c < 0x20U) || (c > 0x7eU)) return 0U;
        if ((host_only != 0U) &&
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-')) return 0U;
        ++index;
    }
    return 0U;
}

static uint8_t m1_settings_credentials_valid(const m1_settings_storage_t *storage)
{
    return !((storage->mqtt_password[0] != '\0') && (storage->mqtt_username[0] == '\0'));
}

static void m1_settings_copy(char *destination, uint32_t capacity, const char *source)
{
    uint32_t length = (uint32_t)strlen(source);
    if (length >= capacity) length = capacity - 1U;
    memset(destination, 0, capacity);
    memcpy(destination, source, length);
}

static uint8_t m1_settings_storage_valid(const m1_settings_storage_t *storage)
{
    if ((storage == 0) || (storage->magic != M1_SETTINGS_MAGIC) ||
        (storage->version != M1_SETTINGS_VERSION) ||
        ((storage->flags & M1_SETTINGS_VALID) == 0U) ||
        (storage->crc16 != m1_settings_crc16((const uint8_t *)storage,
                                             (uint32_t)(sizeof(*storage) - sizeof(storage->crc16))))) {
        return 0U;
    }
    if ((storage->mqtt_port == 0U) ||
        (m1_settings_text_valid(storage->mqtt_host, 0U, 1U, sizeof(storage->mqtt_host)) == 0U) ||
        (m1_settings_text_valid(storage->mqtt_username, 1U, 0U, sizeof(storage->mqtt_username)) == 0U) ||
        (m1_settings_text_valid(storage->mqtt_password, 1U, 0U, sizeof(storage->mqtt_password)) == 0U) ||
        (m1_settings_credentials_valid(storage) == 0U)) {
        return 0U;
    }
    return 1U;
}

static void m1_settings_reseal(void)
{
    if ((g_storage == 0) || (g_storage->mqtt_port == 0U) ||
        (m1_settings_text_valid(g_storage->mqtt_host, 0U, 1U, sizeof(g_storage->mqtt_host)) == 0U) ||
        (m1_settings_text_valid(g_storage->mqtt_username, 1U, 0U, sizeof(g_storage->mqtt_username)) == 0U) ||
        (m1_settings_text_valid(g_storage->mqtt_password, 1U, 0U, sizeof(g_storage->mqtt_password)) == 0U) ||
        (m1_settings_credentials_valid(g_storage) == 0U)) {
        if (g_storage != 0) g_storage->flags &= (uint8_t)~M1_SETTINGS_VALID;
    } else {
        g_storage->flags |= M1_SETTINGS_VALID;
    }
    if (g_storage != 0) {
        g_storage->crc16 = m1_settings_crc16((const uint8_t *)g_storage,
                                              (uint32_t)(sizeof(*g_storage) - sizeof(g_storage->crc16)));
    }
}

OSStatus m1_settings_init(mico_Context_t *context)
{
    OSStatus err;

    if ((context == 0) || (g_settings_ready != 0U)) return kParamErr;
    g_storage = (m1_settings_storage_t *)mico_system_context_get_user_data(context);
    if (g_storage == 0) return kNoMemoryErr;
    err = mico_rtos_init_mutex(&g_settings_lock);
    if (err != kNoErr) return err;
    if (m1_settings_storage_valid(g_storage) == 0U) {
        memset(g_storage, 0, sizeof(*g_storage));
        g_storage->magic = M1_SETTINGS_MAGIC;
        g_storage->version = M1_SETTINGS_VERSION;
        g_storage->mqtt_port = 1883U;
        m1_settings_reseal();
    }
    g_settings_ready = 1U;
    return kNoErr;
}

OSStatus m1_settings_get_mqtt(m1_mqtt_config_t *out)
{
    if ((out == 0) || (g_settings_ready == 0U)) return kNotPreparedErr;
    if (mico_rtos_lock_mutex(&g_settings_lock) != kNoErr) return kGeneralErr;
    if (m1_settings_storage_valid(g_storage) == 0U) {
        (void)mico_rtos_unlock_mutex(&g_settings_lock);
        return kNotFoundErr;
    }
    memset(out, 0, sizeof(*out));
    m1_settings_copy(out->host, sizeof(out->host), g_storage->mqtt_host);
    m1_settings_copy(out->username, sizeof(out->username), g_storage->mqtt_username);
    m1_settings_copy(out->password, sizeof(out->password), g_storage->mqtt_password);
    out->port = g_storage->mqtt_port;
    (void)mico_rtos_unlock_mutex(&g_settings_lock);
    return kNoErr;
}

uint8_t m1_settings_mqtt_is_configured(void)
{
    uint8_t configured = 0U;
    if ((g_settings_ready != 0U) && (mico_rtos_lock_mutex(&g_settings_lock) == kNoErr)) {
        configured = m1_settings_storage_valid(g_storage);
        (void)mico_rtos_unlock_mutex(&g_settings_lock);
    }
    return configured;
}

OSStatus m1_settings_set_mqtt_host(const char *host)
{
    if ((g_settings_ready == 0U) || (m1_settings_text_valid(host, 0U, 1U, M1_MQTT_HOST_MAX) == 0U)) return kParamErr;
    if (mico_rtos_lock_mutex(&g_settings_lock) != kNoErr) return kGeneralErr;
    m1_settings_copy(g_storage->mqtt_host, sizeof(g_storage->mqtt_host), host);
    m1_settings_reseal();
    (void)mico_rtos_unlock_mutex(&g_settings_lock);
    return kNoErr;
}

OSStatus m1_settings_set_mqtt_port(uint16_t port)
{
    if ((g_settings_ready == 0U) || (port == 0U)) return kParamErr;
    if (mico_rtos_lock_mutex(&g_settings_lock) != kNoErr) return kGeneralErr;
    g_storage->mqtt_port = port;
    m1_settings_reseal();
    (void)mico_rtos_unlock_mutex(&g_settings_lock);
    return kNoErr;
}

OSStatus m1_settings_set_mqtt_username(const char *username)
{
    if ((g_settings_ready == 0U) ||
        (m1_settings_text_valid(username, 1U, 0U, M1_MQTT_USERNAME_MAX) == 0U)) return kParamErr;
    if (mico_rtos_lock_mutex(&g_settings_lock) != kNoErr) return kGeneralErr;
    m1_settings_copy(g_storage->mqtt_username, sizeof(g_storage->mqtt_username), username);
    m1_settings_reseal();
    (void)mico_rtos_unlock_mutex(&g_settings_lock);
    return kNoErr;
}

OSStatus m1_settings_set_mqtt_password(const char *password)
{
    if ((g_settings_ready == 0U) ||
        (m1_settings_text_valid(password, 1U, 0U, M1_MQTT_PASSWORD_MAX) == 0U)) return kParamErr;
    if (mico_rtos_lock_mutex(&g_settings_lock) != kNoErr) return kGeneralErr;
    m1_settings_copy(g_storage->mqtt_password, sizeof(g_storage->mqtt_password), password);
    m1_settings_reseal();
    (void)mico_rtos_unlock_mutex(&g_settings_lock);
    return kNoErr;
}

void config_server_delegate_report(json_object *config_cell_list, mico_Context_t *in_context)
{
    m1_mqtt_config_t mqtt;
    uint8_t configured = m1_settings_mqtt_is_configured();
    char *status = (configured != 0U) ? "已配置" : "未配置";

    UNUSED_PARAMETER(in_context);
    memset(&mqtt, 0, sizeof(mqtt));
    if (m1_settings_get_mqtt(&mqtt) != kNoErr) mqtt.port = 1883U;
    (void)config_server_create_string_cell(config_cell_list, "M1 MQTT 状态", status, "RO", NULL);
    (void)config_server_create_string_cell(config_cell_list, "M1 MQTT 主机", mqtt.host, "RW", NULL);
    (void)config_server_create_number_cell(config_cell_list, "M1 MQTT 端口", mqtt.port, "RW", NULL);
    (void)config_server_create_string_cell(config_cell_list, "M1 MQTT 用户名", mqtt.username, "RW", NULL);
    /* 密码绝不回传给配置页面；留空代表不修改。 */
    (void)config_server_create_string_cell(config_cell_list, "M1 MQTT 密码（留空不修改）", "", "RW", NULL);
}

void config_server_delegate_recv(const char *key, json_object *value, bool *need_reboot,
                                 mico_Context_t *in_context)
{
    const char *text;
    OSStatus err = kParamErr;

    UNUSED_PARAMETER(in_context);
    if ((key == 0) || (value == 0) || (need_reboot == 0)) return;
    if (!strcmp(key, "M1 MQTT 主机")) {
        text = json_object_get_string(value);
        err = m1_settings_set_mqtt_host(text);
    } else if (!strcmp(key, "M1 MQTT 端口")) {
        const int port = json_object_get_int(value);
        if ((port > 0) && (port <= 65535)) err = m1_settings_set_mqtt_port((uint16_t)port);
    } else if (!strcmp(key, "M1 MQTT 用户名")) {
        text = json_object_get_string(value);
        err = m1_settings_set_mqtt_username(text);
    } else if (!strcmp(key, "M1 MQTT 密码（留空不修改）")) {
        text = json_object_get_string(value);
        if ((text != 0) && (text[0] != '\0')) err = m1_settings_set_mqtt_password(text);
    }
    if (err == kNoErr) *need_reboot = true;
}
