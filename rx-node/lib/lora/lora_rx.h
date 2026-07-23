#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "packet.h"
#include "link_stats.h"

class LoraRx {
public:
    LoraRx();

    // E22'yi yapılandırır ve UART'ı başlatır
    bool begin();

    // Başarı/kayıp/bozuk paket sayımının besleneceği ortak sayaç nesnesini bağlar.
    // setup()'ta bir kere çağrılır; update() içindeki her sonuç bu nesneye yazılır
    // (OLED ve web /status AYNI nesneyi okur — hesaplama burada tek yerde yapılır).
    void attachLinkStats(LinkStatsTracker& stats) { _stats = &stats; }

    // Yeni paket geldi mi kontrol eder; geçerliyse pkt_out'a yazar
    // Non-blocking — loop'ta her iterasyonda çağır
    bool update(TelemetryPacket* pkt_out);

    // Periyodik RSSI sorgusu — non-blocking, kendi içinde 1-2sn aralığını ve kısa
    // timeout'unu yönetir. loop()'ta HER iterasyonda çağrılmalı (delay() kullanmaz).
    // Hat meşgulse (paket akışı sırasında) sorguyu atlar, mevcut telemetri
    // dinlemeyi asla bloklamaz — bkz. lora_rx.cpp üstündeki yorum.
    void pollRssi();

    bool    hasRssi()        const { return _rssiValid; }
    int16_t getLastRssiDbm() const { return _lastRssiDbm; }

    uint32_t getErrCount()  const { return _err_count; }

private:
    HardwareSerial&    _serial;
    uint32_t           _err_count;
    LinkStatsTracker*  _stats = nullptr;

    bool _configure();
    bool _wait_aux(uint32_t timeout_ms = 1000);

    // RSSI sorgu state machine
    enum RssiQueryState { RSSI_IDLE, RSSI_WAITING };
    RssiQueryState _rssiState        = RSSI_IDLE;
    uint32_t       _lastRssiQueryMs  = 0;
    uint32_t       _rssiWaitStartMs  = 0;
    bool           _rssiValid        = false;
    int16_t        _lastRssiDbm      = 0;
};
