#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// ============================================================================
// LoRa Çalışma Parametreleri — iki BAĞIMSIZ boyut:
//
//   BridgeMode : ESP32 tarafında hangi loop() davranışı çalışır.
//                BRIDGE_TRANSPARENT : TAM transparan MAVLink köprü (Serial<->Serial1
//                                     ham byte aktarımı) — parametre/kalibrasyon/
//                                     waypoint işlemleri için (eski "KALİBRASYON").
//                BRIDGE_COMPRESSED  : parse->pack->şifrele->LoRa gönder/al akışı —
//                                     uçuş sırasında kullanılan asıl mod (eski
//                                     "UZUN MENZİL").
//   AirRate    : E22'nin REG0'daki air data rate + eşleşen UART baud'u. Web config
//                panelinden BridgeMode'dan BAĞIMSIZ seçilebilir; ikisi birlikte
//                lora_build_profile() ile tek bir REG0 yazımına dönüştürülür.
//
// ÖNEMLİ: TX ve RX node'un LoRa üzerinden konuşabilmesi için ikisinin de AYNI
// AirRate'te olması gerekir (frekans/kanal REG2 zaten sabit ve ortak) — bu,
// LoRa'nın fiziksel katman kısıtıdır, yazılım bunu otomatik eşitlemez; operatör
// her iki node'un web panelinden aynı air rate'i seçmelidir.
//
// REG0/REG1 bit alanları CDEBYTE E22-900T30D resmi kullanım kılavuzuna göre:
//   bit7-5 (REG0): UART baud  — 000=1200 001=2400 010=4800 011=9600
//                                100=19200 101=38400 110=57600 111=115200
//   bit4-3 (REG0): parity     — 00=8N1 (bu projede sabit, hep 8N1)
//   bit2-0 (REG0): air rate   — 000=0.3k 001=1.2k 010=2.4k 011=4.8k
//                                100=9.6k 101=19.2k 110=38.4k 111=62.5k
//   bit5   (REG1): RSSI ambient noise enable — dBm sorgusu (oled_display/link
//                  istatistikleri) için açık tutulur, paket formatını etkilemez.
// Kaynak: cdebyte E22-900T30D kullanım kılavuzu (REG0 air-rate/UART-baud tablosu),
// mischianti.org E22 REG1 (TRANSMISSION_MODE) bit dokümantasyonu — WebSearch ile
// doğrulandı, tahmin edilmedi.
// ============================================================================

enum BridgeMode {
    BRIDGE_TRANSPARENT = 0,   // eski MODE_CALIBRATION
    BRIDGE_COMPRESSED  = 1    // eski MODE_LONGRANGE
};

enum AirRate {
    AIR_RATE_0_3K  = 0,
    AIR_RATE_1_2K  = 1,
    AIR_RATE_2_4K  = 2,   // BRIDGE_COMPRESSED'in varsayılanı (eski sabit UZUN MENZİL değeri)
    AIR_RATE_4_8K  = 3,
    AIR_RATE_9_6K  = 4,
    AIR_RATE_19_2K = 5,
    AIR_RATE_38_4K = 6,
    AIR_RATE_62_5K = 7    // BRIDGE_TRANSPARENT'in varsayılanı (eski sabit KALİBRASYON değeri)
};

#define LORA_AIR_RATE_COUNT 8
#define LORA_REG1_VALUE     0x20   // bit5=1 (RSSI ambient noise enable), diğer bitler 0

struct LoraProfile {
    uint8_t     reg0;
    uint32_t    uartBaud;
    const char* name;      // ör. "2.4 kbps"
};

inline const char* bridge_mode_name(BridgeMode m) {
    return (m == BRIDGE_TRANSPARENT) ? "TRANSPARAN KOPRU" : "SIKISTIRILMIS+SIFRELI";
}

// OLED başlığı için kısa mod etiketi — bridge_mode_name() Serial log'larda
// kullanılan (uzun/büyük harf) isim, bu ise 128px genişlikte tek satıra
// sığması için ayrı tutulan OLED'e özgü metin.
inline const char* bridge_mode_oled_label(BridgeMode m) {
    return (m == BRIDGE_TRANSPARENT) ? "Kalibrasyon-Transparan" : "Uzun Menzil-Sifreli";
}

// İnsan-okunur isim ("2.4 kbps") — loglama/OLED için.
inline const char* air_rate_name(AirRate r) {
    static const char* kNames[LORA_AIR_RATE_COUNT] = {
        "0.3 kbps", "1.2 kbps", "2.4 kbps", "4.8 kbps",
        "9.6 kbps", "19.2 kbps", "38.4 kbps", "62.5 kbps"
    };
    return kNames[(int)r];
}

// Bare sayısal string ("2.4") — URL parametresi / JSON alanı için (birim son eki yok).
inline const char* air_rate_kbps_str(AirRate r) {
    static const char* kStr[LORA_AIR_RATE_COUNT] = {
        "0.3", "1.2", "2.4", "4.8", "9.6", "19.2", "38.4", "62.5"
    };
    return kStr[(int)r];
}

