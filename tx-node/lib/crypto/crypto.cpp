#include "crypto.h"
#include "aes_config.h"  // AES_KEY, build_nonce, nonce_to_iv — common/aes_config.h
#include "mbedtls/aes.h"
#include <string.h>

// AES-128-CTR: 40 byte plaintext → 48 byte çıkış [nonce8 | cipher40]
// CTR modu padding gerektirmez — boyut değişmez.
void encrypt_packet(const uint8_t* plain, uint8_t* out,
                    uint8_t seq_id, uint32_t ts_ms) {
    // Nonce oluştur ve çıkışın başına yaz (plaintext — gizli olmasına gerek yok)
    uint8_t nonce[8];
    build_nonce(nonce, seq_id, ts_ms);
    memcpy(out, nonce, 8);

    // IV: nonce (8 byte) + 8 byte sıfır = 16 byte
    uint8_t iv[16];
    nonce_to_iv(nonce, iv);

    // AES-128-CTR encrypt
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, AES_KEY, 128);

    size_t  nc_off       = 0;
    uint8_t stream_block[16] = {0};
    mbedtls_aes_crypt_ctr(&ctx, PACKET_SIZE, &nc_off, iv, stream_block,
                          plain, out + 8);

    mbedtls_aes_free(&ctx);
}

// AES-128-CTR: 48 byte giriş [nonce8 | cipher40] → 40 byte plaintext
// CTR modda şifre çözme = şifreleme ile aynı işlem
void decrypt_packet(const uint8_t* in, uint8_t* plain) {
    // Nonce'u ilk 8 byte'tan oku
    uint8_t iv[16];
    nonce_to_iv(in, iv);   // in[0..7] = nonce

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, AES_KEY, 128);  // CTR'da encrypt key kullanılır

    size_t  nc_off       = 0;
    uint8_t stream_block[16] = {0};
    mbedtls_aes_crypt_ctr(&ctx, PACKET_SIZE, &nc_off, iv, stream_block,
                          in + 8, plain);         // in+8 = ciphertext başlangıcı

    mbedtls_aes_free(&ctx);
}
