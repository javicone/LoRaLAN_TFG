#pragma once
// ponytail: pseudo-AES solo para el simulador. Es un keystream deterministico
// derivado de (clave, contador) usado en modo CTR: como CTR es un XOR y ambos
// extremos usan este mismo shim, cifrar/descifrar es coherente entre nodo y
// gateway. NO es criptografia real: el AES autentico (mbedtls del ESP32) se
// valida en hardware.

#include <stdint.h>
#include <string.h>
#include <stddef.h>

typedef struct {
    unsigned char key[16];
} mbedtls_aes_context;

inline void mbedtls_aes_init(mbedtls_aes_context* ctx) { memset(ctx, 0, sizeof(*ctx)); }
inline void mbedtls_aes_free(mbedtls_aes_context* ctx) { (void)ctx; }

inline int mbedtls_aes_setkey_enc(mbedtls_aes_context* ctx, const unsigned char* key, unsigned int keybits) {
    if (keybits != 128) return -1;
    memcpy(ctx->key, key, 16);
    return 0;
}

inline int mbedtls_aes_crypt_ctr(mbedtls_aes_context* ctx,
                                 size_t length,
                                 size_t* nc_off,
                                 unsigned char nonce_counter[16],
                                 unsigned char stream_block[16],
                                 const unsigned char* input,
                                 unsigned char* output) {
    size_t off = *nc_off;
    for (size_t i = 0; i < length; ++i) {
        if (off == 0) {
            // stream_block = mezcla(clave, contador) con xorshift
            uint32_t s = 0x9E3779B9u;
            for (int k = 0; k < 16; ++k) {
                s ^= (uint32_t)(ctx->key[k] ^ nonce_counter[k]) << ((k % 4) * 8);
                s *= 2654435761u;
                s ^= s >> 15;
            }
            for (int k = 0; k < 16; ++k) {
                s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                stream_block[k] = (unsigned char)s;
            }
            // incrementar contador (big-endian), igual que mbedtls
            for (int k = 15; k >= 0; --k) {
                if (++nonce_counter[k] != 0) break;
            }
        }
        output[i] = input[i] ^ stream_block[off];
        off = (off + 1) & 0x0F;
    }
    *nc_off = off;
    return 0;
}
