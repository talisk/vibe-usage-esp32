#ifndef LLM_ENDPOINT_H_
#define LLM_ENDPOINT_H_
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* HTTPS: DNS name/public IPv4. HTTP: RFC1918 IPv4 only. Full endpoint path is
 * required. No user-info, query, fragment, percent-encoded controls or redirects.
 * This is syntax/origin policy; the caller must still verify TLS and disable
 * redirects. DNS resolution is not performed here. */
bool llm_endpoint_valid(const char *url);
#ifdef __cplusplus
}
#endif
#endif
