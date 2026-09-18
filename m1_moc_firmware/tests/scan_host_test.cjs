// 从生产源码提取扫描与 JSON 实现，替换硬件接口后运行主机边界测试。
// 运行：node m1_moc_firmware/tests/scan_host_test.cjs
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const out = path.join(root, 'output/scan-host-test');
fs.mkdirSync(out, {recursive:true});
const scan = fs.readFileSync(path.join(root,'m1_moc_firmware/m1_wifi_scan.c'),'utf8');
const portal = fs.readFileSync(path.join(root,'m1_moc_firmware/m1_setup_portal.c'),'utf8');
const header = fs.readFileSync(path.join(root,'m1_moc_firmware/m1_wifi_scan.h'),'utf8').replace('#include "mico.h"','');
function extract(source, name) {
  const start = source.search(new RegExp('^(?:static )?(?:OSStatus|void|uint32_t) '+name+'\\(', 'm'));
  if (start < 0) throw Error('生产源码中没有找到：'+name);
  return source.slice(start, source.indexOf('\n}',start)+2);
}
const code = `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int OSStatus;
typedef int wlan_sec_type_t;
typedef struct mico_context mico_Context_t;
enum {kNoErr=0,kGeneralErr=-1,kParamErr=-2,kNotInitializedErr=-3};
${header}
typedef struct { char ssid[32]; char bssid[6]; char channel; int security; int16_t rssi; } TestAP;
typedef struct { char ApNum; TestAP *ApList; } ScanResult_adv;
static m1_wifi_scan_results_t g_results;
static int g_results_lock;
static uint8_t g_started=1;
static uint32_t g_scan_started_at, now, requests;
#define M1_WIFI_SCAN_TIMEOUT_MS 20000U
static int mico_rtos_lock_mutex(int *p) {(void)p;return 0;}
static int mico_rtos_unlock_mutex(int *p) {(void)p;return 0;}
static uint32_t mico_rtos_get_time(void) {return now;}
static void micoWlanStartScanAdv(void) {requests++;}
static void m1_wifi_recovery_connect(const ScanResult_adv *s) {(void)s;}
${extract(scan,'m1_wifi_scan_completed')}
${extract(scan,'m1_wifi_scan_request')}
${extract(scan,'m1_wifi_scan_get_results')}
${extract(portal,'m1_setup_append')}
${extract(portal,'m1_setup_append_json_string')}
#define M1_SCAN_JSON_ITEM_SIZE 288U
#define kMIMEType_JSON "application/json"
static char response[20000];
static size_t response_length, declared_length;
static int m1_setup_send_header(int fd,const char *mime,size_t len) {(void)fd;(void)mime;declared_length=len;response_length=0;return 0;}
static int SocketSend(int fd,const uint8_t *p,size_t len) {(void)fd;assert(response_length+len<sizeof(response));memcpy(response+response_length,p,len);response_length+=len;response[response_length]=0;return 0;}
static int m1_setup_send_result(int fd,uint8_t ok,const char *message) {(void)fd;(void)ok;(void)message;return -9;}
${extract(portal,'m1_setup_scan_item')}
${extract(portal,'m1_setup_send_scan')}
int main(void) {
  TestAP aps[90]={0};
  ScanResult_adv raw={60,aps};
  unsigned i;
  assert(m1_wifi_scan_request()==0 && requests==1 && g_results.scanning);
  assert(m1_wifi_scan_request()==0 && requests==1);
  now=20001;
  assert(m1_wifi_scan_request()==0 && requests==2);
  for(i=0;i<40;i++){strcpy(aps[i].ssid,"same");aps[i].rssi=-80+(int)i;}
  for(i=40;i<60;i++){snprintf(aps[i].ssid,32,"network%02u",i);aps[i].rssi=-(int)i;}
  m1_wifi_scan_completed(&raw,0);
  assert(g_results.count==21 && !g_results.scanning && g_results.generation==1);
  m1_wifi_scan_completed(&raw,0);
  assert(g_results.generation==2 && g_results.source_count==60);
  for(i=0;i<80;i++){snprintf(aps[i].ssid,32,"network%02u",i);aps[i].rssi=-100+(int)i;}
  raw.ApNum=80;
  m1_wifi_scan_completed(&raw,0);
  assert(g_results.count==64 && g_results.limited && g_results.generation==3);
  assert(g_results.ap[0].rssi==-21 && g_results.ap[63].rssi==-84);
  for(i=1;i<g_results.count;i++)assert(g_results.ap[i-1].rssi>=g_results.ap[i].rssi);
  assert(m1_setup_send_scan(0,0)==0);
  assert(response_length==declared_length && response_length>2048);
  puts(response);
  for(i=0;i<64;i++){memset(g_results.ap[i].ssid,1,32);g_results.ap[i].ssid[32]=0;}
  assert(m1_setup_send_scan(0,0)==0 && response_length==declared_length);
  puts(response);
  assert(m1_setup_send_scan(0,1)==0 && g_results.scanning && requests==3);
  puts(response);
  m1_wifi_scan_completed(0,0);
  assert(g_results.count==0 && g_results.generation==4 && !g_results.scanning);
  return 0;
}
`;
const sourceFile=path.join(out,'scan_host_generated.c');
const exe=path.join(out,'scan_host_test.exe');
fs.writeFileSync(sourceFile,code);
execFileSync('gcc',['-std=c99','-Wall','-Wextra','-Werror',sourceFile,'-o',exe],{stdio:'inherit'});
const responses=execFileSync(exe,{encoding:'utf8'}).trim().split(/\r?\n/).map(JSON.parse);
if (responses.length!==3 || responses[0].aps.length!==64 || responses[1].aps.some(ap=>ap.ssid!=='\x01'.repeat(32)) || responses[2].generation!==3 || !responses[2].scanning) throw Error('扫描响应序列化边界验证失败');
console.log('通过：扫描去重、64 个最强 SSID、排序、递增序号、重复请求抑制、超时重试、空扫描、完整 JSON 长度与极端字符转义。');
