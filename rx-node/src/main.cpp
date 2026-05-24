#include <Arduino.h>
#include "lora_rx.h"
#include "mavlink_tx.h"
#include "packet.h"

#define LED_PIN 5  // Paket alındığında blink

static LoraRx    lora;
static MavlinkTx mav;

static uint32_t  lastHeartbeatMs = 0;
static uint32_t  pkt_count       = 0;

// Son alınan paket — HEARTBEAT için saklıyoruz
static TelemetryPacket last_pkt  = {};

void setup() {
    // QGC MAVLink bağlantısı: 57600 baud (standart ArduPilot telemetri hızı)
    Serial.begin(57600);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Başlangıç text'i — QGC parser bunları geçersiz MAVLink olarak atlayacak
    Serial.println("# LoRa Telemetry RX Node starting...");

    lora.begin();
}

void loop() {
    TelemetryPacket pkt;

    // Yeni LoRa paketi geldi mi?
    if (lora.update(&pkt)) {
        last_pkt = pkt;
        pkt_count++;

        // Paket alındı — LED kısa blink
        digitalWrite(LED_PIN, HIGH);
        delay(50);
        digitalWrite(LED_PIN, LOW);

        // Tüm telemetri mesajlarını QGC'ye gönder
        mav.send_telemetry(pkt);
    }

    // HEARTBEAT: QGC bağlantısının kopmaması için 1 Hz
    uint32_t now = millis();
    if (now - lastHeartbeatMs >= 1000) {
        lastHeartbeatMs = now;
        mav.send_heartbeat(last_pkt);
    }
}
