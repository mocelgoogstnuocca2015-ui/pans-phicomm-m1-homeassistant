#include "mico.h"
#include "HTTPUtils.h"
#include "SocketUtils.h"
#include "system_internal.h"
#include "m1_firmware_version.h"
#include "m1_settings.h"
#include "m1_wifi_scan.h"

#include <stdio.h>
#include <string.h>

/*
 * 常驻本地管理页。所有接口都是相对路径，因而不依赖 SoftAP 或 DHCP 地址。
 * 密码只由浏览器 POST 给设备；状态接口永远不会返回既有 Wi-Fi/MQTT 密码。
 */
static const char m1_setup_page[] =
"<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>M1 本地管理</title><style>"
"body{font-family:system-ui,sans-serif;margin:0;background:#f4f7fb;color:#172033}main{max-width:560px;margin:24px auto;padding:20px;background:#fff;border-radius:14px;box-shadow:0 4px 20px #0001}h1{margin:0 0 4px}.panel{margin:16px 0;padding:12px;border:1px solid #d8e0eb;border-radius:9px;background:#f9fbfd}.row{display:flex;justify-content:space-between;gap:16px;padding:4px 0;font-size:.94em}.row b{word-break:break-all}label{display:block;margin:14px 0 5px;font-weight:600}input,button{box-sizing:border-box;width:100%;padding:11px;border:1px solid #b9c4d4;border-radius:8px;font:inherit;background:#fff}button{margin-top:10px;background:#0b73d9;color:#fff;border:0;font-weight:700}.secondary{background:#eef3f9;color:#0b5fac}button:disabled{opacity:.65;cursor:wait}.hint{font-size:.9em;color:#5d6a7a}#status{min-height:1.5em;color:#075fae}"
".wifi-picker{border:1px solid #b9c4d4;border-radius:8px;overflow:hidden;background:#fff}.wifi-picker summary{display:flex;align-items:center;gap:8px;min-height:44px;box-sizing:border-box;padding:10px 12px;font-size:14px;cursor:pointer;list-style:none}.wifi-picker summary::-webkit-details-marker{display:none}.wifi-picker summary:focus-visible,.wifi-option:focus-visible{outline:2px solid #0b73d9;outline-offset:-2px}#ssidText{flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.wifi-arrow{flex:none;color:#5d6a7a}#wifiOptions{max-height:300px;max-height:min(45vh,300px);overflow-y:auto;border-top:1px solid #d8e0eb;overscroll-behavior:contain}.wifi-option{display:flex;align-items:center;gap:8px;width:100%;margin:0;padding:10px 12px;min-height:44px;border:0;border-radius:0;border-bottom:1px solid #edf1f6;background:#fff;color:#172033;font-size:13px;font-weight:400;line-height:20px;text-align:left;white-space:nowrap}.wifi-option[aria-selected=true]{background:#e9f3ff;color:#075fae}.wifi-option:hover{background:#eef7ff}.wifi-name{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.wifi-rssi{flex:none;font-size:12px;font-variant-numeric:tabular-nums;color:#5d6a7a;white-space:nowrap}.wifi-empty{padding:12px;font-size:13px;color:#5d6a7a}#scanStatus{margin:6px 0 12px;min-height:1.4em;font-size:13px;line-height:1.4;color:#5d6a7a}#scanStatus[data-state=busy]{color:#075fae}#scanStatus[data-state=error]{color:#a23425}"
"</style></head><body><main>"
"<h1>斐讯 M1 本地管理</h1><p class=\"hint\">首次连接热点时访问 <b>192.168.4.1</b>。联网后，请访问路由器分配给 M1 的地址。</p>"
"<section class=\"panel\"><div class=\"row\"><span>工作状态</span><b id=\"mode\">读取中</b></div><div class=\"row\"><span>当前 IP</span><b id=\"ip\">读取中</b></div><div class=\"row\"><span>Wi-Fi</span><b id=\"currentSsid\">读取中</b></div><div class=\"row\"><span>MQTT</span><b id=\"mqtt\">读取中</b></div><div class=\"row\"><span>固件</span><b id=\"firmware\">读取中</b></div></section>"
"<p class=\"hint\">扫描后从列表选择 Wi-Fi。仅修改 MQTT 时，Wi-Fi 密码留空即可保留原设置。</p>"
"<form id=\"configForm\"><label id=\"ssidLabel\" for=\"ssidSummary\">Wi-Fi 名称（SSID）</label><input id=\"ssid\" type=\"hidden\">"
"<details id=\"wifiPicker\" class=\"wifi-picker\"><summary id=\"ssidSummary\" aria-labelledby=\"ssidLabel ssidText\"><span id=\"ssidText\">请选择 Wi-Fi</span><span class=\"wifi-arrow\" aria-hidden=\"true\">▾</span></summary><div id=\"wifiOptions\" role=\"listbox\" aria-labelledby=\"ssidLabel\"><div class=\"wifi-empty\">点击下方按钮扫描附近网络</div></div></details>"
"<button id=\"scanButton\" class=\"secondary\" type=\"button\">扫描附近 2.4 GHz Wi-Fi</button><p id=\"scanStatus\" role=\"status\" aria-live=\"polite\">仅显示 2.4 GHz 网络，列表可上下滚动。</p>"
"<label for=\"wifi\">Wi-Fi 密码</label><input id=\"wifi\" type=\"password\" maxlength=\"64\" autocomplete=\"new-password\"><p id=\"wifiHelp\" class=\"hint\"></p>"
"<label for=\"host\">MQTT 主机 / Home Assistant 地址</label><input id=\"host\" maxlength=\"63\" required placeholder=\"例如 192.168.1.10\">"
"<label for=\"port\">MQTT 端口</label><input id=\"port\" type=\"number\" min=\"1\" max=\"65535\" value=\"1883\" required>"
"<label for=\"user\">MQTT 用户名（没有可留空）</label><input id=\"user\" maxlength=\"47\" autocomplete=\"username\">"
"<label for=\"pass\">MQTT 密码（没有可留空）</label><input id=\"pass\" type=\"password\" maxlength=\"63\" autocomplete=\"new-password\">"
"<button id=\"apply\" type=\"submit\">保存并应用</button></form><p id=\"status\" role=\"status\"></p></main><script>"
"const $=id=>document.getElementById(id);let setup=true,scanning=false,ssidTouched=false,networks=[];"
"function scanMessage(text,state){$('scanStatus').textContent=text;$('scanStatus').dataset.state=state||''}"
"function selectSsid(value,touched){$('ssid').value=value;$('ssidText').textContent=value||'请选择 Wi-Fi';$('ssidText').title=value||'';if(touched)ssidTouched=true;document.querySelectorAll('.wifi-option').forEach(o=>o.setAttribute('aria-selected',String(o.dataset.ssid===value)))}"
"function renderNetworks(){const list=$('wifiOptions');list.replaceChildren();if(!networks.length){const empty=document.createElement('div');empty.className='wifi-empty';empty.textContent='暂无扫描结果，请点击下方按钮';list.append(empty);return}networks.forEach(a=>{const row=document.createElement('button'),name=document.createElement('span'),rssi=document.createElement('span');row.type='button';row.className='wifi-option';row.dataset.ssid=a.ssid;row.setAttribute('role','option');row.setAttribute('aria-selected',String(a.ssid===$('ssid').value));name.className='wifi-name';name.textContent=a.ssid;name.title=a.ssid;rssi.className='wifi-rssi';rssi.textContent=a.rssi+' dBm';row.append(name,rssi);row.onclick=()=>{selectSsid(a.ssid,true);$('wifiPicker').open=false;$('ssidSummary').focus()};list.append(row)})}"
"function setScanOptions(aps){const best=new Map(),chosen=$('ssid').value;(aps||[]).forEach(a=>{if(!a.ssid)return;const old=best.get(a.ssid);if(!old||Number(a.rssi)>Number(old.rssi))best.set(a.ssid,a)});networks=Array.from(best.values()).sort((a,b)=>Number(b.rssi)-Number(a.rssi));if(!chosen&&networks.length)selectSsid(networks[0].ssid,false);renderNetworks();return networks.length}"
"async function jsonRequest(url,options){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),5000);try{const r=await fetch(url,Object.assign({cache:'no-store',signal:controller.signal},options));if(!r.ok)throw Error('设备响应异常');return await r.json()}finally{clearTimeout(timer)}}"
"async function load(){if(scanning)return;try{const j=await jsonRequest('/m1-status');setup=!!j.setup;$('mode').textContent=setup?'首次配网热点':'已联网';$('ip').textContent=j.ip||'等待 DHCP 地址';$('currentSsid').textContent=j.ssid||'未设置';$('mqtt').textContent=j.mqtt_configured?'已配置':'未配置';$('firmware').textContent=j.firmware||'未知';if(j.ssid&&!ssidTouched&&!$('ssid').value)selectSsid(j.ssid,false);if(!$('host').value)$('host').value=j.mqtt_host||'';if(!$('port').dataset.touched)$('port').value=j.mqtt_port||1883;$('wifiHelp').textContent=setup?'选择 Wi-Fi 后填写密码，开放网络可留空。':'留空将保留现有 Wi-Fi 密码；填写后才会替换 Wi-Fi。'}catch(e){$('mode').textContent='连接管理服务失败'}}"
"async function scan(){if(scanning)return;scanning=true;$('scanButton').disabled=true;$('apply').disabled=true;$('scanButton').textContent='扫描中…';$('wifiPicker').setAttribute('aria-busy','true');scanMessage('正在扫描附近的 2.4 GHz Wi-Fi，请稍候…','busy');try{const start=await jsonRequest('/m1-wifi-scan',{method:'POST'});if(start.ok===false)throw Error(start.message||'无法开始扫描');const before=start.generation,deadline=Date.now()+21000;while(Date.now()<deadline){await new Promise(r=>setTimeout(r,1000));let j;try{j=await jsonRequest('/m1-wifi-scan')}catch(e){continue}if(j.ok===false)throw Error(j.message||'扫描失败');if(j.generation!==before&&!j.scanning){const count=setScanOptions(j.aps);scanMessage(count?'找到 '+count+' 个 Wi-Fi，按信号从强到弱排列。'+(j.limited?'仅保留最强的 64 个。':'向下滚动可查看更多。'):'未发现有名称的 2.4 GHz Wi-Fi，请重试。',count?'':'error');if(count)$('wifiPicker').open=true;return}}throw Error('扫描超时，请保持连接 M1 后重试')}catch(e){scanMessage((e.name==='AbortError'?'扫描请求超时，请重试':e.message)+(networks.length?'，已保留上次列表。':''),'error')}finally{scanning=false;$('scanButton').disabled=false;$('apply').disabled=false;$('scanButton').textContent='重新扫描 2.4 GHz Wi-Fi';$('wifiPicker').setAttribute('aria-busy','false')}}"
"$('scanButton').addEventListener('click',scan);"
"$('wifiOptions').addEventListener('keydown',e=>{const buttons=Array.from(document.querySelectorAll('.wifi-option')),index=buttons.indexOf(document.activeElement);if(e.key==='Escape'){$('wifiPicker').open=false;$('ssidSummary').focus()}else if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();if(buttons.length)buttons[(index+(e.key==='ArrowDown'?1:buttons.length-1)+buttons.length)%buttons.length].focus()}});"
"async function submitConfig(e){e.preventDefault();if(!$('ssid').value){scanMessage('请先扫描并选择 Wi-Fi','error');$('wifiPicker').open=true;$('ssidSummary').focus();return}const p=+$('port').value;if(p<1||p>65535){$('status').textContent='MQTT 端口无效';return}const changeWifi=setup||$('wifi').value.length>0,changeCredentials=setup||$('user').value.length>0||$('pass').value.length>0;const body={ssid:$('ssid').value,wifi_password:$('wifi').value,change_wifi:changeWifi,mqtt_host:$('host').value,mqtt_port:p,mqtt_username:$('user').value,mqtt_password:$('pass').value,change_mqtt_credentials:changeCredentials};$('apply').disabled=true;$('status').textContent='正在保存配置…';try{const r=await fetch('/m1-setup-save',{method:'POST',headers:{'Content-Type':'application/json'},cache:'no-store',body:JSON.stringify(body)});const j=await r.json();if(!r.ok||!j.ok)throw Error(j.message||'保存失败');$('status').textContent=changeWifi?'已保存，设备正在重启并连接 Wi-Fi。请让手机回到家庭 Wi-Fi 后，从路由器 DHCP 列表打开 M1 的新地址。':'MQTT 已保存，设备正在重启。'}catch(x){$('apply').disabled=false;$('status').textContent='保存失败：'+(x.message||'请保持连接 M1 后重试。')}}"
"$('configForm').addEventListener('submit',submitConfig);$('port').addEventListener('input',()=>$('port').dataset.touched='1');load();setInterval(load,5000);"
"</script></body></html>";

