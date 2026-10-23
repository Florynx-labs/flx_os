#include <bearssl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// PBKDF2-HMAC-SHA256, one 32-byte output block. Passwords stay in the
// caller's memory; all temporary key and digest state is wiped on return.
int flx_pbkdf2_sha256(const void *password, size_t password_len,
                      const void *salt, size_t salt_len,
                      uint32_t iterations, unsigned char out[32]) {
    if (!password || !salt || !out || iterations < 10000 || iterations > 2000000 || salt_len > 1024) return 0;
    br_hmac_key_context key;
    br_hmac_key_init(&key, &br_sha256_vtable, password, password_len);
    unsigned char u[32], t[32], block[4] = {0, 0, 0, 1};
    br_hmac_context h;
    br_hmac_init(&h, &key, 32);
    br_hmac_update(&h, salt, salt_len);
    br_hmac_update(&h, block, sizeof block);
    br_hmac_out(&h, u);
    memcpy(t, u, sizeof t);
    for (uint32_t i = 1; i < iterations; i++) {
        br_hmac_init(&h, &key, 32);
        br_hmac_update(&h, u, sizeof u);
        br_hmac_out(&h, u);
        for (size_t j = 0; j < sizeof t; j++) t[j] ^= u[j];
    }
    memcpy(out, t, sizeof t);
    memset(&key, 0, sizeof key); memset(&h, 0, sizeof h);
    memset(u, 0, sizeof u); memset(t, 0, sizeof t);
    return 1;
}
