#include "vibe_usage.h"

const char *vibe_error_name(vibe_error_t error) {
    switch (error) {
        case VIBE_OK: return "OK";
        case VIBE_ERR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case VIBE_ERR_INVALID_JSON: return "INVALID_JSON";
        case VIBE_ERR_SCHEMA: return "SCHEMA";
        case VIBE_ERR_OVERFLOW: return "OVERFLOW";
        case VIBE_ERR_BODY_TOO_LARGE: return "BODY_TOO_LARGE";
        case VIBE_ERR_TOO_MANY_BUCKETS: return "TOO_MANY_BUCKETS";
        case VIBE_ERR_OUT_OF_RANGE: return "OUT_OF_RANGE";
        case VIBE_ERR_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
        case VIBE_ERR_CRC: return "CRC";
        case VIBE_ERR_VERSION: return "VERSION";
        case VIBE_ERR_CANCELLED: return "CANCELLED";
        case VIBE_ERR_NO_MEMORY: return "NO_MEMORY";
        case VIBE_ERR_STORAGE: return "STORAGE";
        case VIBE_ERR_HTTP_STATUS: return "HTTP_STATUS";
        case VIBE_ERR_DNS: return "DNS";
        case VIBE_ERR_TLS: return "TLS";
        case VIBE_ERR_NETWORK: return "NETWORK";
        case VIBE_ERR_TIME_INVALID: return "TIME_INVALID";
        default: return "UNKNOWN";
    }
}

const char *vibe_timezone_name(vibe_timezone_t timezone) {
    return timezone == VIBE_TZ_UTC ? "UTC" : "Asia/Shanghai";
}

const char *vibe_timezone_posix(vibe_timezone_t timezone) {
    return timezone == VIBE_TZ_UTC ? "UTC0" : "CST-8";
}