static OSStatus m1_setup_send_header(int fd, const char *mime, size_t length)
{
    OSStatus err;
    uint8_t *header = 0;
    size_t header_length = 0U;

    err = CreateSimpleHTTPMessageNoCopy(mime, length, &header, &header_length);
    if (err != kNoErr) return err;
    /* 首配页频繁迭代时不能让手机浏览器沿用旧脚本。 */
    if (header_length + sizeof("Cache-Control: no-store\r\n") < 200U) {
        snprintf((char *)header + header_length - 2U, 200U - header_length + 2U,
                 "Cache-Control: no-store\r\n\r\n");
        header_length = strlen((char *)header);
    }
    err = SocketSend(fd, header, header_length);
    free(header);
    return err;
}

static OSStatus m1_setup_send(int fd, const char *mime, const uint8_t *body, size_t length)
{
    OSStatus err = m1_setup_send_header(fd, mime, length);
    if ((err == kNoErr) && (length != 0U)) err = SocketSend(fd, body, length);
    return err;
}

static uint32_t m1_setup_append(char *out, uint32_t capacity, uint32_t used, const char *text)
{
    while ((*text != '\0') && (used + 1U < capacity)) out[used++] = *text++;
    out[used] = '\0';
    return used;
}

static uint32_t m1_setup_append_json_string(char *out, uint32_t capacity, uint32_t used, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    char escaped[7];

    if (used + 1U < capacity) out[used++] = '\"';
    while ((*cursor != '\0') && (used + 1U < capacity)) {
        if ((*cursor == '\"') || (*cursor == '\\')) {
            if (used + 2U >= capacity) break;
            out[used++] = '\\'; out[used++] = (char)*cursor;
        } else if (*cursor < 0x20U) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)*cursor);
            used = m1_setup_append(out, capacity, used, escaped);
        } else {
            out[used++] = (char)*cursor;
        }
        ++cursor;
    }
    if (used + 1U < capacity) out[used++] = '\"';
    out[used] = '\0';
    return used;
}

