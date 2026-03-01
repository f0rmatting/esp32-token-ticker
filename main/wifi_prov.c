/*
 * SPDX-FileCopyrightText: 2025-2026 Bruce
 * SPDX-License-Identifier: CC-BY-NC-4.0
 */

#include "wifi_prov.h"
#include "ui.h"
#include "token_config.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"

#include <string.h>

static const char *TAG = "wifi_prov";

#define PROV_AP_SSID      "TokenTicker"
#define PROV_AP_CHANNEL   1
#define PROV_AP_MAX_CONN  4
#define NVS_NAMESPACE     "wifi_cfg"
#define NVS_KEY_SSID      "ssid"
#define NVS_KEY_PASS      "pass"
#define NVS_KEY_TZ        "tz"
#define DNS_PORT          53

static volatile bool s_dns_running = false;

/* ── NVS helpers ─────────────────────────────────────────────── */

esp_err_t wifi_prov_get_saved_creds(char *ssid, size_t ssid_len,
                                     char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) return ESP_ERR_NOT_FOUND;

    size_t len = ssid_len;
    ret = nvs_get_str(h, NVS_KEY_SSID, ssid, &len);
    if (ret != ESP_OK || len <= 1) { nvs_close(h); return ESP_ERR_NOT_FOUND; }

    len = pass_len;
    ret = nvs_get_str(h, NVS_KEY_PASS, pass, &len);
    if (ret != ESP_OK) { pass[0] = '\0'; } // open network, no password OK

    nvs_close(h);
    return ESP_OK;
}

esp_err_t wifi_prov_get_saved_tz(char *tz, size_t tz_len)
{
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) return ESP_ERR_NOT_FOUND;

    size_t len = tz_len;
    ret = nvs_get_str(h, NVS_KEY_TZ, tz, &len);
    nvs_close(h);
    return (ret == ESP_OK && len > 1) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t nvs_save_creds(const char *ssid, const char *pass, const char *tz)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "NVS open");
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass ? pass : "");
    if (tz && tz[0]) nvs_set_str(h, NVS_KEY_TZ, tz);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Credentials saved for SSID: %s, TZ: %s", ssid, tz ? tz : "(default)");
    return ESP_OK;
}

