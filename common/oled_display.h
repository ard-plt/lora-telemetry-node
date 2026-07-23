#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// SSD1306 128x64 I2C OLED — sadece RX node kullanır (sinyal/durum göstergesi).
// common/ diğer paylaşılan dosyalarla (packet.h, lora_modes.h, web_config.h)
// aynı desende header-only — common/ PlatformIO'ya sadece -I ile göründüğü
// için ayrı bir .cpp derlenmez, tüm gövdeler in-class (implicit inline).
//
// I2C pinleri (RX node, ESP32-S3-WROOM-1): SDA=GPIO11, SCL=GPIO12.
// Çakışma kontrolü: mevcut kullanılan pinler 2(LED),4/5(E22 UART1),6(AUX),
// 7(M0),8(M1),9(config buton) — GPIO11/12 bunların hiçbiriyle çakışmıyor.
// ESP32-S3-WROOM-1'in Flash/PSRAM için ayırdığı GPIO26-37 aralığının da
// dışında (bkz. CLAUDE.md/breadboard_wiring.md), GPIO11/12 S3'te strapping
// pini de değildir (S3 strapping: GPIO0/3/45/46) — güvenle kullanılabilir.
// ============================================================================

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_I2C_ADDR   0x3C   // SSD1306 128x64 modüllerinde yaygın adres (bazılarında 0x3D)
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN    11
#endif
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN    12
#endif
#define OLED_REFRESH_MS 500    // ekran yeniden çizim periyodu (I2C yazımı görece yavaş, non-blocking throttle)

// OLED'de gösterilecek anlık durum — main.cpp her loop() turunda doldurup update()'e verir.
struct OledStatusData {
    const char* bridgeModeName;   // bridge_mode_oled_label()
    const char* airRateName;      // air_rate_name()
    bool        rssiValid;
    int16_t     rssiDbm;
    bool        paired;
    uint32_t    packetCount;
    uint32_t    lostCount;
    uint32_t    corruptCount;
    uint8_t     efficiencyPct;
    bool        vbatValid;
    uint16_t    vbatMv;
};

class OledDisplay {
public:
    // I2C başlat + ekranı ilklendir. Ekran bağlı değilse/yanıt vermezse false
    // döner — caller bu durumda update()'i yine çağırabilir, no-op olur
    // (OLED'siz kurulumlarda diğer işlevsellik etkilenmemeli).
    bool begin() {
        Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
        _ok = _display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
        if (_ok) {
            _display.clearDisplay();
            _display.setTextColor(SSD1306_WHITE);
            _display.setTextSize(1);
            _display.display();
        }
        return _ok;
    }

    bool isOk() const { return _ok; }

    // loop() içinde HER iterasyonda çağrılmalı. delay() KULLANMAZ — kendi
    // içinde OLED_REFRESH_MS'e göre non-blocking throttle eder.
    void update(const OledStatusData& d) {
        if (!_ok) return;
        uint32_t now = millis();
        if (now - _lastDrawMs < OLED_REFRESH_MS) return;
        _lastDrawMs = now;
        _render(d);
    }

private:
    // Satır düzeni: sabit "TUAV Telemetri" başlığı + mod etiketi + air rate
    // için ayrı bir satır eklendiğinde (eskiden 2 olan üst blok 3 satıra
    // çıkınca) alttaki bilgiler (RSSI/Durum/Pkt/Bozuk) ekrana sığmak için
    // eski 10-12px'lik düzensiz satır aralığından, tüm satırlarda tek tip
    // 9px'lik aralığa geçirildi — 7 satır x 9px = 63px, 64px'lik ekrana
    // sığıyor (son satır y=54 ile ESKİ konumuyla birebir aynı kaldı), içerik/
    // sıralama/anlam değişmedi, sadece birkaç pikselik kayma var.
    void _render(const OledStatusData& d) {
        _display.clearDisplay();
        _display.setTextColor(SSD1306_WHITE);
        _display.setTextSize(1);
        _display.setTextWrap(false);

        _display.setCursor(0, 0);
        _display.print("TUAV Telemetri");

        _display.setCursor(0, 9);
        _display.print(d.bridgeModeName);

        _display.setCursor(0, 18);
        _display.printf("Rate: %s", d.airRateName);

        _display.setCursor(0, 27);
        if (d.rssiValid) _display.printf("RSSI: %d dBm", (int)d.rssiDbm);
        else             _display.print("RSSI: --");
        _drawSignalBar(98, 27, d.rssiValid ? d.rssiDbm : -127);

        _display.setCursor(0, 36);
        _display.print(d.paired ? "Durum: BAGLI" : "Durum: ARANIYOR");

        _display.setCursor(0, 45);
        _display.printf("Pkt:%lu Kayip:%lu", (unsigned long)d.packetCount, (unsigned long)d.lostCount);

        _display.setCursor(0, 54);
        _display.printf("Bozuk:%lu  Verim:%%%u", (unsigned long)d.corruptCount, (unsigned)d.efficiencyPct);

        // Sağ üst köşe: batarya voltajı
        _display.setCursor(88, 0);
        if (d.vbatValid) _display.printf("%.2fV", d.vbatMv / 1000.0f);
        else             _display.print("--V");

        _display.display();
    }

    // Basit 4 çubuklu sinyal göstergesi. Eşikler LoRa/868MHz kısa-orta menzil
    // pratiğine göre kabaca seçildi (gerçek E22 duyarlılığı -148dBm'e kadar
    // iner ama pratikte -60..-120dBm aralığı en sık görülen bölge).
    void _drawSignalBar(int x, int y, int16_t dbm) {
        int bars = 0;
        if (dbm > -110) bars = 1;
        if (dbm > -95)  bars = 2;
        if (dbm > -80)  bars = 3;
        if (dbm > -65)  bars = 4;

        for (int i = 0; i < 4; i++) {
            int barH = 3 + i * 3;
            int barX = x + i * 6;
            int barY = y + (12 - barH);
            if (i < bars) _display.fillRect(barX, barY, 4, barH, SSD1306_WHITE);
            else          _display.drawRect(barX, barY, 4, barH, SSD1306_WHITE);
        }
    }

    Adafruit_SSD1306 _display{OLED_WIDTH, OLED_HEIGHT, &Wire, -1};
    bool             _ok         = false;
    uint32_t         _lastDrawMs = 0;
};