/* 单条 SSID 最多 32 字节，每字节 JSON 转义最多 6 字节，预留完整元数据空间。 */
#define M1_SCAN_JSON_ITEM_SIZE 288U
static OSStatus m1_setup_send_result(int fd, uint8_t ok, const char *message);

static uint32_t m1_setup_scan_item(char *body, const m1_wifi_ap_t *ap, uint8_t first)
{
    char number[80];
    uint32_t used = 0U;
    used = m1_setup_append(body, M1_SCAN_JSON_ITEM_SIZE, used, first ? "{\"ssid\":" : ",{\"ssid\":");
    used = m1_setup_append_json_string(body, M1_SCAN_JSON_ITEM_SIZE, used, ap->ssid);
    snprintf(number, sizeof(number), ",\"rssi\":%d,\"channel\":%u,\"security\":%u}",
             (int)ap->rssi, (unsigned int)ap->channel, (unsigned int)ap->security);
    return m1_setup_append(body, M1_SCAN_JSON_ITEM_SIZE, used, number);
}

static OSStatus m1_setup_send_scan(int fd, uint8_t start)
{
    m1_wifi_scan_results_t *results = malloc(sizeof(*results));
    char prefix[160];
    char item[M1_SCAN_JSON_ITEM_SIZE];
    uint32_t prefix_length;
    uint32_t length;
    uint8_t index;
    OSStatus err;

    if (results == 0) return m1_setup_send_result(fd, 0U, "内存不足，请稍后扫描");
    memset(results, 0, sizeof(*results));
    err = m1_wifi_scan_get_results(results);
    /* POST 发起扫描，GET 只读快照；发起响应返回扫描前的序号用于轮询。 */
    if ((err == kNoErr) && (start != 0U)) {
        err = m1_wifi_scan_request();
        results->scanning = 1U;
    }
    if (err != kNoErr) {
        free(results);
        return m1_setup_send_result(fd, 0U, "扫描服务暂不可用，请重试");
    }
    prefix_length = (uint32_t)snprintf(prefix, sizeof(prefix),
        "{\"ok\":true,\"generation\":%lu,\"scanning\":%s,\"count\":%u,\"source_count\":%u,\"limited\":%s,\"aps\":[",
        (unsigned long)results->generation, results->scanning ? "true" : "false",
        (unsigned int)results->count, (unsigned int)results->source_count,
        results->limited ? "true" : "false");
    length = prefix_length + 2U;
    for (index = 0U; index < results->count; ++index) {
        length += m1_setup_scan_item(item, &results->ap[index], index == 0U);
    }
    /* 分段发送完整快照，既不受旧 2 KiB JSON 缓冲限制，也不放大线程栈。 */
    err = m1_setup_send_header(fd, kMIMEType_JSON, length);
    if (err == kNoErr) err = SocketSend(fd, (const uint8_t *)prefix, prefix_length);
    for (index = 0U; (err == kNoErr) && (index < results->count); ++index) {
        length = m1_setup_scan_item(item, &results->ap[index], index == 0U);
        err = SocketSend(fd, (const uint8_t *)item, length);
    }
    if (err == kNoErr) err = SocketSend(fd, (const uint8_t *)"]}", 2U);
    free(results);
    return err;
}

