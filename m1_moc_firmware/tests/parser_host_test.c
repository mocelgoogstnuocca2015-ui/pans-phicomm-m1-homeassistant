#include <assert.h>
#include <stdint.h>

#include "../m1_sensor_parser.h"

int main(void)
{
    static const uint8_t packet[M1_SENSOR_FRAME_SIZE] = {
        0x23, 0x01, 0x00, 0x32, 0x00, 0x1A, 0x0B, 0x3A, 0x46,
        0x00, 0x10, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x21
    };
    m1_sensor_parser_t parser;
    m1_sensor_frame_t frame;
    m1_sensor_sample_t sample;
    uint32_t i;

    m1_sensor_parser_reset(&parser);
    assert(m1_sensor_parser_feed(&parser, 0x7E, &frame) == 0);
    for (i = 0; i < M1_SENSOR_FRAME_SIZE; ++i) {
        if (i + 1U < M1_SENSOR_FRAME_SIZE) {
            assert(m1_sensor_parser_feed(&parser, packet[i], &frame) == 0);
        } else {
            assert(m1_sensor_parser_feed(&parser, packet[i], &frame) == 1);
        }
    }

    assert(m1_sensor_decode_sample(&frame, &sample) == 1);
    assert(sample.temperature_milli_c == 26100U);
    assert(sample.humidity_integer == 58U);
    assert(sample.humidity_tenths == 7U);
    assert(sample.formaldehyde_milli_mg_m3 == 50U);
    assert(sample.pm25_ug_m3 == 16U);
    return 0;
}
