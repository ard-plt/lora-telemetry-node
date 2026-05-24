#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "mavlink_data.h"

// ===== Paket Sabitleri =====
#define PACKET_HEADER       0xAD
#define PACKET_SIZE         40      // Ham TelemetryPacket boyutu (byte)
#define ENCRYPTED_SIZE      48      // Nonce(8) + Ciphertext(40)
#define LIDAR_INVALID       0xFFFF  // Geçersiz lidar değeri

// ===== LoRa Parametreleri =====
#define LORA_FREQ_MHZ       868.0f
#define LORA_SF             9
#define LORA_BW_KHZ         125.0f
#define LORA_CR             5       // 4/5
#define LORA_PREAMBLE       8
#define LORA_TX_POWER_DBM   30
#define LORA_SEND_INTERVAL_MS 500   // 2 Hz

// ===== UART Sabitleri =====
#define MAVLINK_BAUD        57600
#define GS_BAUD             115200
#define MAVLINK_UART_NUM    2       // ESP32 UART2
#define MAVLINK_RX_PIN      16
#define MAVLINK_TX_PIN      17

// ===== E22 UART Pin Mapping =====
// TX node: GPIO32/33 (GPIO16/17 Pixhawk için kullanılıyor)
// RX node: GPIO17/16 (RX2/TX2 etiketli pinler) — platformio.ini'de override edilir
#ifndef LORA_ESP_TX_PIN
#define LORA_ESP_TX_PIN     32      // ESP32 TX → E22 RX  (TX node varsayılanı)
#endif
#ifndef LORA_ESP_RX_PIN
#define LORA_ESP_RX_PIN     33      // ESP32 RX ← E22 TX  (TX node varsayılanı)
#endif
#define LORA_UART_BAUD      9600    // E22 varsayılan seri baud
#ifndef LORA_M0_PIN
#define LORA_M0_PIN         26      // Mode 0
#endif
#ifndef LORA_M1_PIN
#define LORA_M1_PIN         25      // Mode 1
#endif
#ifndef LORA_AUX_PIN
#define LORA_AUX_PIN        27      // HIGH = modül hazır
#endif

// ===== Telemetri Paketi (40 byte, little-endian) =====
// __attribute__((packed)) ile padding yok
typedef struct __attribute__((packed)) {
    uint8_t  header;            // 0     sabit 0xAD
    uint8_t  seq_id;            // 1     sıra numarası

    int32_t  latitude;          // 2-5   degE7 (örn. 399000000 = 39.9°)
    int32_t  longitude;         // 6-9   degE7
    int32_t  altitude_mm;       // 10-13 MSL yükseklik (mm)

    int16_t  relative_alt_cm;   // 14-15 yerden yükseklik (cm)
    int16_t  groundspeed_cms;   // 16-17 yer hızı (cm/s)
    int16_t  airspeed_cms;      // 18-19 hava hızı (cm/s)
    int16_t  heading_cd;        // 20-21 yön (centidegree, 0–36000)
    int16_t  roll_cd;           // 22-23 roll (centidegree)
    int16_t  pitch_cd;          // 24-25 pitch (centidegree)
    int16_t  yaw_cd;            // 26-27 yaw (centidegree)
    int16_t  climb_rate_cms;    // 28-29 tırmanma hızı (cm/s)

    uint16_t vbat_mv;           // 30-31 batarya voltajı (mV)
    int16_t  current_da;        // 32-33 akım (deciamp, x10)
    uint16_t lidar_cm;          // 34-35 lidar mesafe (cm), 0xFFFF=geçersiz

    uint8_t  battery_pct;       // 36    batarya yüzdesi
    uint8_t  flight_mode;       // 37    uçuş modu
    uint8_t  armed;             // 38    0=disarmed, 1=armed
    uint8_t  checksum;          // 39    XOR of bytes 0–38
} TelemetryPacket;

// Boyut kontrolü — derleme zamanında hata verir
static_assert(sizeof(TelemetryPacket) == PACKET_SIZE,
    "TelemetryPacket boyutu 40 byte olmali!");

// ===== Checksum Hesaplama =====
// Byte 0..38 üzerinde XOR
inline uint8_t calc_checksum(const uint8_t* buf, uint8_t len) {
    uint8_t cs = 0;
    for (uint8_t i = 0; i < len; i++) cs ^= buf[i];
    return cs;
}

// Checksum alanını doldur (byte 39)
inline void fill_checksum(TelemetryPacket* pkt) {
    pkt->checksum = calc_checksum((const uint8_t*)pkt, PACKET_SIZE - 1);
}

// Checksum doğrula
inline bool verify_checksum(const TelemetryPacket* pkt) {
    uint8_t expected = calc_checksum((const uint8_t*)pkt, PACKET_SIZE - 1);
    return pkt->checksum == expected;
}

// ===== Serialize: MavlinkData → TelemetryPacket =====
// Birim dönüşümlerini uygular ve checksum'ı doldurur
inline void pack_telemetry(const MavlinkData* d, TelemetryPacket* pkt, uint8_t seq) {
    pkt->header         = PACKET_HEADER;
    pkt->seq_id         = seq;

    pkt->latitude       = d->lat;
    pkt->longitude      = d->lon;
    pkt->altitude_mm    = d->alt;
    pkt->relative_alt_cm = (int16_t)(d->relative_alt / 10);   // mm → cm

    pkt->groundspeed_cms = (int16_t)(d->groundspeed * 100.0f); // m/s → cm/s
    pkt->airspeed_cms    = (int16_t)(d->airspeed    * 100.0f);
    pkt->heading_cd      = (int16_t)d->hdg;                    // zaten cdeg

    // rad → centidegree
    pkt->roll_cd         = (int16_t)(d->roll  * (180.0f / (float)M_PI) * 100.0f);
    pkt->pitch_cd        = (int16_t)(d->pitch * (180.0f / (float)M_PI) * 100.0f);
    pkt->yaw_cd          = (int16_t)(d->yaw   * (180.0f / (float)M_PI) * 100.0f);

    pkt->climb_rate_cms  = (int16_t)(d->climb * 100.0f);       // m/s → cm/s

    pkt->vbat_mv         = d->vbat_mv;
    // centiamps → deciamps (÷10); negatif değer korunur
    pkt->current_da      = (int16_t)(d->current_ca / 10);

    pkt->lidar_cm        = d->lidar_cm;
    // battery_pct: -1 = bilinmiyor → 0xFF
    pkt->battery_pct     = (d->battery_pct < 0) ? 0xFF : (uint8_t)d->battery_pct;
    pkt->flight_mode     = d->flight_mode;
    pkt->armed           = d->armed;

    fill_checksum(pkt);
}

// ===== Deserialize: ham byte dizisi → TelemetryPacket =====
// Döndürür: true=checksum geçerli, false=bozuk paket
inline bool unpack_telemetry(const uint8_t* buf, TelemetryPacket* pkt) {
    memcpy(pkt, buf, PACKET_SIZE);
    return (pkt->header == PACKET_HEADER) && verify_checksum(pkt);
}
