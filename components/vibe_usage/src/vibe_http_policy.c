#include "vibe_http_policy.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "vibe_date.h"

static const char *skip_ows(const char *text) {
    while (*text == ' ' || *text == '\t') ++text;
    return text;
}

bool vibe_http_content_type_is_json(const char *value) {
    static const char expected[] = "application/json";
    if (value == NULL) return false;
    value = skip_ows(value);
    if (strncasecmp(value, expected, sizeof(expected) - 1U) != 0) return false;
    value = skip_ows(value + sizeof(expected) - 1U);
    return *value == '\0' || *value == ';';
}

static int month_number(const char text[3]) {
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int month = 0; month < 12; ++month) {
        if (strncasecmp(text, months[month], 3) == 0) return month + 1;
    }
    return 0;
}

static bool valid_weekday(const char text[3]) {
    static const char *const weekdays[] = {
        "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
    };
    for (size_t index = 0; index < sizeof(weekdays) / sizeof(weekdays[0]);
         ++index) {
        if (strncasecmp(text, weekdays[index], 3) == 0) return true;
    }
    return false;
}

static bool digits_at(const char *text, size_t offset, size_t count) {
    for (size_t index = 0; index < count; ++index) {
        if (text[offset + index] < '0' || text[offset + index] > '9') {
            return false;
        }
    }
    return true;
}

static bool parse_imf_fixdate(const char *value, int64_t *epoch) {
    if (strlen(value) != 29U || value[3] != ',' || value[4] != ' ' ||
        value[7] != ' ' || value[11] != ' ' || value[16] != ' ' ||
        value[19] != ':' || value[22] != ':' || value[25] != ' ' ||
        strcasecmp(value + 26, "GMT") != 0 || !valid_weekday(value) ||
        !digits_at(value, 5, 2) || !digits_at(value, 12, 4) ||
        !digits_at(value, 17, 2) || !digits_at(value, 20, 2) ||
        !digits_at(value, 23, 2)) {
        return false;
    }
    unsigned day = 0;
    unsigned year = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
    if (sscanf(value + 5, "%2u", &day) != 1 ||
        sscanf(value + 12, "%4u", &year) != 1 ||
        sscanf(value + 17, "%2u", &hour) != 1 ||
        sscanf(value + 20, "%2u", &minute) != 1 ||
        sscanf(value + 23, "%2u", &second) != 1) {
        return false;
    }
    const int month = month_number(value + 8);
    if (month == 0 || year > 9999U || day == 0 || day > 31U || hour > 23U ||
        minute > 59U || second > 59U) {
        return false;
    }
    char iso[21];
    const int written = snprintf(iso, sizeof(iso), "%04u-%02d-%02uT%02u:%02u:%02uZ",
                                 year, month, day, hour, minute, second);
    return written == 20 && vibe_iso8601_to_epoch(iso, epoch) == VIBE_OK;
}

bool vibe_http_parse_retry_after(const char *value, int64_t now_utc,
                                 uint32_t *delay_seconds) {
    if (value == NULL || delay_seconds == NULL) return false;
    value = skip_ows(value);
    if (*value >= '0' && *value <= '9') {
        uint64_t parsed = 0;
        do {
            parsed = parsed * 10U + (uint64_t)(*value - '0');
            if (parsed > UINT32_MAX) return false;
            ++value;
        } while (*value >= '0' && *value <= '9');
        value = skip_ows(value);
        if (*value != '\0') return false;
        *delay_seconds = (uint32_t)parsed;
        return true;
    }

    int64_t retry_utc = 0;
    if (now_utc < 0 || !parse_imf_fixdate(value, &retry_utc)) return false;
    if (retry_utc <= now_utc) {
        *delay_seconds = 0;
        return true;
    }
    const uint64_t delta = (uint64_t)(retry_utc - now_utc);
    if (delta > UINT32_MAX) return false;
    *delay_seconds = (uint32_t)delta;
    return true;
}
