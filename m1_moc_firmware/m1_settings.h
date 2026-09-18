#ifndef M1_SETTINGS_H
#define M1_SETTINGS_H

#include "mico.h"

#define M1_MQTT_HOST_MAX       64U
#define M1_MQTT_USERNAME_MAX   48U
#define M1_MQTT_PASSWORD_MAX   64U

typedef struct {
    char host[M1_MQTT_HOST_MAX];
    uint16_t port;
    char username[M1_MQTT_USERNAME_MAX];
    char password[M1_MQTT_PASSWORD_MAX];
} m1_mqtt_config_t;

/* 放入 MiCO Parameter1/2 的应用配置大小；升级应用程序时不会被覆盖。 */
#define M1_SETTINGS_STORAGE_SIZE 186U

/* 在 mico_system_context_init() 后、mico_system_init() 前调用。 */
OSStatus m1_settings_init(mico_Context_t *context);

/* 仅在 MQTT 主机和端口均有效时返回 kNoErr。调用方获得副本，不持有内部指针。 */
OSStatus m1_settings_get_mqtt(m1_mqtt_config_t *out);
uint8_t m1_settings_mqtt_is_configured(void);

/* 配置服务调用。设置成功后由 MiCO 配置服务统一持久化。 */
OSStatus m1_settings_set_mqtt_host(const char *host);
OSStatus m1_settings_set_mqtt_port(uint16_t port);
OSStatus m1_settings_set_mqtt_username(const char *username);
OSStatus m1_settings_set_mqtt_password(const char *password);

#endif
