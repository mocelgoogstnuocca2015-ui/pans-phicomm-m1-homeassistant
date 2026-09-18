NAME := App_m1_moc_firmware

# MK3080B 不带 BlueNRG；避免 SDK 无条件编译缺失依赖的质检示例。
DISABLE_MICO_BLUENRG := 1

$(NAME)_SOURCES := main.c \
                   m1_sensor_parser.c \
                   m1_sensor_uart.c \
                   m1_wifi_scan.c \
                   m1_setup_portal.c \
                   m1_captive_portal.c \
                   m1_settings.c \
                   m1_ha_mqtt.c \
                   m1_ota.c

$(NAME)_COMPONENTS += protocols/mqtt \
                      daemons/ota_server

# MQTT、OTA 与 MiCO 配置服务均由 SDK 组件提供；应用层仅保存版本化配置并做协议适配。
