#include "lora_rx.h"
#include "crypto.h"

// ============================================================================
// Periyodik RSSI sorgusu (pollRssi) — E22'nin "son alınan paketin RSSI'ı" özel
// sorgu komutu: 0xC0 0xC1 0xC2 0xC3 0x00 0x01 (adres 0x01), yanıt 4 byte
// [0xC1][ADDR][LEN][RSSI]; dBm = -(256 - RSSI byte). REG1 bit5 (RSSI ambient
// noise enable, bkz. lora_modes.h LORA_REG1_VALUE) bu sorgunun çalışması için
// açık tutulur.
//
// ÇAKIŞMA RİSKİ: sorgu yanıtı ile gerçek bir telemetri paketi AYNI Serial1 RX
// hattından geliyor ve donanım bir çerçeveleme/delimiter sağlamıyor. Bunu
// azaltmak için: (1) sorgu sadece hat boşken (available()==0) başlatılır,
// (2) bekleme kısa bir timeout ile sınırlıdır, (3) bekleme sırasında hattaki
// ilk byte peek edilir — 0xC1 değilse bu gerçek bir paketin başlangıcıdır
// (şifreli nonce'un ilk byte'ı olan seq_id'nin RASTGELE 0xC1'e denk gelme
// ihtimali ~1/256 - kabul edilen, ölçüde bir kalıntı risk) ve RSSI beklemesi
// hemen terk edilir, update() baytları normal paket olarak işler.
// ============================================================================
#define RSSI_QUERY_INTERVAL_MS 1500UL  // sorgular arası süre (1-2sn araliginda)
#define RSSI_QUERY_TIMEOUT_MS  150UL   // yanit icin kisa, non-blocking timeout
#define RSSI_RESP_LEN          4       // beklenen yanit: C1 ADDR LEN RSSI

LoraRx::LoraRx() : _serial(Serial1), _err_count(0) {}

bool LoraRx::_wait_aux(uint32_t timeout_ms) {
    uint32_t t = millis();
    while (digitalRead(LORA_AUX_PIN) == LOW) {
        if (millis() - t > timeout_ms) return false;
        delay(1);
    }
    return true;
}

bool LoraRx::_configure() {
    digitalWrite(LORA_M0_PIN, HIGH);
    digitalWrite(LORA_M1_PIN, HIGH);
    delay(100);
    _wait_aux(1000);

    while (_serial.available()) _serial.read();

    // TX ile aynı yapılandırma — frekans ve hız eşleşmeli
    uint8_t cmd[10] = {0xC0, 0x00, 0x00, 0x00, 0x62, 0x00, 0x12, 0x00, 0x00, 0x00};
    _serial.write(cmd, sizeof(cmd));

    _serial.setTimeout(500);
    uint8_t resp[10] = {0};
    size_t n = _serial.readBytes(resp, 10);
    bool ok = (n >= 1 && resp[0] == 0xC1);

    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);
    delay(100);
    _wait_aux(1000);

    return ok;
}

bool LoraRx::begin() {
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);

    _serial.begin(LORA_UART_BAUD, SERIAL_8N1, LORA_ESP_RX_PIN, LORA_ESP_TX_PIN);
    delay(100);

    bool cfg_ok = _configure();
    if (!cfg_ok) {
        Serial.println("# [LoRa] Yapılandırma yanıtı alınamadı (modül zaten hazır olabilir)");
        digitalWrite(LORA_M0_PIN, LOW);
        digitalWrite(LORA_M1_PIN, LOW);
    } else {
        Serial.println("# [LoRa] RX hazir | 868 MHz, 2.4kbps — dinleniyor...");
    }
    return true;
}

bool LoraRx::update(TelemetryPacket* pkt_out) {
    // 48 byte tam paket gelmeden bekleme
    if (_serial.available() < (int)ENCRYPTED_SIZE) return false;

    uint8_t enc_buf[ENCRYPTED_SIZE];
    size_t n = _serial.readBytes(enc_buf, ENCRYPTED_SIZE);
    if (n != ENCRYPTED_SIZE) {
        _err_count++;
        if (_stats) _stats->onCorrupt();
        return false;
    }

    // Decrypt: 48 byte → 40 byte
    uint8_t plain[PACKET_SIZE];
    decrypt_packet(enc_buf, plain);

    // Deserialize + checksum doğrula
    if (!unpack_telemetry(plain, pkt_out)) {
        _err_count++;
        if (_stats) _stats->onCorrupt();
        Serial.printf("# [LoRa] Checksum/header hatasi (toplam: %lu)\n", _err_count);
        return false;
    }

    if (_stats) _stats->onPacketOk(pkt_out->seq_id);
    return true;
}

void LoraRx::pollRssi() {
    uint32_t now = millis();

    if (_rssiState == RSSI_IDLE) {
        if (now - _lastRssiQueryMs < RSSI_QUERY_INTERVAL_MS) return;
        if (_serial.available() > 0) return; // hat mesgul - bu turu atla, telemetriyi bloklama

        uint8_t cmd[6] = {0xC0, 0xC1, 0xC2, 0xC3, 0x00, 0x01};
        _serial.write(cmd, sizeof(cmd));

        _lastRssiQueryMs = now;
        _rssiWaitStartMs = now;
        _rssiState       = RSSI_WAITING;
        return;
    }

    // RSSI_WAITING
    int avail = _serial.available();

    if (avail >= (int)ENCRYPTED_SIZE) {
        // 4 byte'lik bir yanit icin fazla veri birikti - bu kesinlikle gercek
        // bir telemetri paketi. Vazgec, update() normal sekilde islesin.
        _rssiState = RSSI_IDLE;
        return;
    }

    if (avail >= RSSI_RESP_LEN) {
        if (_serial.peek() != 0xC1) {
            // Ilk byte bizim yanitimizin basligi degil -> gercek bir paketin
            // basi olmali (yukaridaki yorumdaki ~1/256 kalinti risk haric).
            _rssiState = RSSI_IDLE;
            return;
        }
        uint8_t resp[RSSI_RESP_LEN];
        _serial.readBytes(resp, RSSI_RESP_LEN);
        if (resp[1] == 0x00 && resp[2] == 0x01) {
            uint8_t rssiByte = resp[3];
            _lastRssiDbm = -(int16_t)(256 - rssiByte);
            _rssiValid   = true;
        }
        _rssiState = RSSI_IDLE;
        return;
    }

    if (now - _rssiWaitStartMs > RSSI_QUERY_TIMEOUT_MS) {
        _rssiState = RSSI_IDLE; // yanit gelmedi, bir sonraki periyotta tekrar dene
    }
}
