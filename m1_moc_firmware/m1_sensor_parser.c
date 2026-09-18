#include "m1_sensor_parser.h"

#include <string.h>

void m1_sensor_parser_reset(m1_sensor_parser_t *parser)
{
    if (parser != 0) {
        parser->length = 0;
    }
}

int m1_sensor_parser_feed(m1_sensor_parser_t *parser, uint8_t byte,
                          m1_sensor_frame_t *out_frame)
{
    if ((parser == 0) || (out_frame == 0)) {
        return 0;
    }

    if (parser->length == 0U) {
        if (byte != M1_SENSOR_FRAME_HEAD) {
            return 0;
        }
        parser->data[0] = byte;
        parser->length = 1U;
        return 0;
    }

    parser->data[parser->length++] = byte;
    if (parser->length < M1_SENSOR_FRAME_SIZE) {
        return 0;
    }

    if (parser->data[M1_SENSOR_FRAME_SIZE - 1U] == M1_SENSOR_FRAME_TAIL) {
        memcpy(out_frame->raw, parser->data, M1_SENSOR_FRAME_SIZE);
        out_frame->type = parser->data[1];
        m1_sensor_parser_reset(parser);
        return 1;
    }

    /* 与旧固件一致：错误长度的候选帧不发布，继续寻找下一个帧头。 */
    m1_sensor_parser_reset(parser);
    if (byte == M1_SENSOR_FRAME_HEAD) {
        parser->data[0] = byte;
        parser->length = 1U;
    }
    return 0;
}

int m1_sensor_decode_sample(const m1_sensor_frame_t *frame,
                            m1_sensor_sample_t *out_sample)
{
    const uint8_t *data;

    if ((frame == 0) || (out_sample == 0) || (frame->type != 0x01U)) {
        return 0;
    }

    data = frame->raw;
    if ((data[0] != M1_SENSOR_FRAME_HEAD) ||
        (data[1] != 0x01U) ||
        (data[M1_SENSOR_FRAME_SIZE - 1U] != M1_SENSOR_FRAME_TAIL)) {
        return 0;
    }

    /*
     * 已由原厂 zM1 的 0x01 分支与实际主控回包交叉确认：
     * [2..3] 甲醛（mg/m³ 的千分之一），[5..6] 温度，
     * [7..8] 湿度；后两项的小数位均为第二字节除以 10。
     */
    out_sample->formaldehyde_milli_mg_m3 =
        ((uint16_t)data[2] << 8) | (uint16_t)data[3];
    out_sample->temperature_milli_c =
        (uint16_t)(((uint16_t)data[5] * 1000U) +
                   ((uint16_t)(data[6] / 10U) * 100U));
    out_sample->humidity_integer = data[7];
    out_sample->humidity_tenths = (uint8_t)(data[8] / 10U);
    out_sample->pm25_ug_m3 = ((uint16_t)data[9] << 8) | (uint16_t)data[10];
    return 1;
}
