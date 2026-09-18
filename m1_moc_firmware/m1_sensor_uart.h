#ifndef M1_SENSOR_UART_H
#define M1_SENSOR_UART_H

#include "m1_sensor_parser.h"
#include "mico.h"

/*
 * 从备份的原厂 zM1 用户程序确认：MicoUartInitialize(0, 115200, ...)
 * 后立即发送 23 02 64 01 00 00 00 00 00 00 00 21，并开始读取 20 字节帧。
 * 本板 MICO_UART_1 的枚举值即为 0，因此原厂和板级定义指向同一条 UART。
 */
#define M1_MCU_UART MICO_UART_1

typedef void (*m1_sensor_frame_callback_t)(const m1_sensor_frame_t *frame,
                                           const m1_sensor_sample_t *sample);

typedef struct {
    uint32_t received_bytes;
    uint32_t completed_frames;
    uint32_t decoded_samples;
    uint8_t has_last_frame;
    uint8_t last_frame_type;
    uint8_t last_frame[M1_SENSOR_FRAME_SIZE];
} m1_sensor_uart_stats_t;

/*
 * 初始化原厂确认的 MICO_UART_1、发送原厂启动帧，并启动解析线程。
 * 该函数不会写 Flash。
 */
OSStatus m1_sensor_uart_start(m1_sensor_frame_callback_t callback);

/* 按已验证的原厂 0x02 格式设置屏幕亮度；与查询帧共用同一发送锁。 */
OSStatus m1_sensor_uart_set_brightness(uint8_t brightness);

/* 不向主控 UART 发送任何字节，仅返回接收/解析计数供 MQTT 诊断。 */
void m1_sensor_uart_get_stats(m1_sensor_uart_stats_t *out_stats);

#endif
