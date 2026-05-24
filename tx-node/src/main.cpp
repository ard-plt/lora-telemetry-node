#include <Arduino.h>
#include "mavlink_reader.h"
#include "lora_tx.h"
#include "crypto.h"
#include "packet.h"

static MavlinkReader reader;
static LoraTx        lora;
static uint8_t       seq      = 0;
static uint32_t      lastSendMs = 0;
static uint32_t      lastLogMs  = 0;

// Serialize → deserialize loopback testi — setup'ta bir kez çalışır
static void run_loopback_test() {
    Serial.println("\n=== Loopback Testi ===");

    // Bilinen değerlerle sahte MavlinkData
    MavlinkData src = {};
    src.armed        = 1;
    src.flight_mode  = 3;
    src.lat          = 399123456;    // 39.9123456°
    src.lon          = 329876543;    // 32.9876543°
    src.alt          = 850000;       // 850.000 m MSL (mm cinsinden)
    src.relative_alt = 12500;        // 125.0 m (mm → cm = 1250 cm)
    src.groundspeed  = 15.5f;        // m/s → 1550 cm/s
    src.airspeed     = 16.2f;        // m/s → 1620 cm/s
    src.hdg          = 27350;        // 273.50°
    src.roll         = 0.1745f;      // ~10° radyan → 1000 cdeg
    src.pitch        = -0.0873f;     // ~-5° radyan → -500 cdeg
    src.yaw          = 2.7925f;      // ~160° radyan → 16000 cdeg
    src.climb        = 1.2f;         // m/s → 120 cm/s
    src.vbat_mv      = 14800;        // 14.800 V
    src.current_ca   = 230;          // 23.0 A (centiamps) → 23 da
    src.battery_pct  = 72;
    src.lidar_cm     = 430;          // 4.30 m

    // Serialize
    TelemetryPacket pkt;
    pack_telemetry(&src, &pkt, 42);

    // Ham byte dizisine dönüştür (memcpy simülasyonu)
    uint8_t buf[PACKET_SIZE];
    memcpy(buf, &pkt, PACKET_SIZE);

    // Deserialize
    TelemetryPacket out;
    bool ok = unpack_telemetry(buf, &out);

    // Sonuçları karşılaştır
    bool pass = true;
    #define CHECK(field, expected) \
        if (out.field != expected) { \
            Serial.printf("  FAIL  %-20s  got=%ld  exp=%ld\n", \
                          #field, (long)out.field, (long)(expected)); \
            pass = false; \
        } else { \
            Serial.printf("  PASS  %-20s  = %ld\n", #field, (long)out.field); \
        }

    if (!ok) { Serial.println("  FAIL  checksum veya header hatasi!"); pass = false; }

    CHECK(header,         PACKET_HEADER)
    CHECK(seq_id,         42)
    CHECK(latitude,       399123456)
    CHECK(longitude,      329876543)
    CHECK(altitude_mm,    850000)
    CHECK(relative_alt_cm, 1250)          // 12500 mm / 10
    CHECK(groundspeed_cms, 1550)
    CHECK(airspeed_cms,    1620)
    CHECK(heading_cd,      27350)
    CHECK(roll_cd,         1000)           // 10°×100
    CHECK(pitch_cd,        -500)
    CHECK(battery_pct,     72)
    CHECK(flight_mode,     3)
    CHECK(armed,           1)
    CHECK(lidar_cm,        430)

    #undef CHECK

    Serial.printf("\n=== Loopback Sonuc: %s ===\n\n", pass ? "GECTI" : "BASARISIZ");
}

// AES-128-CTR encrypt → decrypt loopback testi
static void run_crypto_test() {
    Serial.println("=== Crypto Loopback Testi ===");

    // Bilinen bir TelemetryPacket oluştur
    MavlinkData src = {};
    src.lat = 399123456; src.lon = 329876543; src.alt = 850000;
    src.vbat_mv = 14800; src.battery_pct = 72; src.armed = 1;
    TelemetryPacket plain_pkt;
    pack_telemetry(&src, &plain_pkt, 99);

    // Şifrele
    uint8_t enc_buf[ENCRYPTED_SIZE];
    encrypt_packet((uint8_t*)&plain_pkt, enc_buf, plain_pkt.seq_id, 123456);

    // Şifreli verinin düz metinden farklı olduğunu doğrula
    bool differs = (memcmp(enc_buf + 8, &plain_pkt, PACKET_SIZE) != 0);
    Serial.printf("  %s  Sifreleme degistiriyor\n", differs ? "PASS" : "FAIL");

    // Çöz
    uint8_t dec_buf[PACKET_SIZE];
    decrypt_packet(enc_buf, dec_buf);

    // Çözülen veri orijinalle eşleşmeli
    bool matches = (memcmp(dec_buf, &plain_pkt, PACKET_SIZE) == 0);
    Serial.printf("  %s  Decrypt == plaintext\n", matches ? "PASS" : "FAIL");

    // Checksum hâlâ geçerli mi?
    TelemetryPacket recovered;
    bool ok = unpack_telemetry(dec_buf, &recovered);
    Serial.printf("  %s  Checksum gecerli\n", ok ? "PASS" : "FAIL");

    Serial.printf("  Nonce: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  enc_buf[0], enc_buf[1], enc_buf[2], enc_buf[3],
                  enc_buf[4], enc_buf[5], enc_buf[6], enc_buf[7]);

    Serial.printf("\n=== Crypto Sonuc: %s ===\n\n",
                  (differs && matches && ok) ? "GECTI" : "BASARISIZ");
}

void setup() {
    Serial.begin(115200);
    Serial.println("[TX] LoRa Telemetry TX Node basliyor...");

    run_loopback_test();
    run_crypto_test();

    reader.begin();
    Serial.printf("[TX] UART2 baslatildi: %d baud, RX=GPIO%d TX=GPIO%d\n",
                  MAVLINK_BAUD, MAVLINK_RX_PIN, MAVLINK_TX_PIN);

    lora.begin();  // hata olursa log basar ama devam eder
}

void loop() {
    // Pixhawk'tan gelen byte'ları sürekli parse et
    reader.update();

    uint32_t now = millis();

    // 2 Hz LoRa gönderim döngüsü (her 500 ms)
    if (now - lastSendMs >= LORA_SEND_INTERVAL_MS) {
        lastSendMs = now;

        // 1) Serialize
        TelemetryPacket pkt;
        pack_telemetry(&reader.getData(), &pkt, seq++);

        // 2) Şifrele: 40 byte → 48 byte [nonce8 | cipher40]
        uint8_t enc_buf[ENCRYPTED_SIZE];
        encrypt_packet((uint8_t*)&pkt, enc_buf, pkt.seq_id, (uint32_t)now);

        // 3) LoRa ile gönder
        if (lora.send_raw(enc_buf, ENCRYPTED_SIZE)) {
            Serial.printf("[LoRa] sent seq=%d (encrypted) | armed=%d mode=%d "
                          "lat=%.5f lon=%.5f alt=%.1fm vbat=%.2fV\n",
                          pkt.seq_id, pkt.armed, pkt.flight_mode,
                          pkt.latitude  / 1e7f,
                          pkt.longitude / 1e7f,
                          pkt.altitude_mm / 1000.0f,
                          pkt.vbat_mv / 1000.0f);
        }
    }

    // 5 saniyede bir MAVLink ham veri özeti (tanılama)
    if (now - lastLogMs >= 5000) {
        lastLogMs = now;
        const MavlinkData& d = reader.getData();
        Serial.printf("[MAV] roll=%.1f° pitch=%.1f° yaw=%.1f° "
                      "gspd=%.1f aspd=%.1f climb=%.2f batt=%d%%\n",
                      degrees(d.roll), degrees(d.pitch), degrees(d.yaw),
                      d.groundspeed, d.airspeed, d.climb,
                      (int)d.battery_pct);
    }
}
