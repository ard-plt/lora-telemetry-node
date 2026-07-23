#include "crypto.h"
#include "aes_config.h"  // AES_KEY, build_nonce, nonce_to_iv — common/aes_config.h
#include "mbedtls/aes.h"
#include <string.h>

void encrypt_packet(const uint8_t* plain, uint8_t* out,
                    uint8_t seq_id, uint32_t ts_ms) {
    uint8_t nonce[8];
    build_nonce(nonce, seq_id, ts_ms);
    memcpy(out, nonce, 8);

    uint8_t iv[16];
    nonce_to_iv(nonce, iv);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, AES_KEY, 128);

    size_t  nc_off       = 0;
    uint8_t stream_block[16] = {0};
    mbedtls_aes_crypt_ctr(&ctx, PACKET_SIZE, &nc_off, iv, stream_block,
                          plain, out + 8);

    mbedtls_aes_free(&ctx);
}

void decrypt_packet(const uint8_t* in, uint8_t* plain) {
    uint8_t iv[16];
    nonce_to_iv(in, iv);

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, AES_KEY, 128);

    size_t  nc_off       = 0;
    uint8_t stream_block[16] = {0};
    mbedtls_aes_crypt_ctr(&ctx, PACKET_SIZE, &nc_off, iv, stream_block,
                          in + 8, plain);

    mbedtls_aes_free(&ctx);
}
