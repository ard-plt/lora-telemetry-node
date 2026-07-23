#pragma once
#include <Arduino.h>

// ============================================================================
// TX ve RX'in AYNI görsel dille eşleşme durumunu göstermesi için ortak, tek
// LED durum makinesi: eşleşmemişken ~300ms periyotla yanıp söner, eşleşince
// sabit yanar. Her iki node da "eşleşik" tanımını KENDİ verisinden çıkarır
// (RX: LinkStatsTracker'ın son başarılı paket zamanı, TX: son alınan
// SYNC_TYPE_PING zamanı — bkz. common/sync_command.h) ama LED'i nasıl
// yaktıklarının/söndürdüklerinin mantığı burada TEK yerde, tekrarsız.
//
// Non-blocking: delay() kullanmaz, millis() tabanlı bir toggle zamanlayıcısı
// tutar; loop()'ta HER turda update(paired) çağrılmalıdır.
// ============================================================================

#define LINK_LED_BLINK_PERIOD_MS 300UL   // araniyorken yanip-sonme (toggle) periyodu

class LinkLed {
public:
    void begin(int pin) {
        _pin = pin;
        pinMode(_pin, OUTPUT);
    }

    // paired: son birkaç saniyede (öner: 3sn, bkz. WEB_CONFIG_PAIRED_WINDOW_MS)
    // geçerli veri (paket/PING) geldi mi.
    void update(bool paired) {
        if (paired) {
            digitalWrite(_pin, HIGH);
            _state = true; // araniyor moduna donulurse toggle'in HIGH'tan baslamasi onemli degil, tutarlilik icin
            return;
        }

        uint32_t now = millis();
        if (now - _lastToggleMs >= LINK_LED_BLINK_PERIOD_MS) {
            _lastToggleMs = now;
            _state = !_state;
            digitalWrite(_pin, _state ? HIGH : LOW);
        }
    }

private:
    int      _pin          = -1;
    bool     _state        = false;
    uint32_t _lastToggleMs = 0;
};
