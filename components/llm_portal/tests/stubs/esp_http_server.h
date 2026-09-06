#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef struct {
    size_t content_len;
    const char *host, *origin, *token, *query, *content_type, *body;
    const char *status;
    char response[4096];
    size_t offset;
    unsigned security_headers;
} httpd_req_t;
typedef struct {
    unsigned server_port, stack_size, max_open_sockets, max_uri_handlers;
    bool lru_purge_enable;
    unsigned recv_wait_timeout, send_wait_timeout;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() {0}
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTP_DELETE 2
typedef struct {
    const char *uri;
    int method;
    esp_err_t (*handler)(httpd_req_t *);
} httpd_uri_t;
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *, const char *, char *, size_t);
size_t httpd_req_get_hdr_value_len(httpd_req_t *, const char *);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *, char *, size_t);
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, ssize_t);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
