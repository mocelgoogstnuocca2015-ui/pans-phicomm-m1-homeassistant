#include "mico.h"
#include "SocketUtils.h"
#include "m1_captive_portal.h"

#include <string.h>

#define M1_DNS_PORT       53U
#define M1_DNS_PACKET_MAX 512U

static uint8_t g_captive_portal_started;
/* DNS 服务线程栈较小；报文缓冲区放到静态存储，避免大请求压坏线程栈。 */
static uint8_t g_dns_packet[M1_DNS_PACKET_MAX];

/*
 * 返回任意 A 记录为 192.168.4.1。若客户端将 SoftAP 网关作为 DNS，
 * Android/iOS/Windows 的 HTTP 连通性探测会到达本机 Web 服务。
 */
static void m1_captive_dns_thread(mico_thread_arg_t arg)
{
    int fd = -1;
    struct sockaddr_in local_addr;

    (void)arg;
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!IsValidSocket(fd)) goto exit;

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(M1_DNS_PORT);
    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) goto exit;

    for (;;) {
        struct sockaddr_in remote_addr;
        socklen_t remote_len = sizeof(remote_addr);
        int length = recvfrom(fd, g_dns_packet, sizeof(g_dns_packet), 0,
                              (struct sockaddr *)&remote_addr, &remote_len);
        uint16_t question_end;
        uint16_t response_length;

        if (length < 17) continue;
        /* DNS 头中必须至少含有一个问题，且只处理标准查询。 */
        if ((g_dns_packet[2] & 0x80U) != 0U || ((g_dns_packet[4] == 0U) && (g_dns_packet[5] == 0U))) continue;
        question_end = 12U;
        while ((question_end < (uint16_t)length) && (g_dns_packet[question_end] != 0U)) {
            uint8_t label_length = g_dns_packet[question_end];
            if ((label_length == 0U) || (label_length > 63U) ||
                ((uint32_t)question_end + 1U + label_length >= (uint32_t)length)) {
                question_end = 0U;
                break;
            }
            question_end = (uint16_t)(question_end + 1U + label_length);
        }
        if ((question_end == 0U) || ((uint32_t)question_end + 5U > (uint32_t)length)) continue;
        question_end = (uint16_t)(question_end + 5U); /* 终止字节 + QTYPE + QCLASS */
        if ((uint32_t)question_end + 16U > sizeof(g_dns_packet)) continue;

        /* 保留事务 ID 和原问题，构造一条 A 答案。 */
        g_dns_packet[2] = 0x81U; g_dns_packet[3] = 0x80U;
        g_dns_packet[4] = 0x00U; g_dns_packet[5] = 0x01U;
        g_dns_packet[6] = 0x00U; g_dns_packet[7] = 0x01U;
        g_dns_packet[8] = 0x00U; g_dns_packet[9] = 0x00U;
        g_dns_packet[10] = 0x00U; g_dns_packet[11] = 0x00U;
        g_dns_packet[question_end + 0U] = 0xC0U; g_dns_packet[question_end + 1U] = 0x0CU;
        g_dns_packet[question_end + 2U] = 0x00U; g_dns_packet[question_end + 3U] = 0x01U;
        g_dns_packet[question_end + 4U] = 0x00U; g_dns_packet[question_end + 5U] = 0x01U;
        g_dns_packet[question_end + 6U] = 0x00U; g_dns_packet[question_end + 7U] = 0x00U;
        g_dns_packet[question_end + 8U] = 0x00U; g_dns_packet[question_end + 9U] = 0x3CU;
        g_dns_packet[question_end + 10U] = 0x00U; g_dns_packet[question_end + 11U] = 0x04U;
        g_dns_packet[question_end + 12U] = 192U; g_dns_packet[question_end + 13U] = 168U;
        g_dns_packet[question_end + 14U] = 4U; g_dns_packet[question_end + 15U] = 1U;
        response_length = (uint16_t)(question_end + 16U);
        (void)sendto(fd, g_dns_packet, response_length, 0,
                     (struct sockaddr *)&remote_addr, remote_len);
    }

exit:
    if (IsValidSocket(fd)) SocketClose(&fd);
    mico_rtos_delete_thread(NULL);
}

OSStatus m1_captive_portal_start(void)
{
    if (g_captive_portal_started != 0U) return kNoErr;
    if (mico_rtos_create_thread(NULL, MICO_APPLICATION_PRIORITY, "M1 Captive DNS",
                                m1_captive_dns_thread, 0x600U, 0) != kNoErr) {
        return kNoResourcesErr;
    }
    g_captive_portal_started = 1U;
    return kNoErr;
}
