#include "m1_sensor_uart.h"

#include <string.h>

#define M1_UART_RX_BUFFER_SIZE 128U
#define M1_UART_THREAD_STACK    1024U
#define M1_SENSOR_POLL_MS       5000U
#define M1_UART_RECV_SLICE_MS   1000U

static ring_buffer_t g_rx_ring;
static uint8_t g_rx_storage[M1_UART_RX_BUFFER_SIZE];
static m1_sensor_frame_callback_t g_callback;
static volatile uint32_t g_received_bytes;
static volatile uint32_t g_completed_frames;
static volatile uint32_t g_decoded_samples;
static volatile uint8_t g_has_last_frame;
static volatile uint8_t g_last_frame_type;
static uint8_t g_last_frame[M1_SENSOR_FRAME_SIZE];
static mico_mutex_t g_tx_lock;

static const uint8_t g_sensor_request_frame[12] = {
    0x23, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x21
};

static OSStatus m1_sensor_uart_send(const uint8_t *data, uint32_t length)
{
    OSStatus err;

    if ((data == 0) || (length == 0U)) {
        return kParamErr;
    }
    err = mico_rtos_lock_mutex(&g_tx_lock);
    if (err != kNoErr) {
        return err;
    }
    err = MicoUartSend(M1_MCU_UART, (uint8_t *)data, length);
    (void)mico_rtos_unlock_mutex(&g_tx_lock);
    return err;
}

static void m1_sensor_request_sample(void)
{
    /* 原厂 zM1 定时发送的 0x01 查询帧，主控以 20 字节 0x01 数据帧应答。 */
    (void)m1_sensor_uart_send(g_sensor_request_frame, sizeof(g_sensor_request_frame));
}

static void m1_sensor_uart_thread(mico_thread_arg_t arg)
{
    m1_sensor_parser_t parser;
    m1_sensor_frame_t frame;
    m1_sensor_sample_t sample;
    uint8_t byte;
    uint32_t idle_ms = 0U;

    (void)arg;
    m1_sensor_parser_reset(&parser);

    for (;;) {
        if (MicoUartRecv(M1_MCU_UART, &byte, 1U, M1_UART_RECV_SLICE_MS) != kNoErr) {
            idle_ms += M1_UART_RECV_SLICE_MS;
            if (idle_ms >= M1_SENSOR_POLL_MS) {
                m1_sensor_request_sample();
                idle_ms = 0U;
            }
            continue;
        }
        idle_ms = 0U;
        ++g_received_bytes;

        if (m1_sensor_parser_feed(&parser, byte, &frame) == 0) {
            continue;
        }
        ++g_completed_frames;
        memcpy(g_last_frame, frame.raw, sizeof(g_last_frame));
        g_last_frame_type = frame.type;
        g_has_last_frame = 1U;

        if (g_callback == 0) {
            continue;
        }

        if (m1_sensor_decode_sample(&frame, &sample) != 0) {
            ++g_decoded_samples;
            g_callback(&frame, &sample);
        } else {
            g_callback(&frame, 0);
        }
    }
}

OSStatus m1_sensor_uart_start(m1_sensor_frame_callback_t callback)
{
    mico_uart_config_t uart_config;
    static const uint8_t original_startup_frame[12] = {
        0x23, 0x02, 0x64, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x21
    };
    OSStatus err;

    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.baud_rate = 115200;
    uart_config.data_width = DATA_WIDTH_8BIT;
    uart_config.parity = NO_PARITY;
    uart_config.stop_bits = STOP_BITS_1;
    uart_config.flow_control = FLOW_CONTROL_DISABLED;
    uart_config.flags = UART_WAKEUP_DISABLE;

    ring_buffer_init(&g_rx_ring, g_rx_storage, M1_UART_RX_BUFFER_SIZE);
    err = MicoUartInitialize(M1_MCU_UART, &uart_config, &g_rx_ring);
    if (err != kNoErr) {
        return err;
    }

    err = mico_rtos_init_mutex(&g_tx_lock);
    if (err != kNoErr) {
        return err;
    }

    /* 与备份中的原厂 zM1 保持相同的 UART 初始化后首帧顺序。 */
    (void)m1_sensor_uart_send(original_startup_frame, sizeof(original_startup_frame));
    m1_sensor_request_sample();

    g_callback = callback;
    return mico_rtos_create_thread(0, MICO_APPLICATION_PRIORITY,
                                   "m1_sensor", m1_sensor_uart_thread,
                                   M1_UART_THREAD_STACK, 0);
}

OSStatus m1_sensor_uart_set_brightness(uint8_t brightness)
{
    uint8_t command[12] = { 0x23, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x21 };

    if (brightness > 4U) {
        brightness = 4U;
    }
    command[2] = (uint8_t)(brightness * 25U);
    command[3] = (brightness == 0U) ? 0U : 1U;
    return m1_sensor_uart_send(command, sizeof(command));
}

void m1_sensor_uart_get_stats(m1_sensor_uart_stats_t *out_stats)
{
    if (out_stats == 0) {
        return;
    }
    out_stats->received_bytes = g_received_bytes;
    out_stats->completed_frames = g_completed_frames;
    out_stats->decoded_samples = g_decoded_samples;
    out_stats->has_last_frame = g_has_last_frame;
    out_stats->last_frame_type = g_last_frame_type;
    memcpy(out_stats->last_frame, g_last_frame, sizeof(out_stats->last_frame));
}