static OSStatus m1_setup_send_result(int fd, uint8_t ok, const char *message)
{
    char body[160];
    snprintf(body, sizeof(body), "{\"ok\":%s,\"message\":\"%s\"}",
             (ok != 0U) ? "true" : "false", message);
    return m1_setup_send(fd, kMIMEType_JSON, (const uint8_t *)body, strlen(body));
}

/* 管理页状态只包含可公开诊断信息，绝不回传 Wi-Fi/MQTT 密码。 */
static OSStatus m1_setup_send_status(int fd)
{
    system_context_t *context = system_context();
    IPStatusTypedef ip;
    char body[384];
    char ssid[maxSsidLen + 1U];
    char escaped_ssid[2U * maxSsidLen + 8U];
    m1_mqtt_config_t mqtt;
    uint32_t used = 0U;
    uint8_t setup = 1U;

    if (context == 0) return m1_setup_send_result(fd, 0U, "系统配置不可用");
    memset(&ip, 0, sizeof(ip));
    memset(ssid, 0, sizeof(ssid));
    memset(&mqtt, 0, sizeof(mqtt));
    if (mico_rtos_lock_mutex(&context->flashContentInRam_mutex) == kNoErr) {
        setup = (context->flashContentInRam.micoSystemConfig.configured == unConfigured) ? 1U : 0U;
        strncpy(ssid, context->flashContentInRam.micoSystemConfig.ssid, maxSsidLen);
        (void)mico_rtos_unlock_mutex(&context->flashContentInRam_mutex);
    }
    /* 首刷时不展示原厂遗留的 SSID，避免让新用户误以为已经完成配网。 */
    if (setup != 0U) memset(ssid, 0, sizeof(ssid));
    (void)micoWlanGetIPStatus(&ip, Station);
    (void)m1_settings_get_mqtt(&mqtt);
    if ((ip.ip[0] == '\0') || (strcmp(ip.ip, "0.0.0.0") == 0)) {
        strncpy(ip.ip, setup ? "192.168.4.1" : "等待 DHCP 地址", sizeof(ip.ip) - 1U);
    }
    memset(escaped_ssid, 0, sizeof(escaped_ssid));
    (void)m1_setup_append_json_string(escaped_ssid, sizeof(escaped_ssid), 0U, ssid);
    used = m1_setup_append(body, sizeof(body), used, "{\"setup\":");
    used = m1_setup_append(body, sizeof(body), used, setup ? "true" : "false");
    used = m1_setup_append(body, sizeof(body), used, ",\"ip\":");
    used = m1_setup_append_json_string(body, sizeof(body), used, ip.ip);
    used = m1_setup_append(body, sizeof(body), used, ",\"ssid\":");
    used = m1_setup_append(body, sizeof(body), used, escaped_ssid);
    used = m1_setup_append(body, sizeof(body), used, ",\"mqtt_configured\":");
    used = m1_setup_append(body, sizeof(body), used,
                           m1_settings_mqtt_is_configured() ? "true" : "false");
    used = m1_setup_append(body, sizeof(body), used, ",\"mqtt_host\":");
    used = m1_setup_append_json_string(body, sizeof(body), used, mqtt.host);
    snprintf(escaped_ssid, sizeof(escaped_ssid), ",\"mqtt_port\":%u",
             (unsigned int)mqtt.port);
    used = m1_setup_append(body, sizeof(body), used, escaped_ssid);
    used = m1_setup_append(body, sizeof(body), used, ",\"firmware\":\"");
    used = m1_setup_append(body, sizeof(body), used, M1_FIRMWARE_VERSION);
    used = m1_setup_append(body, sizeof(body), used, "\"}");
    return m1_setup_send(fd, kMIMEType_JSON, (const uint8_t *)body, used);
}

