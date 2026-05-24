#include "lora_tx.h"

// E22 UART versiyonu — transparent mod
// M0=LOW, M1=LOW → Normal mod: seri porta yazılan byte LoRa ile gönderilir

LoraTx::LoraTx() : _serial(Serial1) {}

bool LoraTx::_wait_aux(uint32_t timeout_ms) {
    uint32_t t = millis();
    while (digitalRead(LORA_AUX_PIN) == LOW) {
        if (millis() - t > timeout_ms) return false;
        delay(1);
    }
    return true;
}

bool LoraTx::_configure() {
    // Program modu: M0=HIGH, M1=HIGH
    digitalWrite(LORA_M0_PIN, HIGH);
    digitalWrite(LORA_M1_PIN, HIGH);
    delay(100);
    _wait_aux(1000);

    // Gelen buffer'ı temizle
    while (_serial.available()) _serial.read();

    // Yapılandırma komutu (10 byte):
    // C0 ADDH ADDL NETID REG0  REG1  REG2  REG3  CRYPT_H CRYPT_L
    //
    // REG0=0x62: baud=9600 | parity=8N1 | air_rate=2.4kbps
    // REG1=0x00: sub-paket=200B | tx_power=30dBm
    // REG2=0x12: kanal 18 → 850+18=868 MHz
    // REG3=0x00: transparent mod, LBT kapalı
    uint8_t cmd[10] = {0xC0, 0x00, 0x00, 0x00, 0x62, 0x00, 0x12, 0x00, 0x00, 0x00};
    _serial.write(cmd, sizeof(cmd));

    // Yanıt bekleniyor: C1 + 9 byte
    _serial.setTimeout(500);
    uint8_t resp[10] = {0};
    size_t n = _serial.readBytes(resp, 10);
    bool ok = (n >= 1 && resp[0] == 0xC1);

    // Normal mod: M0=LOW, M1=LOW
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);
    delay(100);
    _wait_aux(1000);

    return ok;
}

bool LoraTx::begin() {
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);

    // E22 ile haberleşecek UART1 — TX=GPIO32, RX=GPIO33
    _serial.begin(LORA_UART_BAUD, SERIAL_8N1, LORA_ESP_RX_PIN, LORA_ESP_TX_PIN);
    delay(100);

    bool cfg_ok = _configure();
    if (cfg_ok) {
        Serial.printf("[LoRa] TX hazir | 868 MHz, 2.4kbps air rate\n");
    } else {
        // Yanıt gelmedi: modül zaten yapılandırılmış olabilir, devam et
        Serial.println("[LoRa] Yapılandırma yanıtı alınamadı (modül zaten hazır olabilir)");
        // Normal modu garantile
        digitalWrite(LORA_M0_PIN, LOW);
        digitalWrite(LORA_M1_PIN, LOW);
    }
    return true;
}

bool LoraTx::send_raw(const uint8_t* data, size_t len) {
    // AUX HIGH bekle — modül meşgulse gönderme
    if (!_wait_aux(500)) {
        Serial.println("[LoRa] AUX timeout, paket atlandı");
        return false;
    }

    _serial.write(data, len);

    // Gönderim tamamlanana kadar bekle (AUX LOW → HIGH geçişi)
    delay(5);           // E22'nin TX'e başlaması için kısa bekleme
    _wait_aux(1000);    // Gönderim bitince AUX HIGH olur

    return true;
}
