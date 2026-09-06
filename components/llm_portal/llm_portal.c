#include "llm_portal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "wifi_adapter.h"

#define TOKEN_BYTES 16
#define TOKEN_HEX_SIZE (TOKEN_BYTES * 2 + 1)
#define MAX_CONFIG_BODY 4096

extern const unsigned char portal_html_start[] asm("_binary_portal_html_start");
extern const unsigned char portal_html_end[] asm("_binary_portal_html_end");
static httpd_handle_t server;
static char token[TOKEN_HEX_SIZE];
static char station_ip[16];
static char portal_url[96];

static void wipe(void *data, size_t size) {
    volatile uint8_t *p = data;
    while (size--) *p++ = 0;
}
static void response_headers(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; "
        "connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'");
}
static esp_err_t error_response(httpd_req_t *req, const char *status, const char *message) {
    response_headers(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Connection", "close");
    /* All caller messages are constant literals, never request text/secrets. */
    esp_err_t sent = httpd_resp_sendstr(req, message);
    /* Returning failure closes the session instead of asking IDF to drain an
     * untrusted/oversized unread request body in httpd_req_delete(). */
    return sent == ESP_OK ? ESP_FAIL : sent;
}
static bool token_matches(const char *candidate) {
    if (!token[0] || strlen(candidate) != TOKEN_HEX_SIZE - 1) return false;
    unsigned difference = 0;
    for (size_t i = 0; i < TOKEN_HEX_SIZE - 1; ++i)
        difference |= (unsigned char)candidate[i] ^ (unsigned char)token[i];
    return difference == 0;
}
static bool trusted_request(httpd_req_t *req, bool page) {
    char active_ip[16], host[24], expected_host[24], origin[40], expected_origin[40];
    if (!wifi_adapter_is_connected() || wifi_adapter_is_provisioning() ||
        wifi_adapter_station_ip(active_ip, sizeof(active_ip)) != ESP_OK ||
        strcmp(active_ip, station_ip) != 0) return false;
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
    snprintf(expected_host, sizeof(expected_host), "%s:80", station_ip);
    if (strcmp(host, station_ip) && strcmp(host, expected_host)) return false;
    if (httpd_req_get_hdr_value_len(req, "Origin")) {
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) return false;
        snprintf(expected_origin, sizeof(expected_origin), "http://%s", host);
        if (strcmp(origin, expected_origin)) return false;
    }
    char candidate[TOKEN_HEX_SIZE] = {0};
    if (page) {
        char query[48];
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            strncmp(query, "token=", 6) || strlen(query) != 6 + TOKEN_HEX_SIZE - 1) return false;
        memcpy(candidate, query + 6, TOKEN_HEX_SIZE);
    } else if (httpd_req_get_hdr_value_str(req, "X-LLM-Token", candidate,
                                           sizeof(candidate)) != ESP_OK) return false;
    bool accepted = token_matches(candidate);
    wipe(candidate, sizeof(candidate));
    return accepted;
}
static esp_err_t page_handler(httpd_req_t *req) {
    if (!trusted_request(req, true))
        return error_response(req, "403 Forbidden", "{\"error\":\"Scan the current device QR code.\"}");
    if (req->content_len != 0)
        return error_response(req, "400 Bad Request", "{\"error\":\"Page request must be empty.\"}");
    response_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)portal_html_start,
                          (ssize_t)(portal_html_end - portal_html_start - 1));
}
static esp_err_t send_settings(httpd_req_t *req, const llm_settings_t *settings) {
    cJSON *json = cJSON_CreateObject();
    bool valid = json && cJSON_AddBoolToObject(json, "enabled", settings->enabled) &&
        cJSON_AddStringToObject(json, "chat_url", settings->chat_url) &&
        cJSON_AddStringToObject(json, "chat_model", settings->chat_model) &&
        cJSON_AddBoolToObject(json, "api_key_set", settings->api_key[0] != '\0') &&
        cJSON_AddStringToObject(json, "asr_url", settings->asr_url) &&
        cJSON_AddStringToObject(json, "asr_model", settings->asr_model) &&
        cJSON_AddStringToObject(json, "asr_language", settings->asr_language) &&
        cJSON_AddStringToObject(json, "asr_upload", llm_asr_upload_name(settings->asr_upload)) &&
        cJSON_AddBoolToObject(json, "asr_key_set", settings->asr_key[0] != '\0');
    char *body = valid ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!body) return error_response(req, "503 Service Unavailable", "{\"error\":\"Not enough memory.\"}");
    response_headers(req);
    httpd_resp_set_type(req, "application/json");
    esp_err_t error = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return error;
}
static esp_err_t get_settings(httpd_req_t *req) {
    if (!trusted_request(req, false))
        return error_response(req, "403 Forbidden", "{\"error\":\"Configuration session expired. Scan again.\"}");
    if (req->content_len != 0)
        return error_response(req, "400 Bad Request", "{\"error\":\"Read request must be empty.\"}");
    llm_settings_t *settings = calloc(1, sizeof(*settings));
    if (!settings) return error_response(req, "503 Service Unavailable", "{\"error\":\"Not enough memory.\"}");
    esp_err_t error = llm_settings_load(settings);
    if (error == ESP_OK) error = send_settings(req, settings);
    else error = error_response(req, "500 Internal Server Error", "{\"error\":\"Cannot read saved settings. Reset LLM settings to recover.\"}");
    wipe(settings, sizeof(*settings));
    free(settings);
    return error;
}
static bool known_fields(const cJSON *json) {
    static const char *allowed[] = {"enabled", "chat_url", "chat_model", "api_key",
        "asr_url", "asr_model", "asr_language", "asr_upload", "asr_key",
        "clear_api_key", "clear_asr_key"};
    const cJSON *item;
    cJSON_ArrayForEach(item, json) {
        bool known = false;
        for (size_t i = 0; i < sizeof(allowed) / sizeof(*allowed); ++i)
            if (item->string && !strcmp(item->string, allowed[i])) known = true;
        if (!known) return false;
        for (const cJSON *prior = json->child; prior != item; prior = prior->next)
            if (!strcmp(prior->string, item->string)) return false;
    }
    return true;
}
static bool read_field(const cJSON *json, const char *name, char *out, size_t capacity,
                       bool key, const char *clear_name) {
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(json, name);
    const cJSON *clear = clear_name ? cJSON_GetObjectItemCaseSensitive(json, clear_name) : NULL;
    if (clear && !cJSON_IsBool(clear)) return false;
    if (field && (!cJSON_IsString(field) || !field->valuestring || strlen(field->valuestring) >= capacity)) return false;
    if (!key && (!field || !field->valuestring[0])) return false;
    if (cJSON_IsTrue(clear)) {
        if (field && field->valuestring[0]) return false;
        wipe(out, capacity);
    } else if (field && (!key || field->valuestring[0])) {
        wipe(out, capacity);
        memcpy(out, field->valuestring, strlen(field->valuestring));
    }
    return true;
}
static bool read_upload(const cJSON *json, llm_asr_upload_t *upload) {
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(json, "asr_upload");
    return !field || (cJSON_IsString(field) && llm_asr_upload_parse(field->valuestring, upload));
}
static bool read_language(const cJSON *json, char *out, size_t capacity) {
    const cJSON *field = cJSON_GetObjectItemCaseSensitive(json, "asr_language");
    if (!field) return true;
    if (!cJSON_IsString(field) || !field->valuestring ||
        strlen(field->valuestring) >= capacity) return false;
    wipe(out, capacity);
    memcpy(out, field->valuestring, strlen(field->valuestring));
    return true;
}
static void wipe_json_strings(cJSON *json) {
    if (!json) return;
    for (cJSON *item = json->child; item; item = item->next) {
        if (item->valuestring) wipe(item->valuestring, strlen(item->valuestring));
        wipe_json_strings(item);
    }
}
static bool flat_json_body(const char *body, size_t length) {
    /* This API has one object containing scalars. Enforce that before cJSON's
     * recursive parser can consume stack on a malicious deeply nested body. */
    bool quoted = false, escaped = false;
    int depth = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = body[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '[' || c == ']') return false;
        else if (c == '{' && ++depth > 1) return false;
        else if (c == '}' && --depth < 0) return false;
    }
    return !quoted && depth == 0;
}
static esp_err_t save_settings(httpd_req_t *req) {
    if (!trusted_request(req, false))
        return error_response(req, "403 Forbidden", "{\"error\":\"Configuration session expired. Scan again.\"}");
    char content_type[48];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        (strcmp(content_type, "application/json") && strcmp(content_type, "application/json; charset=utf-8")))
        return error_response(req, "415 Unsupported Media Type", "{\"error\":\"Send application/json.\"}");
    if (req->content_len == 0 || req->content_len > MAX_CONFIG_BODY)
        return error_response(req, "413 Payload Too Large", "{\"error\":\"Settings body must be 1-4096 bytes.\"}");
    char *body = calloc(req->content_len + 1, 1);
    llm_settings_t *settings = calloc(1, sizeof(*settings));
    if (!body || !settings) {
        free(body); free(settings);
        return error_response(req, "503 Service Unavailable", "{\"error\":\"Not enough memory.\"}");
    }
    size_t offset = 0;
    int64_t deadline = esp_timer_get_time() + 10000000LL;
    while (offset < req->content_len && esp_timer_get_time() < deadline) {
        int received = httpd_req_recv(req, body + offset, req->content_len - offset);
        if (received <= 0) break; /* Includes timeout; no unbounded retry loop. */
        offset += (size_t)received;
    }
    /* cJSON strings are C strings; disallow escaped NUL rather than silently
     * accepting a value different from what the browser submitted. */
    cJSON *json = offset == req->content_len && flat_json_body(body, offset) && !memchr(body, '\0', offset) &&
        !strstr(body, "\\u0000") ? cJSON_ParseWithLengthOpts(body, offset + 1, NULL, true) : NULL;
    esp_err_t loaded = llm_settings_load(settings);
    const cJSON *enabled = json ? cJSON_GetObjectItemCaseSensitive(json, "enabled") : NULL;
    bool valid = loaded == ESP_OK && cJSON_IsObject(json) && known_fields(json) &&
        cJSON_IsBool(enabled) &&
        read_field(json, "chat_url", settings->chat_url, sizeof(settings->chat_url), false, NULL) &&
        read_field(json, "chat_model", settings->chat_model, sizeof(settings->chat_model), false, NULL) &&
        read_field(json, "api_key", settings->api_key, sizeof(settings->api_key), true, "clear_api_key") &&
        read_field(json, "asr_url", settings->asr_url, sizeof(settings->asr_url), false, NULL) &&
        read_field(json, "asr_model", settings->asr_model, sizeof(settings->asr_model), false, NULL) &&
        read_language(json, settings->asr_language, sizeof(settings->asr_language)) &&
        read_upload(json, &settings->asr_upload) &&
        read_field(json, "asr_key", settings->asr_key, sizeof(settings->asr_key), true, "clear_asr_key");
    if (valid) {
        settings->enabled = cJSON_IsTrue(enabled);
        valid = llm_settings_valid(settings);
    }
    esp_err_t saved = valid ? llm_settings_save(settings) : ESP_ERR_INVALID_ARG;
    wipe_json_strings(json);
    cJSON_Delete(json);
    wipe(body, req->content_len + 1);
    free(body);
    esp_err_t response;
    if (loaded != ESP_OK || (valid && saved != ESP_OK))
        response = error_response(req, "500 Internal Server Error", "{\"error\":\"Storage failed. Settings were not confirmed saved.\"}");
    else if (!valid)
        response = error_response(req, "400 Bad Request", "{\"error\":\"Invalid settings. Use a full HTTPS endpoint or HTTP RFC1918 IPv4 endpoint; no query, fragment or credentials in URL.\"}");
    else response = send_settings(req, settings);
    wipe(settings, sizeof(*settings));
    free(settings);
    return response;
}
static esp_err_t reset_settings(httpd_req_t *req) {
    if (!trusted_request(req, false))
        return error_response(req, "403 Forbidden", "{\"error\":\"Configuration session expired. Scan again.\"}");
    if (req->content_len != 0)
        return error_response(req, "400 Bad Request", "{\"error\":\"Reset request must be empty.\"}");
    if (llm_settings_clear() != ESP_OK)
        return error_response(req, "500 Internal Server Error", "{\"error\":\"Could not reset saved settings.\"}");
    return get_settings(req);
}
esp_err_t llm_portal_start(void) {
    if (server) return ESP_OK;
    if (wifi_adapter_is_provisioning()) return ESP_ERR_INVALID_STATE;
    esp_err_t error = wifi_adapter_station_ip(station_ip, sizeof(station_ip));
    if (error != ESP_OK) return error;
    uint8_t random[TOKEN_BYTES];
    esp_fill_random(random, sizeof(random));
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(random); ++i) {
        token[i * 2] = hex[random[i] >> 4];
        token[i * 2 + 1] = hex[random[i] & 15];
    }
    token[sizeof(token) - 1] = '\0';
    wipe(random, sizeof(random));
    snprintf(portal_url, sizeof(portal_url), "http://%s/?token=%s", station_ip, token);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 6144;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    error = httpd_start(&server, &config);
    if (error != ESP_OK) { llm_portal_stop(); return error; }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = page_handler},
        {.uri = "/api/settings", .method = HTTP_GET, .handler = get_settings},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = save_settings},
        {.uri = "/api/settings", .method = HTTP_DELETE, .handler = reset_settings},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(*routes); ++i) {
        error = httpd_register_uri_handler(server, &routes[i]);
        if (error != ESP_OK) { llm_portal_stop(); return error; }
    }
    return ESP_OK;
}
void llm_portal_stop(void) {
    if (server) { httpd_stop(server); server = NULL; }
    wipe(token, sizeof(token));
    wipe(portal_url, sizeof(portal_url));
    wipe(station_ip, sizeof(station_ip));
}
const char *llm_portal_url(void) { return portal_url; }
bool llm_portal_is_running(void) { return server != NULL; }
