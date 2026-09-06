#pragma once
void fixture_log(const char *, const char *, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGI(...) fixture_log(__VA_ARGS__)
#define ESP_LOGW(...) fixture_log(__VA_ARGS__)
