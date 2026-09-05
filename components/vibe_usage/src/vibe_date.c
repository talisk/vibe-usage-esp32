#include "vibe_date.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

vibe_error_t vibe_date_api_bounds(int32_t date_key, vibe_timezone_t timezone,
                                  char from[25], char to[25]) {
    if (from == NULL || to == NULL || !vibe_date_is_valid(date_key) ||
        (timezone != VIBE_TZ_ASIA_SHANGHAI && timezone != VIBE_TZ_UTC)) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    int32_t start_key = date_key;
    if (timezone == VIBE_TZ_ASIA_SHANGHAI) {
        const vibe_error_t error = vibe_date_add_days(date_key, -1, &start_key);
        if (error != VIBE_OK) return error;
    }
    char start_date[11], end_date[11];
    vibe_date_format_iso(start_key, start_date);
    vibe_date_format_iso(date_key, end_date);
    snprintf(from, 25, "%sT%s:00:00.000Z", start_date,
             timezone == VIBE_TZ_ASIA_SHANGHAI ? "16" : "00");
    snprintf(to, 25, "%sT%s:59:59.999Z", end_date,
             timezone == VIBE_TZ_ASIA_SHANGHAI ? "15" : "23");
    return VIBE_OK;
}

static int days_in_month(int year, int month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap(year)) return 29;
    return month >= 1 && month <= 12 ? days[month - 1] : 0;
}

static bool split_date(int32_t key, int *year, int *month, int *day) {
    if (key <= 0 || year == NULL || month == NULL || day == NULL) return false;
    *year = key / 10000;
    *month = (key / 100) % 100;
    *day = key % 100;
    return *year >= 1 && *year <= 9999 && *month >= 1 && *month <= 12 &&
           *day >= 1 && *day <= days_in_month(*year, *month);
}

/* Howard Hinnant's civil calendar conversion, shifted to Unix epoch. */
static int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year =
        (153U * (month + (month > 2 ? (unsigned)-3 : 9U)) + 2U) / 5U +
        day - 1U;
    const unsigned day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U +
        day_of_year;
    return (int64_t)era * 146097LL + (int64_t)day_of_era - 719468LL;
}

static void civil_from_days(int64_t days, int *year, unsigned *month,
                            unsigned *day) {
    days += 719468LL;
    const int64_t era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    const unsigned day_of_era = (unsigned)(days - era * 146097LL);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
         day_of_era / 146096U) /
        365U;
    int result_year = (int)year_of_era + (int)(era * 400LL);
    const unsigned day_of_year =
        day_of_era - (365U * year_of_era + year_of_era / 4U -
                      year_of_era / 100U);
    const unsigned month_prime = (5U * day_of_year + 2U) / 153U;
    *day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    *month = month_prime + (month_prime < 10U ? 3U : (unsigned)-9);
    result_year += *month <= 2U;
    *year = result_year;
}

static int64_t floor_div(int64_t value, int64_t divisor) {
    int64_t quotient = value / divisor;
    int64_t remainder = value % divisor;
    if (remainder < 0) --quotient;
    return quotient;
}

static bool parse_digits(const char *text, size_t offset, size_t count,
                         int *value) {
    int result = 0;
    if (text == NULL || value == NULL) return false;
    for (size_t i = 0; i < count; ++i) {
        const char c = text[offset + i];
        if (c < '0' || c > '9') return false;
        result = result * 10 + (c - '0');
    }
    *value = result;
    return true;
}

bool vibe_date_is_valid(int32_t date_key) {
    int year, month, day;
    return split_date(date_key, &year, &month, &day);
}

vibe_error_t vibe_date_add_days(int32_t date_key, int delta_days,
                                int32_t *result) {
    int year, month, day;
    if (result == NULL || !split_date(date_key, &year, &month, &day)) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    const int64_t serial = days_from_civil(year, (unsigned)month,
                                           (unsigned)day) + delta_days;
    unsigned new_month, new_day;
    civil_from_days(serial, &year, &new_month, &new_day);
    if (year < 1 || year > 9999) return VIBE_ERR_OUT_OF_RANGE;
    *result = (int32_t)(year * 10000 + (int)new_month * 100 + (int)new_day);
    return VIBE_OK;
}

