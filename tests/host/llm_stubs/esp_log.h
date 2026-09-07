#pragma once

static inline void fixture_llm_log(const char *tag, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static inline void fixture_llm_log(const char *tag, const char *format, ...) {
    (void)tag;
    (void)format;
}

#define ESP_LOGW(...) fixture_llm_log(__VA_ARGS__)
