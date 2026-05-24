#pragma once
#include <stdint.h>
#include "packet.h"   // PACKET_SIZE, ENCRYPTED_SIZE

// 40 byte plaintext → 48 byte şifreli çıkış: [nonce8 | ciphertext40]
void encrypt_packet(const uint8_t* plain, uint8_t* out,
                    uint8_t seq_id, uint32_t ts_ms);

// 48 byte şifreli giriş → 40 byte plaintext
void decrypt_packet(const uint8_t* in, uint8_t* plain);