vibe_error_t vibe_date_from_epoch(int64_t epoch_seconds,
                                  vibe_timezone_t timezone,
                                  int32_t *date_key) {
    if (date_key == NULL || (timezone != VIBE_TZ_ASIA_SHANGHAI &&
                             timezone != VIBE_TZ_UTC)) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    const int64_t offset = timezone == VIBE_TZ_ASIA_SHANGHAI ? 8LL * 3600LL : 0;
    if (epoch_seconds > INT64_MAX - offset) return VIBE_ERR_OVERFLOW;
    const int64_t local_seconds = epoch_seconds + offset;
    const int64_t serial = floor_div(local_seconds, 86400LL);
    int year;
    unsigned month, day;
    civil_from_days(serial, &year, &month, &day);
    if (year < 1 || year > 9999) return VIBE_ERR_OUT_OF_RANGE;
    *date_key = (int32_t)(year * 10000 + (int)month * 100 + (int)day);
    return VIBE_OK;
}

vibe_error_t vibe_iso8601_to_epoch(const char *text, int64_t *epoch_seconds) {
    if (text == NULL || epoch_seconds == NULL) return VIBE_ERR_INVALID_ARGUMENT;
    const size_t length = strlen(text);
    if (length < 20 || length > 64 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != 't' && text[10] != ' ') ||
        text[13] != ':' || text[16] != ':') {
        return VIBE_ERR_SCHEMA;
    }

    int year, month, day, hour, minute, second;
    if (!parse_digits(text, 0, 4, &year) ||
        !parse_digits(text, 5, 2, &month) ||
        !parse_digits(text, 8, 2, &day) ||
        !parse_digits(text, 11, 2, &hour) ||
        !parse_digits(text, 14, 2, &minute) ||
        !parse_digits(text, 17, 2, &second) || year < 1 || year > 9999 ||
        month < 1 || month > 12 || day < 1 ||
        day > days_in_month(year, month) || hour > 23 || minute > 59 ||
        second > 60) {
        return VIBE_ERR_SCHEMA;
    }

    size_t cursor = 19;
    if (cursor < length && text[cursor] == '.') {
        ++cursor;
        const size_t fraction_start = cursor;
        while (cursor < length && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
        }
        if (cursor == fraction_start) return VIBE_ERR_SCHEMA;
    }

    int offset_seconds = 0;
    if (cursor < length && (text[cursor] == 'Z' || text[cursor] == 'z')) {
        ++cursor;
    } else if (cursor + 6 == length &&
               (text[cursor] == '+' || text[cursor] == '-') &&
               text[cursor + 3] == ':') {
        int offset_hour, offset_minute;
        if (!parse_digits(text, cursor + 1, 2, &offset_hour) ||
            !parse_digits(text, cursor + 4, 2, &offset_minute) ||
            offset_hour > 23 || offset_minute > 59) {
            return VIBE_ERR_SCHEMA;
        }
        offset_seconds = offset_hour * 3600 + offset_minute * 60;
        if (text[cursor] == '-') offset_seconds = -offset_seconds;
        cursor += 6;
    } else {
        return VIBE_ERR_SCHEMA;
    }
    if (cursor != length) return VIBE_ERR_SCHEMA;

    const int64_t serial = days_from_civil(year, (unsigned)month, (unsigned)day);
    const int64_t seconds = serial * 86400LL + hour * 3600LL + minute * 60LL +
                            (second == 60 ? 59 : second);
    *epoch_seconds = seconds - offset_seconds;
    return VIBE_OK;
}

vibe_error_t vibe_iso8601_to_date(const char *text,
                                  vibe_timezone_t timezone,
                                  int32_t *date_key) {
    int64_t epoch;
    const vibe_error_t error = vibe_iso8601_to_epoch(text, &epoch);
    if (error != VIBE_OK) return error;
    return vibe_date_from_epoch(epoch, timezone, date_key);
}

vibe_error_t vibe_date_format_iso(int32_t date_key, char output[11]) {
    int year, month, day;
    if (output == NULL || !split_date(date_key, &year, &month, &day)) {
        return VIBE_ERR_INVALID_ARGUMENT;
    }
    output[0] = (char)('0' + year / 1000);
    output[1] = (char)('0' + (year / 100) % 10);
    output[2] = (char)('0' + (year / 10) % 10);
    output[3] = (char)('0' + year % 10);
    output[4] = '-';
    output[5] = (char)('0' + month / 10);
    output[6] = (char)('0' + month % 10);
    output[7] = '-';
    output[8] = (char)('0' + day / 10);
    output[9] = (char)('0' + day % 10);
    output[10] = '\0';
    return VIBE_OK;
}