/*
 * 独立于 SDK /config-write：后者先回 200 再异步解析/重启，手机浏览器无法
 * 区分“确实已落盘”和“请求未被处理”。此路径先完整验证及持久化，随后返回 JSON。
 */
static OSStatus m1_setup_save(int fd, HTTPHeader_t *in_header)
{
    json_object *request = 0;
    json_object *value;
    const char *ssid;
    const char *wifi_password;
    const char *mqtt_host;
    const char *mqtt_username;
    const char *mqtt_password;
    int mqtt_port;
    uint8_t change_wifi;
    uint8_t change_mqtt_credentials;
    system_context_t *context;
    OSStatus err = kNoErr;

    if ((in_header == 0) || (in_header->contentLength == 0U) ||
        (in_header->extraDataPtr == 0)) {
        return m1_setup_send_result(fd, 0U, "请求内容为空");
    }
    request = json_tokener_parse(in_header->extraDataPtr);
    if (request == 0) return m1_setup_send_result(fd, 0U, "请求格式无效");

    value = json_object_object_get(request, "ssid");
    ssid = (value != 0) ? json_object_get_string(value) : 0;
    value = json_object_object_get(request, "wifi_password");
    wifi_password = (value != 0) ? json_object_get_string(value) : 0;
    value = json_object_object_get(request, "mqtt_host");
    mqtt_host = (value != 0) ? json_object_get_string(value) : 0;
    value = json_object_object_get(request, "mqtt_port");
    mqtt_port = (value != 0) ? json_object_get_int(value) : 0;
    value = json_object_object_get(request, "mqtt_username");
    mqtt_username = (value != 0) ? json_object_get_string(value) : 0;
    value = json_object_object_get(request, "mqtt_password");
    mqtt_password = (value != 0) ? json_object_get_string(value) : 0;
    value = json_object_object_get(request, "change_wifi");
    change_wifi = (value != 0) && json_object_get_boolean(value) ? 1U : 0U;
    value = json_object_object_get(request, "change_mqtt_credentials");
    change_mqtt_credentials = (value != 0) && json_object_get_boolean(value) ? 1U : 0U;

    if ((ssid == 0) || (strlen(ssid) >= maxSsidLen) ||
        (wifi_password == 0) || (strlen(wifi_password) >= maxKeyLen) ||
        (mqtt_host == 0) || (mqtt_host[0] == '\0') ||
        (mqtt_username == 0) || (mqtt_password == 0) ||
        (mqtt_port <= 0) || (mqtt_port > 65535)) {
        json_object_put(request);
        return m1_setup_send_result(fd, 0U, "Wi-Fi 或 MQTT 参数无效");
    }

    err = m1_settings_set_mqtt_host(mqtt_host);
    if (err == kNoErr) err = m1_settings_set_mqtt_port((uint16_t)mqtt_port);
    if ((err == kNoErr) && (change_mqtt_credentials != 0U)) {
        err = m1_settings_set_mqtt_username(mqtt_username);
    }
    if ((err == kNoErr) && (change_mqtt_credentials != 0U)) {
        err = m1_settings_set_mqtt_password(mqtt_password);
    }
    if (err != kNoErr) {
        json_object_put(request);
        return m1_setup_send_result(fd, 0U, "MQTT 参数无效");
    }

    context = system_context();
    if (context == 0) {
        json_object_put(request);
        return m1_setup_send_result(fd, 0U, "系统配置不可用");
    }
    if (mico_rtos_lock_mutex(&context->flashContentInRam_mutex) != kNoErr) {
        json_object_put(request);
        return m1_setup_send_result(fd, 0U, "配置正忙，请重试");
    }
    if ((change_wifi != 0U) ||
        (context->flashContentInRam.micoSystemConfig.configured == unConfigured)) {
        if ((ssid[0] == '\0') || ((change_wifi == 0U) &&
            (context->flashContentInRam.micoSystemConfig.configured == unConfigured))) {
            (void)mico_rtos_unlock_mutex(&context->flashContentInRam_mutex);
            json_object_put(request);
            return m1_setup_send_result(fd, 0U, "首次配网必须填写 Wi-Fi 名称");
        }
        memset(context->flashContentInRam.micoSystemConfig.ssid, 0,
               sizeof(context->flashContentInRam.micoSystemConfig.ssid));
        memset(context->flashContentInRam.micoSystemConfig.key, 0,
               sizeof(context->flashContentInRam.micoSystemConfig.key));
        memset(context->flashContentInRam.micoSystemConfig.user_key, 0,
               sizeof(context->flashContentInRam.micoSystemConfig.user_key));
        strncpy(context->flashContentInRam.micoSystemConfig.ssid, ssid, maxSsidLen - 1U);
        strncpy(context->flashContentInRam.micoSystemConfig.key, wifi_password, maxKeyLen - 1U);
        strncpy(context->flashContentInRam.micoSystemConfig.user_key, wifi_password, maxKeyLen - 1U);
        context->flashContentInRam.micoSystemConfig.keyLength = strlen(wifi_password);
        context->flashContentInRam.micoSystemConfig.user_keyLength = strlen(wifi_password);
        context->flashContentInRam.micoSystemConfig.channel = 0U;
        memset(context->flashContentInRam.micoSystemConfig.bssid, 0,
               sizeof(context->flashContentInRam.micoSystemConfig.bssid));
        context->flashContentInRam.micoSystemConfig.security = SECURITY_TYPE_AUTO;
        context->flashContentInRam.micoSystemConfig.dhcpEnable = true;
    }
    context->flashContentInRam.micoSystemConfig.configured = allConfigured;
    (void)mico_rtos_unlock_mutex(&context->flashContentInRam_mutex);

    err = mico_system_context_update(&context->flashContentInRam);
    json_object_put(request);
    if (err != kNoErr) return m1_setup_send_result(fd, 0U, "配置写入失败");

    err = m1_setup_send_result(fd, 1U, "已保存");
    if (err == kNoErr) {
        /* 给浏览器完整接收 JSON 的时间，随后再切换到目标 Wi-Fi。 */
        mico_thread_msleep(600U);
        mico_system_power_perform(&context->flashContentInRam, eState_Software_Reset);
    }
    return err;
}

