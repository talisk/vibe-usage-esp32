#include "llm_endpoint.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool suffix(const char *host, size_t length, const char *tail) {
    size_t n = strlen(tail);
    if (length < n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = host[length - n + i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != tail[i]) return false;
    }
    return true;
}
static bool ipv4(const char *host, size_t length, uint8_t octets[4]) {
    size_t cursor = 0;
    for (size_t i = 0; i < 4; ++i) {
        size_t start = cursor;
        unsigned value = 0;
        while (cursor < length && digit(host[cursor])) {
            value = value * 10 + (unsigned)(host[cursor++] - '0');
            if (cursor - start > 3 || value > 255) return false;
        }
        if (cursor == start || (cursor - start > 1 && host[start] == '0')) return false;
        octets[i] = (uint8_t)value;
        if (i < 3 && (cursor >= length || host[cursor++] != '.')) return false;
    }
    return cursor == length;
}
static bool private_ipv4(const uint8_t ip[4]) {
    return ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
           (ip[0] == 192 && ip[1] == 168);
}
static bool public_ipv4(const uint8_t ip[4]) {
    if (private_ipv4(ip) || ip[0] == 0 || ip[0] == 127 || ip[0] >= 224 ||
        (ip[0] == 100 && ip[1] >= 64 && ip[1] <= 127) ||
        (ip[0] == 169 && ip[1] == 254) ||
        (ip[0] == 192 && ip[1] == 0 && (ip[2] == 0 || ip[2] == 2)) ||
        (ip[0] == 198 && (ip[1] == 18 || ip[1] == 19 || (ip[1] == 51 && ip[2] == 100))) ||
        (ip[0] == 203 && ip[1] == 0 && ip[2] == 113)) return false;
    return true;
}
static bool dns_name(const char *host, size_t length) {
    if (length < 3 || length > 253 || !alpha(host[length - 1]) ||
        suffix(host, length, ".local") || suffix(host, length, ".localhost") ||
        suffix(host, length, ".internal") || suffix(host, length, ".invalid")) return false;
    size_t label = 0;
    bool dot = false;
    for (size_t i = 0; i < length; ++i) {
        char c = host[i];
        if (c == '.') {
            if (!label || host[i - 1] == '-') return false;
            dot = true; label = 0;
        } else {
            if (!alpha(c) && !digit(c) && c != '-') return false;
            if ((!label && c == '-') || ++label > 63) return false;
        }
    }
    return dot && label && host[length - 1] != '-';
}
static int hex(char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool llm_endpoint_valid(const char *url) {
    if (!url) return false;
    size_t length = 0;
    while (length < 256 && url[length]) ++length;
    if (length == 256) return false;
    bool secure = strncmp(url, "https://", 8) == 0;
    size_t start = secure ? 8 : 7;
    if (!secure && strncmp(url, "http://", 7) != 0) return false;
    for (size_t i = start; i < length; ++i) {
        unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c >= 0x7f || c == '@' || c == '#' || c == '?' || c == '\\') return false;
        if (c == '%') {
            if (i + 2 >= length || hex(url[i + 1]) < 0 || hex(url[i + 2]) < 0) return false;
            int decoded = hex(url[i + 1]) * 16 + hex(url[i + 2]);
            if (decoded <= 0x20 || decoded == 0x7f || decoded == '\\') return false;
        }
    }
    size_t end = start;
    while (end < length && url[end] != ':' && url[end] != '/') ++end;
    if (end == start || end == length) return false;
    uint8_t ip[4] = {0};
    bool is_ip = ipv4(url + start, end - start, ip);
    if (secure ? !(is_ip ? public_ipv4(ip) : dns_name(url + start, end - start)) :
                 !(is_ip && private_ipv4(ip))) return false;
    size_t path = end;
    if (url[path] == ':') {
        size_t first = ++path;
        unsigned port = 0;
        while (path < length && digit(url[path])) {
            port = port * 10 + (unsigned)(url[path++] - '0');
            if (path - first > 5 || port > 65535) return false;
        }
        if (path == first || port == 0) return false;
    }
    return path + 1 < length && url[path] == '/';
}