// air_rate_kbps_str() değerinden AirRate'e geri döner. Eşleşme yoksa false.
inline bool air_rate_from_kbps_str(const String& s, AirRate& out) {
    for (int i = 0; i < LORA_AIR_RATE_COUNT; i++) {
        if (s == air_rate_kbps_str((AirRate)i)) { out = (AirRate)i; return true; }
    }
    return false;
}

// Önerilen UART baud (E22<->ESP32, Serial1). Datasheette UART baud ve air rate
// birbirinden BAĞIMSIZ alanlar — resmi bir "şu air rate şu baud'la kullanılmalı"
// tablosu YOK, bu eşleştirme projenin kendi mühendislik tercihidir: düşük air
// rate'lerde UART'ın darboğaz olmaması için taban 9600'de tutulur, yüksek air
// rate'lerde paket aktarım gecikmesini azaltmak için UART hızı da artırılır.
// 2.4k->9600 ve 62.5k->115200 halihazırda kullanılan/doğrulanmış eşleşmelerdir,
// bu ikisi değiştirilmeden korundu.
inline uint32_t air_rate_uart_baud(AirRate r) {
    static const uint32_t kBaud[LORA_AIR_RATE_COUNT] = {
        9600, 9600, 9600, 9600, 19200, 38400, 57600, 115200
    };
    return kBaud[(int)r];
}

// air_rate_uart_baud() değerinin REG0 bit7-5 kodu (yukarıdaki tabloyla birebir eşleşir).
inline uint8_t air_rate_uart_baud_bits(AirRate r) {
    static const uint8_t kBits[LORA_AIR_RATE_COUNT] = {
        0b011, 0b011, 0b011, 0b011, 0b100, 0b101, 0b110, 0b111
    };
    return kBits[(int)r];
}

// Bridge mode'un ilk açılış / hiç kaydedilmemiş varsayılan air rate'i — eski
// sabit-profil davranışını (KALİBRASYON=62.5k, UZUN MENZİL=2.4k) korur.
inline AirRate default_air_rate_for(BridgeMode m) {
    return (m == BRIDGE_TRANSPARENT) ? AIR_RATE_62_5K : AIR_RATE_2_4K;
}

// AirRate'i REG0 (air-rate bitleri + UART-baud bitleri + sabit 8N1 parity) ve
// eşleşen UART baud'una dönüştürür — "iki parametreyi birleştirip tek REG0"
// burada yapılır.
inline LoraProfile lora_build_profile(AirRate rate) {
    uint8_t reg0 = (uint8_t)((air_rate_uart_baud_bits(rate) << 5) |
                             (0b00 << 3) |             // parity: 8N1, sabit
                             ((uint8_t)rate & 0x07));  // air rate bitleri
    return LoraProfile{ reg0, air_rate_uart_baud(rate), air_rate_name(rate) };
}

// AUX HIGH olana kadar bekle
inline bool lora_wait_aux(int auxPin, uint32_t timeout_ms = 1000) {
    uint32_t t = millis();
    while (digitalRead(auxPin) == LOW) {
        if (millis() - t > timeout_ms) return false;
        delay(1);
    }
    return true;
}

// AUX pini musaitse (HIGH) veriyi yazar, mesgulse (LOW) hic yazmadan false
// doner - bloklamaz, timeout eklemez. Periyodik tekrar eden gonderimler
// (PING, sync CMD gibi) icin yeterlidir: bu tur atlanirsa bir sonraki
// periyotta zaten tekrar denenecektir.
inline bool lora_try_write(HardwareSerial& serial, int auxPin, const uint8_t* data, size_t len) {
    if (digitalRead(auxPin) == LOW) return false; // modul mesgul, bu turu atla
    serial.write(data, len);
    return true;
}

// Modülü verilen profile göre yeniden yapılandırır. REG1 her zaman LORA_REG1_VALUE
// (RSSI ambient noise açık) yazılır — paket formatını/boyutunu etkilemez.
// Çağrıdan sonra serial.begin(profile.uartBaud, ...) ile UART'ı da güncellemeyi UNUTMA.
inline bool lora_apply_profile(HardwareSerial& serial, int m0Pin, int m1Pin, int auxPin,
                                const LoraProfile& profile) {
    digitalWrite(m0Pin, HIGH);
    digitalWrite(m1Pin, HIGH);
    delay(100);
    lora_wait_aux(auxPin, 1000);

    while (serial.available()) serial.read();

    // C0 ADDH ADDL NETID REG0 REG1 REG2 REG3 CRYPT_H CRYPT_L
    // REG1=LORA_REG1_VALUE (RSSI ambient noise) | REG2=0x12 (kanal 18, 868MHz, sabit)
    uint8_t cmd[10] = {0xC0, 0x00, 0x00, 0x00, profile.reg0, LORA_REG1_VALUE, 0x12, 0x00, 0x00, 0x00};
    serial.write(cmd, sizeof(cmd));

    serial.setTimeout(500);
    uint8_t resp[10] = {0};
    size_t n = serial.readBytes(resp, 10);
    bool ok = (n >= 1 && resp[0] == 0xC1);

    digitalWrite(m0Pin, LOW);
    digitalWrite(m1Pin, LOW);
    delay(100);
    lora_wait_aux(auxPin, 1000);

    return ok;
}