OSStatus config_server_delegate_http(int fd, HTTPHeader_t *in_header)
{
    if (in_header == 0) {
        return kNotFoundErr;
    }
    if ((HTTPHeaderMatchMethod(in_header, "POST") == kNoErr) &&
        (HTTPHeaderMatchURL(in_header, "/m1-setup-save") == kNoErr)) {
        return m1_setup_save(fd, in_header);
    }
    if ((HTTPHeaderMatchMethod(in_header, "POST") == kNoErr) &&
        (HTTPHeaderMatchURL(in_header, "/m1-wifi-scan") == kNoErr)) {
        return m1_setup_send_scan(fd, 1U);
    }
    /* 旧 SDK 路径会读取或写入明文密钥，常驻管理服务中不允许再走它们。 */
    if ((HTTPHeaderMatchURL(in_header, "/config-read") == kNoErr) ||
        (HTTPHeaderMatchURL(in_header, "/config-write") == kNoErr) ||
        (HTTPHeaderMatchURL(in_header, "/config-write-uap") == kNoErr) ||
        (HTTPHeaderMatchURL(in_header, "/OTA") == kNoErr)) {
        return m1_setup_send_result(fd, 0U, "请使用 M1 本地管理接口");
    }
    if (HTTPHeaderMatchMethod(in_header, "GET") != kNoErr) return kNotFoundErr;
    if (HTTPHeaderMatchURL(in_header, "/m1-status") == kNoErr) {
        return m1_setup_send_status(fd);
    }
    if (HTTPHeaderMatchURL(in_header, "/m1-wifi-scan") == kNoErr) {
        return m1_setup_send_scan(fd, 0U);
    }
    if (HTTPHeaderMatchURL(in_header, "/") == kNoErr) {
        return m1_setup_send(fd, "text/html; charset=utf-8",
                             (const uint8_t *)m1_setup_page, sizeof(m1_setup_page) - 1U);
    }
    /* Android、iOS、Windows 的 HTTP 探测路径也返回门户页面。 */
    return m1_setup_send(fd, "text/html; charset=utf-8",
                         (const uint8_t *)m1_setup_page, sizeof(m1_setup_page) - 1U);
}
