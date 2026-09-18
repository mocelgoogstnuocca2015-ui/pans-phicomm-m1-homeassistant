#include "m1_ota.h"

#include "mico.h"
#include "CheckSumUtils.h"
#include "ota_server.h"

#include <string.h>

#define M1_OTA_USER_APP_OFFSET 0x75000UL
#define M1_OTA_MD5_SIZE        16U
#define M1_OTA_BUFFER_SIZE     1024U

#define M1_OTA_STATUS_IDLE       0U
#define M1_OTA_STATUS_DOWNLOADING 1U
#define M1_OTA_STATUS_SUCCESS    2U
#define M1_OTA_STATUS_FAILED     3U

/* MQTT 状态中的 OTA 参数拒绝原因，便于区分命令格式问题，均不会启动下载。 */
#define M1_OTA_ERR_URL           (kParamErr - 1)
#define M1_OTA_ERR_URL_LENGTH    (kParamErr - 2)
#define M1_OTA_ERR_MD5           (kParamErr - 3)
#define M1_OTA_ERR_BUSY          (kParamErr - 4)

/*
 * 日常 OTA 必须携带已验证的门户内核前缀。这样升级流程重新写入
 * Application 分区时不会把 DHCP Option 6 补丁降级成原厂内核。
 * 该值对应 patch_captive_dhcp_kernel.py 的唯一受控输出。
 */
static const uint8_t g_captive_prefix_md5[M1_OTA_MD5_SIZE] = {
    0x72, 0xad, 0xb4, 0x4f, 0xcb, 0x95, 0xca, 0x84,
    0xb5, 0x72, 0x9d, 0x17, 0x54, 0xc8, 0x82, 0x66
};

static volatile uint8_t g_ota_state = M1_OTA_STATUS_IDLE;
static volatile uint8_t g_ota_progress;
static volatile int g_ota_error;

/*
 * ota_server 的 HTTP 模式线程栈只有 2 KiB。完整性校验在该线程中运行，
 * 不能把多个 1 KiB 临时数组叠在调用栈上，否则下载完成后可能在校验阶段
 * 覆盖线程栈，表现为进度停在 100%。校验工作严格串行，共用该缓冲区即可。
 */
static uint8_t g_ota_validation_buffer[M1_OTA_BUFFER_SIZE];

static uint8_t m1_ota_is_hex32(const char *value)
{
    uint8_t index;

    if ((value == 0) || (strlen(value) != 32U)) return 0U;
    for (index = 0U; index < 32U; ++index) {
        char c = value[index];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return 0U;
    }
    return 1U;
}

static uint8_t m1_ota_url_allowed(const char *url)
{
    const char *cursor;
    uint8_t has_host = 0U;
    uint8_t has_path = 0U;

    if ((url == 0) || (strncmp(url, "http://", 7U) != 0)) return 0U;
    for (cursor = url + 7U; *cursor != '\0'; ++cursor) {
        if (((unsigned char)*cursor <= 0x20U) || (*cursor == '|')) return 0U;
        if (*cursor == '/') {
            has_path = 1U;
        } else if (has_path == 0U) {
            has_host = 1U;
        }
    }
    return (has_host != 0U) && (has_path != 0U);
}

static OSStatus m1_ota_md5_partition(mico_partition_t partition, uint32_t offset,
                                       uint32_t length, uint8_t out_md5[M1_OTA_MD5_SIZE])
{
    uint32_t remaining = length;
    md5_context context;

    InitMd5(&context);
    while (remaining > 0U) {
        uint32_t chunk = (remaining > sizeof(g_ota_validation_buffer)) ?
                         sizeof(g_ota_validation_buffer) : remaining;
        if (MicoFlashRead(partition, &offset, g_ota_validation_buffer, chunk) != kNoErr) return kReadErr;
        Md5Update(&context, g_ota_validation_buffer, (int)chunk);
        remaining -= chunk;
    }
    Md5Final(&context, out_md5);
    return kNoErr;
}

static void m1_ota_progress(OTA_STATE_E state, float progress)
{
    if (progress < 0.0F) progress = 0.0F;
    if (progress > 100.0F) progress = 100.0F;
    if (state == OTA_SUCCE) {
        g_ota_state = M1_OTA_STATUS_SUCCESS;
    } else if (state == OTA_FAIL) {
        g_ota_state = M1_OTA_STATUS_FAILED;
        g_ota_error = kGeneralErr;
    } else {
        g_ota_state = M1_OTA_STATUS_DOWNLOADING;
    }
    g_ota_progress = (uint8_t)progress;
}

