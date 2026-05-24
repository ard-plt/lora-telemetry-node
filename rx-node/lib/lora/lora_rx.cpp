#include "lora_rx.h"
#include "crypto.h"

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
        return false;
    }

    // Decrypt: 48 byte → 40 byte
    uint8_t plain[PACKET_SIZE];
    decrypt_packet(enc_buf, plain);

    // Deserialize + checksum doğrula
    if (!unpack_telemetry(plain, pkt_out)) {
        _err_count++;
        Serial.printf("# [LoRa] Checksum/header hatasi (toplam: %lu)\n", _err_count);
        return false;
    }

    return true;
}
