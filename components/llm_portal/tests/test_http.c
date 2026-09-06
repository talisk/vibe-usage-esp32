#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "llm_portal.h"
#include "wifi_adapter.h"
#include "esp_http_server.h"
#include "nvs_fixture.h"

/* Contiguous linker symbols match ESP-IDF EMBED_TXTFILES, without embedding a
 * browser/page simulator in these protocol-focused handler tests. */
#if defined(__APPLE__)
__asm__(".section __TEXT,__const\n");
#else
__asm__(".section .rodata\n");
#endif
__asm__(".globl _binary_portal_html_start\n_binary_portal_html_start:\n"
        ".asciz \"<html>fixture</html>\"\n"
        ".globl _binary_portal_html_end\n_binary_portal_html_end:\n.text\n");

static httpd_uri_t routes[4];
static size_t route_count;
static bool connected = true, provisioning, receive_timeout;
static bool slow_body;
static int64_t clock_us;
static int fail_start, fail_register;
static char ip[16] = "192.168.1.30", session_token[33];
static unsigned random_seed;
int64_t esp_timer_get_time(void) { return clock_us; }
void esp_fill_random(void *out, size_t size) {
    for (size_t i = 0; i < size; ++i) ((unsigned char *)out)[i] = (unsigned char)++random_seed;
}
bool wifi_adapter_is_connected(void) { return connected; }
bool wifi_adapter_is_provisioning(void) { return provisioning; }
esp_err_t wifi_adapter_station_ip(char *out, size_t capacity) {
    if (!connected) return ESP_ERR_INVALID_STATE;
    assert(capacity > strlen(ip)); strcpy(out, ip); return ESP_OK;
}
esp_err_t httpd_start(httpd_handle_t *server, const httpd_config_t *config) {
    assert(config->server_port == 80 && config->max_open_sockets == 2);
    assert(config->recv_wait_timeout <= 5 && config->stack_size >= 6144);
    if (fail_start) return ESP_FAIL;
    *server = (void *)1; route_count = 0; return ESP_OK;
}
esp_err_t httpd_stop(httpd_handle_t server) { assert(server == (void *)1); return ESP_OK; }
esp_err_t httpd_register_uri_handler(httpd_handle_t server, const httpd_uri_t *route) {
    assert(server == (void *)1 && route_count < 4);
    if (fail_register) return ESP_FAIL;
    routes[route_count++] = *route; return ESP_OK;
}
static const char *request_header(httpd_req_t *req, const char *name) {
    if (!strcmp(name, "Host")) return req->host;
    if (!strcmp(name, "Origin")) return req->origin;
    if (!strcmp(name, "X-LLM-Token")) return req->token;
    if (!strcmp(name, "Content-Type")) return req->content_type;
    assert(false); return NULL;
}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req, const char *name, char *out, size_t capacity) {
    const char *value = request_header(req, name);
    if (!value) return ESP_ERR_NOT_FOUND;
    if (strlen(value) >= capacity) return ESP_ERR_INVALID_SIZE;
    strcpy(out, value); return ESP_OK;
}
size_t httpd_req_get_hdr_value_len(httpd_req_t *req, const char *name) {
    const char *value = request_header(req, name); return value ? strlen(value) : 0;
}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *req, char *out, size_t capacity) {
    if (!req->query) return ESP_ERR_NOT_FOUND;
    if (strlen(req->query) >= capacity) return ESP_ERR_INVALID_SIZE;
    strcpy(out, req->query); return ESP_OK;
}
int httpd_req_recv(httpd_req_t *req, char *out, size_t capacity) {
    if (receive_timeout) return -1;
    if (slow_body) clock_us += 6000000;
    size_t chunk = capacity > 7 ? 7 : capacity;
    memcpy(out, req->body + req->offset, chunk); req->offset += chunk; return (int)chunk;
}
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *name, const char *value) {
    assert(strstr(name, "Access-Control") == NULL);
    if (!strcmp(name, "Cache-Control")) { assert(strstr(value, "no-store")); req->security_headers |= 1; }
    if (!strcmp(name, "Referrer-Policy")) { assert(!strcmp(value, "no-referrer")); req->security_headers |= 2; }
    if (!strcmp(name, "Content-Security-Policy")) { assert(strstr(value, "frame-ancestors 'none'")); req->security_headers |= 4; }
    return ESP_OK;
}
esp_err_t httpd_resp_set_status(httpd_req_t *req, const char *status) { req->status = status; return ESP_OK; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type) { (void)req; assert(type); return ESP_OK; }
esp_err_t httpd_resp_send(httpd_req_t *req, const char *body, ssize_t size) {
    assert(size >= 0 && (size_t)size < sizeof(req->response));
    memcpy(req->response, body, (size_t)size); req->response[size] = 0; return ESP_OK;
}
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body) { return httpd_resp_send(req, body, (ssize_t)strlen(body)); }
static httpd_req_t request(void) {
    httpd_req_t req = {.host = "192.168.1.30", .token = session_token, .content_type = "application/json"};
    return req;
}
static void invoke(httpd_req_t *req, const char *uri, int method, const char *status) {
    for (size_t i = 0; i < route_count; ++i) {
        if (routes[i].method == method && !strcmp(routes[i].uri, uri)) {
            assert(routes[i].handler(req) == (status ? ESP_FAIL : ESP_OK));
            if (status) assert(req->status && !strcmp(req->status, status));
            else assert(req->status == NULL);
            assert(req->security_headers == 7);
            return;
        }
    }
    assert(false);
}
static void post(const char *body, const char *status) {
    httpd_req_t req = request(); req.body = body; req.content_len = strlen(body);
    invoke(&req, "/api/settings", HTTP_POST, status);
    assert(!strstr(req.response, "fixture-chat-key") && !strstr(req.response, "fixture-asr-key"));
}
#define FIELDS "\"enabled\":true,\"chat_url\":\"https://api.openai.com/v1/chat/completions\",\"chat_model\":\"fixture-model\",\"asr_url\":\"https://api.openai.com/v1/audio/transcriptions\",\"asr_model\":\"fixture-asr\",\"asr_language\":\"zh\",\"asr_upload\":\"auto\""
#define FIELDS_NO_UPLOAD "\"enabled\":true,\"chat_url\":\"https://api.openai.com/v1/chat/completions\",\"chat_model\":\"fixture-model\",\"asr_url\":\"https://api.openai.com/v1/audio/transcriptions\",\"asr_model\":\"fixture-asr\""
int main(void) {
    fixture_reset();
    provisioning = true; assert(llm_portal_start() == ESP_ERR_INVALID_STATE);
    provisioning = false; connected = false; assert(llm_portal_start() == ESP_ERR_INVALID_STATE);
    connected = true; fail_start = 1; assert(llm_portal_start() == ESP_FAIL && !llm_portal_url()[0]);
    fail_start = 0; fail_register = 1; assert(llm_portal_start() == ESP_FAIL && !llm_portal_is_running());
    fail_register = 0; assert(llm_portal_start() == ESP_OK && llm_portal_is_running());
    assert(llm_portal_start() == ESP_OK);
    const char *url_token = strstr(llm_portal_url(), "?token="); assert(url_token && strlen(url_token + 7) == 32);
    strcpy(session_token, url_token + 7);
    httpd_req_t req = request(); invoke(&req, "/api/settings", HTTP_GET, NULL);
    assert(strstr(req.response, "\"api_key_set\":false") && strstr(req.response, "\"asr_upload\":\"auto\"") &&
           strstr(req.response, "\"asr_language\":\"zh\"") &&
           !strstr(req.response, "\"api_key\":"));
    req = request(); req.token = NULL; invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden");
    req = request(); req.token = "00000000000000000000000000000000"; invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden");
    req = request(); req.host = "attacker.example.com"; invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden");
    req = request(); req.origin = "http://attacker.example.com"; invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden");
    req = request(); req.origin = "null"; invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden");
    req = request(); req.origin = "http://192.168.1.30"; invoke(&req, "/api/settings", HTTP_GET, NULL);
    req = request(); req.host = "192.168.1.30:80"; req.origin = "http://192.168.1.30:80"; invoke(&req, "/api/settings", HTTP_GET, NULL);
    req = request(); invoke(&req, "/", HTTP_GET, "403 Forbidden");
    char query[48]; snprintf(query, sizeof(query), "token=%s", session_token);
    req = request(); req.query = query; invoke(&req, "/", HTTP_GET, NULL);
    assert(!strcmp(req.response, "<html>fixture</html>"));
    strcat(query, "&x=1"); req = request(); req.query = query; invoke(&req, "/", HTTP_GET, "403 Forbidden");
    post("{" FIELDS ",\"api_key\":\"fixture-chat-key\",\"asr_key\":\"fixture-asr-key\"}", NULL);
    llm_settings_t settings;
    assert(llm_settings_load(&settings) == ESP_OK && settings.enabled && !strcmp(settings.api_key, "fixture-chat-key"));
    post("{" FIELDS ",\"api_key\":\"\",\"asr_key\":\"\"}", NULL);
    assert(llm_settings_load(&settings) == ESP_OK && !strcmp(settings.api_key, "fixture-chat-key"));
    req = request(); invoke(&req, "/api/settings", HTTP_GET, NULL);
    assert(strstr(req.response, "\"api_key_set\":true") && !strstr(req.response, "fixture-chat-key"));
    post("{" FIELDS ",\"api_key\":\"\",\"clear_api_key\":true}", NULL);
    assert(llm_settings_load(&settings) == ESP_OK && !settings.api_key[0] && settings.asr_key[0]);
    post("{" FIELDS ",\"api_key\":\"bad\",\"clear_api_key\":true}", "400 Bad Request");
    post("{" FIELDS ",\"unknown\":true}", "400 Bad Request");
    post("{" FIELDS ",\"enabled\":false}", "400 Bad Request");
    post("{" FIELDS ",\"api_key\":\"bad\\u0000key\"}", "400 Bad Request");
    post("{" FIELDS ",\"api_key\":\"bad\\r\\nkey\"}", "400 Bad Request");
    post("{" FIELDS ",\"api_key\":[]}", "400 Bad Request");
    post("{" FIELDS_NO_UPLOAD ",\"asr_upload\":\"invalid\"}", "400 Bad Request");
    post("{" FIELDS_NO_UPLOAD ",\"asr_language\":\"zh Chinese\"}", "400 Bad Request");
    post("{\"nested\":{\"nested\":{}}}", "400 Bad Request");
    post("[]", "400 Bad Request"); post("{} trailing", "400 Bad Request");
    req = request(); req.content_type = "text/plain"; invoke(&req, "/api/settings", HTTP_POST, "415 Unsupported Media Type");
    req = request(); req.content_len = 4097; invoke(&req, "/api/settings", HTTP_POST, "413 Payload Too Large");
    req = request(); invoke(&req, "/api/settings", HTTP_POST, "413 Payload Too Large");
    req = request(); req.content_len = 1000000; invoke(&req, "/api/settings", HTTP_GET, "400 Bad Request");
    receive_timeout = true; post("{" FIELDS "}", "400 Bad Request"); receive_timeout = false;
    slow_body = true; post("{" FIELDS "}", "400 Bad Request"); slow_body = false;
    fail_commit = ESP_FAIL; post("{" FIELDS "}", "500 Internal Server Error"); fail_commit = 0;
    /* Browser-only subscription presets use the same strict wire schema and
     * store both independent gateway credentials, never an account token. */
    post("{\"enabled\":true,\"chat_url\":\"http://192.168.1.10:8000/v1/chat/completions\","
         "\"chat_model\":\"codex-default\",\"api_key\":\"fixture-gateway-key\",\"clear_api_key\":false,"
         "\"asr_url\":\"http://192.168.1.10:8000/v1/audio/transcriptions\",\"asr_model\":\"codex-voice\","
         "\"asr_language\":\"zh\",\"asr_upload\":\"auto\",\"asr_key\":\"fixture-gateway-key\",\"clear_asr_key\":false}", NULL);
    assert(llm_settings_load(&settings) == ESP_OK && !strcmp(settings.chat_model, "codex-default") &&
           !strcmp(settings.asr_model, "codex-voice") && !strcmp(settings.api_key, "fixture-gateway-key") &&
           !strcmp(settings.asr_key, "fixture-gateway-key"));
    req = request(); invoke(&req, "/api/settings", HTTP_GET, NULL);
    assert(strstr(req.response, "\"api_key_set\":true") && strstr(req.response, "\"asr_key_set\":true") &&
           !strstr(req.response, "fixture-gateway-key"));
    post("{" FIELDS ",\"api_key\":\"\",\"asr_key\":\"\",\"clear_api_key\":true,\"clear_asr_key\":true}", NULL);
    assert(llm_settings_load(&settings) == ESP_OK && !settings.api_key[0] && !settings.asr_key[0]);
    fixture_corrupt(); req = request(); invoke(&req, "/api/settings", HTTP_GET, "500 Internal Server Error");
    req = request(); invoke(&req, "/api/settings", HTTP_DELETE, NULL);
    assert(llm_settings_load(&settings) == ESP_OK && !settings.enabled && !settings.asr_key[0]);
    connected = false; req = request(); invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden"); connected = true;
    strcpy(ip, "192.168.1.31"); req = request(); invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden"); strcpy(ip, "192.168.1.30");
    llm_portal_stop(); assert(!llm_portal_is_running() && !llm_portal_url()[0]);
    assert(llm_portal_start() == ESP_OK && !strstr(llm_portal_url(), session_token));
    req = request(); invoke(&req, "/api/settings", HTTP_GET, "403 Forbidden");
    llm_portal_stop();
    puts("LLM HTTP handlers: session/CSRF/Host guards, key redaction/preserve/clear, bounded JSON, NVS failures: PASS");
}