OSStatus ota_server_validate_staged_image(uint32_t image_length)
{
    mico_logic_partition_t *partition;
    uint8_t header[8];
    uint8_t expected_md5[M1_OTA_MD5_SIZE];
    uint8_t actual_md5[M1_OTA_MD5_SIZE];
    uint32_t offset;
    uint32_t payload_length;
    uint32_t remaining;
    uint16_t crc_a;
    uint16_t crc_b;
    uint16_t actual_crc;
    CRC16_Context crc_context;

    partition = MicoFlashGetInfo(MICO_PARTITION_OTA_TEMP);
    if ((partition == 0) || (partition->partition_owner == MICO_FLASH_NONE) ||
        (image_length <= M1_OTA_USER_APP_OFFSET + 8U + M1_OTA_MD5_SIZE) ||
        (image_length > partition->partition_length)) return kSizeErr;
    if ((m1_ota_md5_partition(MICO_PARTITION_OTA_TEMP, 0U, M1_OTA_USER_APP_OFFSET,
                              actual_md5) != kNoErr) ||
        (memcmp(actual_md5, g_captive_prefix_md5, sizeof(actual_md5)) != 0)) return kChecksumErr;

    offset = M1_OTA_USER_APP_OFFSET;
    if (MicoFlashRead(MICO_PARTITION_OTA_TEMP, &offset, header, sizeof(header)) != kNoErr) return kReadErr;
    payload_length = (uint32_t)header[0] | ((uint32_t)header[1] << 8U) |
                     ((uint32_t)header[2] << 16U) | ((uint32_t)header[3] << 24U);
    crc_a = (uint16_t)header[4] | ((uint16_t)header[5] << 8U);
    crc_b = (uint16_t)header[6] | ((uint16_t)header[7] << 8U);
    if ((crc_a != crc_b) ||
        (image_length != M1_OTA_USER_APP_OFFSET + 8U + payload_length + M1_OTA_MD5_SIZE)) return kChecksumErr;

    CRC16_Init(&crc_context);
    remaining = payload_length;
    while (remaining > 0U) {
        uint32_t chunk = (remaining > sizeof(g_ota_validation_buffer)) ?
                         sizeof(g_ota_validation_buffer) : remaining;
        if (MicoFlashRead(MICO_PARTITION_OTA_TEMP, &offset, g_ota_validation_buffer, chunk) != kNoErr) return kReadErr;
        CRC16_Update(&crc_context, g_ota_validation_buffer, chunk);
        remaining -= chunk;
    }
    CRC16_Final(&crc_context, &actual_crc);
    if (actual_crc != crc_a) return kChecksumErr;

    offset = image_length - M1_OTA_MD5_SIZE;
    if ((MicoFlashRead(MICO_PARTITION_OTA_TEMP, &offset, expected_md5, sizeof(expected_md5)) != kNoErr) ||
        (m1_ota_md5_partition(MICO_PARTITION_OTA_TEMP, 0U,
                              image_length - M1_OTA_MD5_SIZE, actual_md5) != kNoErr) ||
        (memcmp(expected_md5, actual_md5, sizeof(actual_md5)) != 0)) return kChecksumErr;
    return kNoErr;
}

OSStatus m1_ota_start(const char *url, const char *md5_hex)
{
    OSStatus err;

    if (m1_ota_url_allowed(url) == 0U) {
        g_ota_state = M1_OTA_STATUS_FAILED;
        g_ota_progress = 0U;
        g_ota_error = M1_OTA_ERR_URL;
        return M1_OTA_ERR_URL;
    }
    if (strlen(url) >= 192U) {
        g_ota_state = M1_OTA_STATUS_FAILED;
        g_ota_progress = 0U;
        g_ota_error = M1_OTA_ERR_URL_LENGTH;
        return M1_OTA_ERR_URL_LENGTH;
    }
    if (m1_ota_is_hex32(md5_hex) == 0U) {
        g_ota_state = M1_OTA_STATUS_FAILED;
        g_ota_progress = 0U;
        g_ota_error = M1_OTA_ERR_MD5;
        return M1_OTA_ERR_MD5;
    }
    if (g_ota_state == M1_OTA_STATUS_DOWNLOADING) {
        g_ota_state = M1_OTA_STATUS_FAILED;
        g_ota_progress = 0U;
        g_ota_error = M1_OTA_ERR_BUSY;
        return M1_OTA_ERR_BUSY;
    }
    g_ota_state = M1_OTA_STATUS_DOWNLOADING;
    g_ota_progress = 0U;
    g_ota_error = kNoErr;
    err = ota_server_start((char *)url, (char *)md5_hex, m1_ota_progress);
    if (err != kNoErr) g_ota_state = M1_OTA_STATUS_FAILED;
    if (err != kNoErr) g_ota_error = err;
    return err;
}

void m1_ota_get_status(uint8_t *out_state, uint8_t *out_progress, int *out_error)
{
    if (out_state != 0) *out_state = g_ota_state;
    if (out_progress != 0) *out_progress = g_ota_progress;
    if (out_error != 0) *out_error = g_ota_error;
}
