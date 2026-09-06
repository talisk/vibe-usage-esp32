#include "board_ndef.h"
#include <string.h>

typedef struct { uint8_t *out; size_t capacity, length; bool valid; } writer_t;
static void append(writer_t *w, const void *data, size_t length) {
    if (!w->valid || length > w->capacity - w->length) { w->valid = false; return; }
    if (length) memcpy(w->out + w->length, data, length);
    w->length += length;
}
static void byte(writer_t *w, uint8_t b) { append(w, &b, 1); }
static void be16(writer_t *w, uint16_t n) { byte(w, n >> 8); byte(w, n & 255); }
static void attr(writer_t *w, uint16_t id, const void *value, size_t length) {
    be16(w, id); be16(w, (uint16_t)length); append(w, value, length);
}
static bool valid_url(const char *url, size_t *length) {
    if (!url) return false;
    *length = strnlen(url, 193);
    if (!*length || *length > 192 ||
        (strncmp(url, "http://", 7) && strncmp(url, "https://", 8))) return false;
    const size_t prefix = !strncmp(url, "https://", 8) ? 8 : 7;
    if (*length <= prefix || url[prefix] == '/' || url[prefix] == '?') return false;
    for (size_t i = 0; i < *length; ++i)
        if ((unsigned char)url[i] <= 32 || (unsigned char)url[i] == 127) return false;
    return true;
}
static void uri_record(writer_t *w, const char *url, size_t length, bool first) {
    bool tls = !strncmp(url, "https://", 8);
    size_t prefix = tls ? 8 : 7;
    byte(w, first ? 0xd1 : 0x51); // ME | SR | well-known, optional MB.
    byte(w, 1); byte(w, (uint8_t)(length - prefix + 1));
    byte(w, 'U'); byte(w, tls ? 4 : 3); append(w, url + prefix, length - prefix);
}
bool board_ndef_uri(const char *url, uint8_t *out, size_t capacity, size_t *written) {
    size_t length = 0;
    if (written) *written = 0;
    if (!out || !written || !valid_url(url, &length)) return false;
    writer_t w = {out, capacity, 0, true};
    uri_record(&w, url, length, true);
    if (w.valid) *written = w.length;
    return w.valid;
}
bool board_ndef_wifi(const char *ssid, const char *password, const char *url,
                     uint8_t *out, size_t capacity, size_t *written) {
    if (written) *written = 0;
    size_t url_length = 0;
    if (!ssid || !password || !out || !written || !valid_url(url, &url_length)) return false;
    const size_t ssid_length = strnlen(ssid, 33), key_length = strnlen(password, 64);
    if (!ssid_length || ssid_length > 32 || key_length > 63 ||
        (key_length && key_length < 8)) return false;
    uint8_t credential[144];
    writer_t c = {credential, sizeof(credential), 0, true};
    const uint8_t network = 1, version = 0x10;
    const uint8_t auth[] = {0, key_length ? 0x20 : 0x01}; // WPA2PSK / Open
    const uint8_t encryption[] = {0, key_length ? 0x08 : 0x01}; // AES / None
    const uint8_t mac[] = {255, 255, 255, 255, 255, 255};
    attr(&c, 0x1026, &network, 1);
    attr(&c, 0x1045, ssid, ssid_length);
    attr(&c, 0x1003, auth, sizeof(auth));
    attr(&c, 0x100f, encryption, sizeof(encryption));
    attr(&c, 0x1027, password, key_length);
    attr(&c, 0x1020, mac, sizeof(mac));
    if (!c.valid) return false;
    static const char mime[] = "application/vnd.wfa.wsc";
    writer_t w = {out, capacity, 0, true};
    byte(&w, 0x92); // MB | SR | MIME, URI is the second and final record.
    byte(&w, sizeof(mime) - 1); byte(&w, (uint8_t)(5 + 4 + c.length));
    append(&w, mime, sizeof(mime) - 1);
    attr(&w, 0x104a, &version, 1);
    attr(&w, 0x100e, credential, c.length);
    uri_record(&w, url, url_length, false);
    // Do not retain the copied Wi-Fi key after encoding.
    volatile uint8_t *clear = credential;
    for (size_t i = 0; i < sizeof(credential); ++i) clear[i] = 0;
    if (w.valid) *written = w.length;
    return w.valid;
}
bool board_ndef_type2(const uint8_t *message, size_t length, uint8_t *out,
                      size_t capacity, size_t *written) {
    if (written) *written = 0;
    if (!message || !length || length > 65535 || !out || !written) return false;
    writer_t w = {out, capacity, 0, true};
    byte(&w, 3);
    if (length <= 254) byte(&w, (uint8_t)length);
    else { byte(&w, 255); be16(&w, (uint16_t)length); }
    append(&w, message, length); byte(&w, 0xfe);
    if (w.valid) *written = w.length;
    return w.valid;
}
