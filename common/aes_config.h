#pragma once
#include <stdint.h>
#include <string.h>

// ===== AES-128 Anahtarı =====
// Her iki ESP32'ye aynı key yüklenecek — üretim öncesi değiştir!
static const uint8_t AES_KEY[16] = {
    0x2D, 0x4A, 0x6F, 0x8B, 0x1C, 0x3E, 0x5F, 0x7A,
    0x9D, 0xBE, 0xCF, 0xE0, 0x12, 0x34, 0x56, 0x78
};

// ===== Nonce Yapısı =====
// 8 byte: [seq_id (1 byte) | timestamp ms (4 byte) | padding (3 byte sıfır)]
// Her pakette farklı nonce → replay saldırısına karşı koruma

// Nonce oluştur: seq_id + millis()
inline void build_nonce(uint8_t* nonce8, uint8_t seq_id, uint32_t ts_ms) {
    nonce8[0] = seq_id;
    nonce8[1] = (uint8_t)(ts_ms);
    nonce8[2] = (uint8_t)(ts_ms >>  8);
    nonce8[3] = (uint8_t)(ts_ms >> 16);
    nonce8[4] = (uint8_t)(ts_ms >> 24);
    nonce8[5] = 0;
    nonce8[6] = 0;
    nonce8[7] = 0;
}

// Nonce (8 byte) → AES-CTR IV (16 byte): nonce + 8 byte sıfır
inline void nonce_to_iv(const uint8_t* nonce8, uint8_t* iv16) {
    memcpy(iv16, nonce8, 8);
    memset(iv16 + 8, 0, 8);
}
