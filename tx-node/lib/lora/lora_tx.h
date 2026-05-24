#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "packet.h"

class LoraTx {
public:
    LoraTx();

    // E22'yi yapılandırır (868 MHz, 2.4kbps) ve UART'ı başlatır
    // false → modül yanıt vermedi, yine de devam edilebilir
    bool begin();

    // Şifreli tamponu E22 üzerinden gönderir (ENCRYPTED_SIZE byte)
    bool send_raw(const uint8_t* data, size_t len);

private:
    HardwareSerial& _serial;

    // E22 register yapılandırması (program modunda)
    bool _configure();

    // AUX HIGH olana kadar bekle (timeout_ms sonra vazgeç)
    bool _wait_aux(uint32_t timeout_ms = 1000);
};
