#ifndef M1_SENSOR_PARSER_H
#define M1_SENSOR_PARSER_H

#include <stdint.h>

#define M1_SENSOR_FRAME_SIZE 20U
#define M1_SENSOR_FRAME_HEAD 0x23U
#define M1_SENSOR_FRAME_TAIL 0x21U

typedef struct {
    uint8_t data[M1_SENSOR_FRAME_SIZE];
    uint8_t length;
} m1_sensor_parser_t;

typedef struct {
    uint8_t type;
    uint8_t raw[M1_SENSOR_FRAME_SIZE];
} m1_sensor_frame_t;

typedef struct {
    uint16_t temperature_milli_c;
    uint8_t humidity_integer;
    uint8_t humidity_tenths;
    uint16_t formaldehyde_milli_mg_m3;
    uint16_t pm25_ug_m3;
} m1_sensor_sample_t;

void m1_sensor_parser_reset(m1_sensor_parser_t *parser);

/* 返回 1 表示收到并校验完成一个完整帧，0 表示仍在收集或已丢弃无效字节。 */
int m1_sensor_parser_feed(m1_sensor_parser_t *parser, uint8_t byte,
                          m1_sensor_frame_t *out_frame);

/* 仅解码类型 0x01 传感器帧；成功返回 1，其他类型或非法帧返回 0。 */
int m1_sensor_decode_sample(const m1_sensor_frame_t *frame,
                            m1_sensor_sample_t *out_sample);

#endif
