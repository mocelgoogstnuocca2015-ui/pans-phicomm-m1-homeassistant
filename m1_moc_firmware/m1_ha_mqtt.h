#ifndef M1_HA_MQTT_H
#define M1_HA_MQTT_H

#include "m1_sensor_parser.h"

/* 启动 HA MQTT 后台线程。MQTT 连接失败会按固定间隔重试。 */
OSStatus m1_ha_mqtt_start(void);

/* 从 UART 回调提交最新的传感器值；实际网络发送在 MQTT 线程中完成。 */
void m1_ha_mqtt_submit_sample(const m1_sensor_sample_t *sample);

#endif
