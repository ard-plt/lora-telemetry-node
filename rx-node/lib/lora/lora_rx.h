#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "packet.h"

class LoraRx {
public:
    LoraRx();

    // E22'yi yapılandırır ve UART'ı başlatır
    bool begin();

    // Yeni paket geldi mi kontrol eder; geçerliyse pkt_out'a yazar
    // Non-blocking — loop'ta her iterasyonda çağır
    bool update(TelemetryPacket* pkt_out);

    // RSSI UART modunda mevcut değil — gelecekte REG3 ile eklenebilir
    float    getLastRSSI()  const { return 0.0f; }
    float    getLastSNR()   const { return 0.0f; }
    uint32_t getErrCount()  const { return _err_count; }

private:
    HardwareSerial& _serial;
    uint32_t        _err_count;

    bool _configure();
    bool _wait_aux(uint32_t timeout_ms = 1000);
};