/* ── DNS captive portal responder ────────────────────────────── */

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "DNS socket failed"); vTaskDelete(NULL); return; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    // Set receive timeout so we can check s_dns_running
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    while (s_dns_running) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
        if (len < 12) continue; // Too short or timeout

        // Build DNS response: copy query, set response flags, append answer
        uint8_t resp[512];
        if ((size_t)len + 16 > sizeof(resp)) continue;
        memcpy(resp, buf, len);

        // Set QR=1 (response), AA=1 (authoritative), RA=1
        resp[2] = 0x85; // QR=1, Opcode=0, AA=1, TC=0, RD=1
        resp[3] = 0x80; // RA=1, RCODE=0

        // Set answer count = 1
        resp[6] = 0x00;
        resp[7] = 0x01;

        // Append answer: pointer to query name + A record → 192.168.4.1
        int pos = len;
        resp[pos++] = 0xC0; resp[pos++] = 0x0C; // Name pointer to offset 12
        resp[pos++] = 0x00; resp[pos++] = 0x01; // Type A
        resp[pos++] = 0x00; resp[pos++] = 0x01; // Class IN
        resp[pos++] = 0x00; resp[pos++] = 0x00;
        resp[pos++] = 0x00; resp[pos++] = 0x3C; // TTL 60s
        resp[pos++] = 0x00; resp[pos++] = 0x04; // Data length 4
        resp[pos++] = 192; resp[pos++] = 168;
        resp[pos++] = 4;   resp[pos++] = 1;     // 192.168.4.1

        sendto(sock, resp, pos, 0, (struct sockaddr *)&src, src_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ── Embedded HTML page ──────────────────────────────────────── */

/* IANA timezone → POSIX TZ mapping (common zones, embedded in JS) */
static const char PROV_HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>TokenTicker Setup</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:system-ui,sans-serif;background:#0F0F1A;color:#E0E0E0;"
    "display:flex;justify-content:center;padding:20px}"
    ".c{width:100%;max-width:360px}"
    "h2{text-align:center;color:#00E676;margin-bottom:20px}"
    "label{display:block;color:#888;font-size:13px;margin-top:12px}"
    "select,input,button{width:100%;padding:12px;margin:4px 0 8px;border:1px solid #333;"
    "border-radius:8px;background:#1A1A2E;color:#E0E0E0;font-size:16px}"
    "select:focus,input:focus{border-color:#00E676;outline:none}"
    "button{background:#00E676;color:#0F0F1A;font-weight:bold;border:none;cursor:pointer;margin-top:16px}"
    "button:active{opacity:.8}"
    ".s{text-align:center;color:#888;margin:12px 0}"
    ".tg{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:8px 0}"
    ".tg label{display:flex;align-items:center;gap:6px;color:#E0E0E0;font-size:14px;"
    "margin:0;padding:8px;background:#1A1A2E;border-radius:6px;border:1px solid #333;cursor:pointer}"
    ".tg input{width:auto;margin:0;accent-color:#00E676}"
    "#te{color:#FF5252;font-size:12px;text-align:center;min-height:16px}"
    "</style></head><body><div class='c'>"
    "<h2>TokenTicker</h2>"
    "<p class='s' id='st'>Scanning WiFi...</p>"
    "<select id='ss' style='display:none'><option>Loading...</option></select>"
    "<input id='pw' type='password' placeholder='Password'>"
    "<label>Timezone</label>"
    "<select id='tz'></select>"
    "<label>Tokens (select 4-6)</label>"
    "<div class='tg'>"
    "<label><input type='checkbox' value='bitcoin' checked>BTC</label>"
    "<label><input type='checkbox' value='ethereum' checked>ETH</label>"
    "<label><input type='checkbox' value='paxg' checked>PAXG</label>"
    "<label><input type='checkbox' value='chainbase' checked>Chainbase</label>"
    "<label><input type='checkbox' value='sui' checked>SUI</label>"
    "<label><input type='checkbox' value='doge'>DOGE</label>"
    "<label><input type='checkbox' value='solana'>SOL</label>"
    "<label><input type='checkbox' value='tron'>TRX</label>"
    "<label><input type='checkbox' value='usdc'>USDC</label>"
    "<label><input type='checkbox' value='usdt'>USDT</label>"
    "</div><p id='te'></p>"
    "<button onclick='go()'>Save</button>"
    "<div id='msg'></div>"
    "</div><script>"
    "var tz=document.getElementById('tz');"
    "var lo=Math.round(-new Date().getTimezoneOffset()/60);"
    "for(var i=-12;i<=14;i++){"
    "var o=document.createElement('option');"
    "o.textContent='UTC'+(i>=0?'+':'')+i;"
    "if(i===0)o.value='UTC0';"
    "else if(i>0)o.value='<+'+(i<10?'0':'')+i+'>-'+i;"
    "else o.value='<-'+((-i)<10?'0':'')+(-i)+'>'+(-i);"
    "if(i===lo)o.selected=true;"
    "tz.appendChild(o)}"
    "function scan(){"
    "fetch('/scan').then(r=>r.json()).then(d=>{"
    "let s=document.getElementById('ss');"
    "s.innerHTML='';"
    "d.forEach(n=>{"
    "let o=document.createElement('option');"
    "o.textContent=n.ssid+' ('+n.rssi+'dBm)';"
    "o.value=n.ssid;s.appendChild(o)});"
    "s.style.display='block';"
    "document.getElementById('st').textContent='Select your network:';"
    "}).catch(()=>{document.getElementById('st').textContent='Scan failed, retrying...';setTimeout(scan,2000)})}"
    "function go(){"
    "var cb=document.querySelectorAll('.tg input:checked');"
    "if(cb.length<4||cb.length>6){document.getElementById('te').textContent='Please select 4-6 tokens';return}"
    "document.getElementById('te').textContent='';"
    "var tk=Array.from(cb).map(e=>e.value).join(',');"
    "let ssid=document.getElementById('ss').value;"
    "let pass=document.getElementById('pw').value;"
    "let t=document.getElementById('tz').value;"
    "document.getElementById('msg').innerHTML='<p class=\"s\">Saving... Rebooting...</p>';"
    "fetch('/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "body:'ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass)+'&tz='+encodeURIComponent(t)+'&tokens='+encodeURIComponent(tk)})}"
    "scan();"
    "</script></body></html>";

/* ── HTTP handlers ───────────────────────────────────────────── */

static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PROV_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_scan(httpd_req_t *req)
{
    wifi_scan_config_t scan_cfg = { .show_hidden = false };
    esp_wifi_scan_start(&scan_cfg, true); // blocking scan

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    // Build JSON array
    int json_size = ap_count * 80 + 16;
    char *json = malloc(json_size);
    if (!json) {
        free(ap_list);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int pos = 0;
    int rem = json_size;
    pos += snprintf(json + pos, rem - pos, "[");
    // Track seen SSIDs to deduplicate
    for (int i = 0; i < ap_count && pos < rem - 1; i++) {
        if (ap_list[i].ssid[0] == '\0') continue; // skip hidden

        // Check duplicate
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp((char *)ap_list[i].ssid, (char *)ap_list[j].ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;

        if (pos > 1) pos += snprintf(json + pos, rem - pos, ",");
        // Escape SSID for JSON (simple: skip quotes in SSID)
        char safe_ssid[33];
        int k = 0;
        for (int c = 0; ap_list[i].ssid[c] && k < 31; c++) {
            if (ap_list[i].ssid[c] != '"' && ap_list[i].ssid[c] != '\\')
                safe_ssid[k++] = ap_list[i].ssid[c];
        }
        safe_ssid[k] = '\0';
        pos += snprintf(json + pos, rem - pos, "{\"ssid\":\"%s\",\"rssi\":%d}", safe_ssid, ap_list[i].rssi);
    }
    pos += snprintf(json + pos, rem - pos, "]");

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, pos);
    free(json);
    free(ap_list);
    return ret;
}

static esp_err_t handle_connect(httpd_req_t *req)
{
    char body[384] = {0};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[len] = '\0';

    // Parse URL-encoded: ssid=xxx&pass=yyy&tz=zzz&tokens=aaa,bbb,...
    char ssid[33] = {0}, pass[65] = {0}, tz[64] = {0}, tokens[128] = {0};

    // Simple URL-decode parser
    char *p = strstr(body, "ssid=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        int slen = end ? (end - p) : (int)strlen(p);
        if (slen > 32) slen = 32;
        // URL decode in-place
        int j = 0;
        for (int i = 0; i < slen && j < 32; i++) {
            if (p[i] == '+') { ssid[j++] = ' '; }
            else if (p[i] == '%' && i + 2 < slen) {
                char hex[3] = { p[i+1], p[i+2], 0 };
                ssid[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else { ssid[j++] = p[i]; }
        }
        ssid[j] = '\0';
    }

    p = strstr(body, "pass=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        int slen = end ? (end - p) : (int)strlen(p);
        if (slen > 64) slen = 64;
        int j = 0;
        for (int i = 0; i < slen && j < 64; i++) {
            if (p[i] == '+') { pass[j++] = ' '; }
            else if (p[i] == '%' && i + 2 < slen) {
                char hex[3] = { p[i+1], p[i+2], 0 };
                pass[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else { pass[j++] = p[i]; }
        }
        pass[j] = '\0';
    }

    p = strstr(body, "tz=");
    if (p) {
        p += 3;
        char *end = strchr(p, '&');
        int slen = end ? (end - p) : (int)strlen(p);
        if (slen > 63) slen = 63;
        int j = 0;
        for (int i = 0; i < slen && j < 63; i++) {
            if (p[i] == '+') { tz[j++] = ' '; }
            else if (p[i] == '%' && i + 2 < slen) {
                char hex[3] = { p[i+1], p[i+2], 0 };
                tz[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else { tz[j++] = p[i]; }
        }
        tz[j] = '\0';
    }

    p = strstr(body, "tokens=");
    if (p) {
        p += 7;
        char *end = strchr(p, '&');
        int slen = end ? (end - p) : (int)strlen(p);
        if (slen > 127) slen = 127;
        int j = 0;
        for (int i = 0; i < slen && j < 127; i++) {
            if (p[i] == '+') { tokens[j++] = ' '; }
            else if (p[i] == '%' && i + 2 < slen) {
                char hex[3] = { p[i+1], p[i+2], 0 };
                tokens[j++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else { tokens[j++] = p[i]; }
        }
        tokens[j] = '\0';
    }

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received credentials for SSID: %s, TZ: %s", ssid, tz);
    nvs_save_creds(ssid, pass, tz);

    if (tokens[0]) {
        token_config_save(tokens);
        ESP_LOGI(TAG, "Token selection saved: %s", tokens);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, "<html><body style='background:#0F0F1A;color:#00E676;"
                             "display:flex;justify-content:center;align-items:center;"
                             "height:100vh;font-family:system-ui'>"
                             "<h2>Saved! Rebooting...</h2></body></html>");

    // Delay to let response reach client, then reboot
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK; // unreachable
}

static esp_err_t handle_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

/* ── SoftAP provisioning entry ───────────────────────────────── */

void wifi_prov_start(void)
{
    ESP_LOGI(TAG, "Starting SoftAP provisioning mode");

    // Tear down any running UI and show clean WiFi config screen
    ui_cleanup();
    ui_show_wifi_config();

    // Stop current WiFi (STA mode)
    esp_wifi_stop();

    // Create AP netif
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    (void)ap_netif;

    // Configure AP
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = PROV_AP_SSID,
            .ssid_len = strlen(PROV_AP_SSID),
            .channel = PROV_AP_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = PROV_AP_MAX_CONN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP started: SSID=%s", PROV_AP_SSID);

    // Start DNS responder for captive portal
    s_dns_running = true;
    xTaskCreate(dns_task, "dns", 4096, NULL, 5, NULL);

    // Start HTTP server
    httpd_handle_t server = NULL;
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 8;
    http_cfg.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &http_cfg));

    // Register routes
    const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = handle_root,
    };
    const httpd_uri_t uri_scan = {
        .uri = "/scan", .method = HTTP_GET, .handler = handle_scan,
    };
    const httpd_uri_t uri_connect = {
        .uri = "/connect", .method = HTTP_POST, .handler = handle_connect,
    };
    const httpd_uri_t uri_gen204 = {
        .uri = "/generate_204", .method = HTTP_GET, .handler = handle_captive_redirect,
    };
    const httpd_uri_t uri_hotspot = {
        .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handle_captive_redirect,
    };

    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_scan);
    httpd_register_uri_handler(server, &uri_connect);
    httpd_register_uri_handler(server, &uri_gen204);
    httpd_register_uri_handler(server, &uri_hotspot);

    ESP_LOGI(TAG, "HTTP server started, waiting for user...");

    // Block forever — reboot happens in handle_connect
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
