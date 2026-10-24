#pragma once
#include <stddef.h>

// Local request/reply protocol to the root-owned Rust FLX auth service.
int flx_auth_request(const char *request, char *response, size_t response_size);
int flx_auth_status(void); // -1 unavailable, 0 first-time setup, 1 enrolled
int flx_auth_status_user(const char *name);
