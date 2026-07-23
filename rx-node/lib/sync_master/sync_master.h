#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include "sync_command.h"

// ============================================================================
// RX-Master mod/rate senkronizasyon durum makinesi. RX, web panelinden yeni
// bir BridgeMode/AirRate seçildiğinde kendi E22'sini HEMEN değiştirmez — önce
// TX'e (mevcut/eski profilde, link hâlâ açıkken) bir SYNC_TYPE_CMD paketi
// gönderir, TX'ten SYNC_TYPE_ACK bekler, ACK gelirse KENDİ profilini uygular.
// ACK gelmezse RX kendi modunu DEĞİŞTİRMEZ (link tutarlılığı korunur).
//
// ACK'in RX'e ulaşabilmesi için TX'in ACK'i YENİ profile geçmeden ÖNCE, hâlâ
// RX'in dinlediği ESKİ profille göndermesi gerekir (bkz. tx-node/src/main.cpp
// txHandleSyncCommand() — "önce ACK, sonra profil değiştir" sırası kasıtlıdır;
// tersi olsaydı air-rate değişen senkronizasyonlarda ACK, RX artık dinlemediği
// bir REG0/baud'da gönderilmiş olur ve HİÇBİR ZAMAN ulaşmazdı).
//
// Tek nesne olarak kullanılır (rx-node main.cpp'de global) — link_stats.h ile
// aynı desende header-only.
// ============================================================================

enum SyncState { SYNC_IDLE, SYNC_SENDING, SYNC_WAITING_ACK, SYNC_OK, SYNC_FAILED };

// requestChange() basariyla onaylandiginda (ACK dogrulandiginda) cagrilir —
// main.cpp bu callback icinde kendi applyProfile()'ini tetikler.
typedef void (*SyncApplyFn)(BridgeMode mode, AirRate rate);

class SyncMaster {
public:
    void begin(HardwareSerial& serial1, int auxPin, SyncApplyFn onApply) {
        _serial  = &serial1;
        _auxPin  = auxPin;
        _onApply = onApply;
    }

    // Web panelinden yeni mod/rate seçildiğinde çağrılır — senkronizasyonu
    // (yeniden) başlatır, önceki bir senkronizasyonun sonucunu (OK/FAILED)
    // üzerine yazar.
    void requestChange(BridgeMode mode, AirRate rate) {
        _pendingMode = mode;
        _pendingRate = rate;
        _attempts    = 0;
        _state       = SYNC_SENDING;
        _sendNext();
        Serial.printf("# [SYNC] TX'e istek gonderiliyor: Bridge=%s Rate=%s\n",
                      bridge_mode_name(mode), air_rate_name(rate));
    }

    // loop()'ta HER turda, BridgeMode'dan bagimsiz cagrilmali — tekrar gonderim
    // ve ACK timeout'unu yonetir. Serial1'i OKUMAZ (bkz. scanCompressed/
    // handleIncoming) - sadece gonderim tarafini yonetir, cunku CMD paketini
    // yazmak (Serial1.write) her iki BridgeMode'da da ayni sekilde RF'e cikar.
    void tick() {
        if (_state == SYNC_SENDING) {
            if (millis() - _lastSendMs < SYNC_RESEND_INTERVAL_MS) return;
            if (_attempts >= SYNC_SEND_REPEAT) {
                _state       = SYNC_WAITING_ACK;
                _waitStartMs = millis();
            } else {
                _sendNext();
            }
        } else if (_state == SYNC_WAITING_ACK) {
            if (millis() - _waitStartMs >= SYNC_ACK_TIMEOUT_MS) {
                _state = SYNC_FAILED;
                Serial.println("# [SYNC] TX yanit vermedi - senkronizasyon basarisiz, mod degistirilmedi");
            }
        }
    }

    // BRIDGE_COMPRESSED dalinda cagrilir. Serial1'i sync paketi icin YALNIZCA
    // aktif bir senkronizasyon bekliyorken tarar (bkz. common/sync_command.h
    // dosya-ustu risk analizi - 48-byte hizalamayi bozmama garantisi bu
    // pencerenin darligina dayanir). Not: sync_scan() yerine burada dogrudan
    // ayni mantik inline yazildi ki handler icin static/singleton bir kopruye
    // gerek kalmasin (member fonksiyonlar duz fonksiyon pointer olamaz).
    void scanCompressed() {
        if (_state != SYNC_SENDING && _state != SYNC_WAITING_ACK) return;
        if (_serial->available() < (int)SYNC_PACKET_SIZE) return;
        if (_serial->peek() != SYNC_MAGIC_0) return;

        uint8_t buf[SYNC_PACKET_SIZE];
        _serial->readBytes(buf, SYNC_PACKET_SIZE);

        SyncPacketType type; BridgeMode mode; AirRate rate;
        if (sync_parse_packet(buf, type, mode, rate)) {
            handleIncoming(type, mode, rate);
        }
        // Dogrulama basarisizsa (yanlis pozitif) bu byte'lar tuketilmis olur -
        // bkz. sync_command.h'deki risk analizi: bu sadece aktif senkronizasyon
        // penceresinde calistigi icin kabul edilebilir, sinirli bir risktir.
    }

    // BRIDGE_TRANSPARENT dalinda, sync_relay_with_filter()'in handler'i olarak
    // kullanilir (main.cpp'de global syncMaster'a yonlendiren duz bir static
    // fonksiyon araciligiyla - bkz. rx-node/src/main.cpp).
    void handleIncoming(SyncPacketType type, BridgeMode mode, AirRate rate) {
        if (_state != SYNC_SENDING && _state != SYNC_WAITING_ACK) return;
        if (type != SYNC_TYPE_ACK) return;
        if (mode != _pendingMode || rate != _pendingRate) return; // baska bir istegin ACK'i olabilir - yoksay

        _state = SYNC_OK;
        Serial.println("# [SYNC] TX onayladi (ACK alindi) - profil uygulaniyor");
        if (_onApply) _onApply(mode, rate);
    }

    // /status JSON'una eklenecek kisa durum kodu - RX web panelinde satir
    // olarak gosterilir. Hicbir senkronizasyon calismiyorsa NULL doner (satir
    // gizlenir).
    const char* statusText() const {
        switch (_state) {
            case SYNC_SENDING:
            case SYNC_WAITING_ACK: return "pending";
            case SYNC_OK:          return "ok";
            case SYNC_FAILED:      return "failed";
            default:                return nullptr;
        }
    }

private:
    // AUX LOW ise (E22 mesgul) bu deneme sessizce atlanir - _attempts/
    // _lastSendMs yine de ilerletilir, cunku tick() zaten SYNC_SEND_REPEAT
    // kez tekrarliyor; bir denemenin atlanmasi sorun degil.
    void _sendNext() {
        SyncPacket pkt;
        sync_build_packet(SYNC_TYPE_CMD, _pendingMode, _pendingRate, pkt);
        lora_try_write(*_serial, _auxPin, (uint8_t*)&pkt, sizeof(pkt));
        _attempts++;
        _lastSendMs = millis();
    }

    HardwareSerial* _serial  = nullptr;
    int             _auxPin  = -1;
    SyncApplyFn     _onApply = nullptr;

    SyncState  _state       = SYNC_IDLE;
    BridgeMode _pendingMode = BRIDGE_COMPRESSED;
    AirRate    _pendingRate = AIR_RATE_2_4K;
    uint8_t    _attempts    = 0;
    uint32_t   _lastSendMs  = 0;
    uint32_t   _waitStartMs = 0;
};
