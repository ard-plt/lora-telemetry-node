#include <Arduino.h>
#include "lora_rx.h"
#include "mavlink_tx.h"
#include "packet.h"
#include "lora_modes.h"
#include "web_config.h"
#include "oled_display.h"
#include "link_stats.h"
#include "sync_command.h"
#include "sync_master.h"
#include "link_led.h"

#define LED_PIN         2
#define CONFIG_BTN_PIN  9   // momentary buton: 3sn basili tutulunca web config AP'si acilir
#define PING_SEND_INTERVAL_MS 1200UL   // TX'e eslesme PING'i gonderim araligi (~1-1.5sn)

// LoraRx kendi içinde Serial1'i kullanıyor (lora_rx.cpp) - kalibrasyon modunda
// ham köprü için de aynı global Serial1'i kullanıyoruz, ikinci nesne açmıyoruz.
static LoraRx           lora;   // uzun menzil modunda paket almak için
static MavlinkTx        mav;
static WebConfigPortal  webConfig;
static OledDisplay      oled;
static LinkStatsTracker linkStats;
static SyncMaster       syncMaster; // RX-master mod/rate senkronizasyonu (bkz. sync_master.h)
static LinkLed          linkLed;    // eslesme durumuna gore yanip-soner/sabit yanar (bkz. common/link_led.h)

static BridgeMode currentBridge;
static AirRate    currentRate;
static uint32_t   lastHeartbeatMs = 0;
static uint32_t   lastPacketMs    = 0;
static uint32_t   lastPingSentMs  = 0;
static uint32_t   pkt_count       = 0;
static TelemetryPacket last_pkt = {};

// TX'e eslesme/LED durumu icin hafif bir PING gonderir - ACK beklenmez.
// BridgeMode'dan bagimsiz cagrilir: Serial1'e yazmak (E22 Normal modda)
// telemetriyi/senkron komutlarini bozmadan RF'e cikar (bkz. sync_command.h).
static void sendPingIfDue() {
    uint32_t now = millis();
    if (now - lastPingSentMs < PING_SEND_INTERVAL_MS) return;
    lastPingSentMs = now;

    SyncPacket ping;
    sync_build_ping(ping);
    // AUX LOW ise (E22 mesgul, ör. TX'in telemetrisiyle cakisma) bu tur
    // sessizce atlanir - bir sonraki PING_SEND_INTERVAL_MS'te tekrar denenir.
    lora_try_write(Serial1, LORA_AUX_PIN, (uint8_t*)&ping, sizeof(ping));
}

// Mevcut air rate'e göre E22'yi ve UART hızını yeniden ayarla. BridgeMode'un
// REG0 üzerinde etkisi yok (yalnızca loop()'un hangi dalını çalıştıracağını
// belirler) — o yüzden burada sadece currentRate kullanılır.
void applyProfile() {
    LoraProfile p = lora_build_profile(currentRate);
    Serial.printf("\n# [PROFIL] Bridge=%s Rate=%s\n", bridge_mode_name(currentBridge), p.name);

    bool ok = lora_apply_profile(Serial1, LORA_M0_PIN, LORA_M1_PIN, LORA_AUX_PIN, p);
    Serial1.updateBaudRate(p.uartBaud);

    if (!ok) Serial.println("# [UYARI] E22 config yaniti alinamadi");

    if (currentBridge == BRIDGE_TRANSPARENT) {
        Serial.println("# [MOD] Transparan kopru aktif - Mission Planner E22 uzerinden dogrudan konusuyor");
    } else {
        Serial.println("# [MOD] Sifreli telemetri cozme + QGC MAVLink sentezleme aktif");
    }
}

// ACK dogrulaninca cagrilir (SyncMaster icinden) - RX artik TX'in onayladigi
// profili kendi uzerinde de uygular. requestChange()'e verilen mode/rate ile
// ayni oldugu SyncMaster::handleIncoming() icinde zaten dogrulandi.
static void syncApplyCallback(BridgeMode mode, AirRate rate) {
    currentBridge = mode;
    currentRate   = rate;
    applyProfile();
}

// BRIDGE_TRANSPARENT dalinda sync_relay_with_filter()'e verilen handler - duz
// fonksiyon pointer gerektigi icin global syncMaster'a yonlendiren kucuk bir
// sarmalayici (main.cpp'deki diger callback'lerle ayni desen).
static void rxSyncPacketHandler(SyncPacketType type, BridgeMode mode, AirRate rate) {
    syncMaster.handleIncoming(type, mode, rate);
}

// ===== WebConfigPortal callback'leri =====
static BridgeMode wcGetMode() { return currentBridge; }

// ARTIK E22'yi HEMEN degistirmez - once TX'e senkronizasyon istegi gonderir
// (bkz. syncMaster.requestChange). currentBridge, yalnizca TX ACK'i
// dogrulaninca syncApplyCallback() icinde guncellenir.
static void wcSetMode(BridgeMode mode) {
    syncMaster.requestChange(mode, currentRate);
}

static AirRate wcGetAirRate() { return currentRate; }

static void wcSetAirRate(AirRate rate) {
    syncMaster.requestChange(currentBridge, rate);
}

static uint32_t wcGetPacketCount() { return pkt_count; }
static uint32_t wcGetLastEventAgoMs() { return millis() - lastPacketMs; }

// RX'e özgü link istatistikleri — OLED de AYNI linkStats/lora nesnelerini okur,
// hesaplama tek yerde (LinkStatsTracker + LoraRx::pollRssi) yapılır.
static void wcAppendExtraStatus(String& json) {
    json += ",\"rssi_dbm\":" + String(lora.getLastRssiDbm());
    json += ",\"rssi_valid\":" + String(lora.hasRssi() ? "true" : "false");
    json += ",\"rx_success_count\":" + String(linkStats.successCount());
    json += ",\"rx_lost_count\":" + String(linkStats.lostCount());
    json += ",\"rx_corrupt_count\":" + String(linkStats.corruptCount());
    json += ",\"link_efficiency_pct\":" + String(linkStats.efficiencyPct());
    if (const char* s = syncMaster.statusText()) {
        json += ",\"sync_status\":\"" + String(s) + "\"";
    }
}

void setup() {
    // Kalibrasyon modunda bu port Mission Planner'a HAM MAVLink taşıyacağı için
    // baud'u uzun menzil modundaki QGC beklentisiyle (GS_BAUD) aynı tutuyoruz;
    // Mission Planner'da COM port ayarını da GS_BAUD'da bırakabilirsiniz.
    Serial.begin(GS_BAUD);

    linkLed.begin(LED_PIN);
    pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);

    Serial1.begin(9600, SERIAL_8N1, LORA_ESP_RX_PIN, LORA_ESP_TX_PIN);
    delay(100);

    lora.attachLinkStats(linkStats);
    syncMaster.begin(Serial1, LORA_AUX_PIN, syncApplyCallback);

    // Açılışta NVS'te kayıtlı son bridge mode + air rate'i uygula (ilk açılışta
    // varsayılan: UZUN MENZIL + o bridge mode'un varsayılan air rate'i)
    currentBridge = WebConfigPortal::loadSavedMode("rxcfg", BRIDGE_COMPRESSED);
    currentRate   = WebConfigPortal::loadSavedAirRate("rxcfg", default_air_rate_for(currentBridge));
    applyProfile();

    if (!oled.begin()) {
        Serial.println("# [OLED] Baslatilamadi (bagli degil mi? SDA=GPIO11 SCL=GPIO12) - ekran gosterimi devre disi");
    }

    WebConfigCallbacks cb;
    cb.apSsid                 = "TUAV-RX-Config";
    cb.nvsNamespace            = "rxcfg";
    cb.getMode                 = wcGetMode;
    cb.setMode                 = wcSetMode;
    cb.getAirRate              = wcGetAirRate;
    cb.setAirRate              = wcSetAirRate;
    cb.getPacketCount          = wcGetPacketCount;
    cb.getLastEventAgoMs       = wcGetLastEventAgoMs;
    cb.appendExtraStatusJson   = wcAppendExtraStatus;
    webConfig.begin(cb);
}

void loop() {
    // Buton 3sn basili tutulursa web config AP'sini acar; AP aciksa DNS/HTTP servis eder.
    // Non-blocking - LoRa/MAVLink akisini bloke etmez.
    webConfig.update(CONFIG_BTN_PIN);

    // Periyodik RSSI sorgusu - kendi icinde 1-2sn araligini/kisa timeout'unu
    // yonetir, non-blocking, mevcut telemetri dinlemeyi bloklamaz.
    lora.pollRssi();

    // RX-master mod/rate senkronizasyonu: tekrar gonderim + ACK timeout'unu
    // yonetir (Serial1'e yazar, ama okumaz) - BridgeMode'dan bagimsiz her
    // turda calismali (kullanici hangi moddayken senkron baslatirsa baslatsin).
    syncMaster.tick();

    // TX'e eslesme PING'i - BridgeMode'dan bagimsiz, kendi araligini kendi yonetir.
    sendPingIfDue();

    bool paired = (millis() - lastPacketMs) < WEB_CONFIG_PAIRED_WINDOW_MS;

    if (currentBridge == BRIDGE_TRANSPARENT) {
        // ---- TRANSPARAN KÖPRÜ: PC/Mission Planner (Serial) <-> E22 ham MAVLink ----
        while (Serial.available()) Serial1.write(Serial.read());
        // TX'ten gelen ACK'i (varsa) yakalayip PC'ye sizdirmadan tuketir; geri
        // kalan her seyi (ham MAVLink) oldugu gibi Serial'e iletir - bkz.
        // sync_command.h dosya-ustu yorum (MAVLink kendi kendini senkronize eder).
        sync_relay_with_filter(Serial1, Serial, rxSyncPacketHandler);

    } else {
        // Senkronizasyon aktifse ACK'i Serial1'den ara - lora.update()'ten ONCE,
        // ki bir ACK paketi 48-byte'lik telemetri okumasiyla yanlislikla
        // karismasin (bkz. sync_command.h / sync_master.h risk analizi).
        syncMaster.scanCompressed();

        // ---- UZUN MENZİL: al -> sifre coz -> deserialize -> QGC'ye MAVLink olarak yaz ----
        TelemetryPacket pkt;
        if (lora.update(&pkt)) {
            last_pkt = pkt;
            pkt_count++;
            lastPacketMs = millis();
            paired = true;

            mav.send_telemetry(pkt);
        }

        uint32_t now = millis();
        if (now - lastHeartbeatMs >= 1000) {
            lastHeartbeatMs = now;
            mav.send_heartbeat(last_pkt);
        }
    }

    // Eslesme durumuna gore LED: eslesmemisken yanip soner, eslesince sabit yanar
    // (bkz. common/link_led.h) - non-blocking, ayni "paired" OLED'de de kullanilir.
    linkLed.update(paired);

    OledStatusData disp;
    disp.bridgeModeName = bridge_mode_oled_label(currentBridge);
    disp.airRateName    = air_rate_name(currentRate);
    disp.rssiValid      = lora.hasRssi();
    disp.rssiDbm        = lora.getLastRssiDbm();
    disp.paired         = paired;
    disp.packetCount    = pkt_count;
    disp.lostCount      = linkStats.lostCount();
    disp.corruptCount   = linkStats.corruptCount();
    disp.efficiencyPct  = linkStats.efficiencyPct();
    disp.vbatValid      = (last_pkt.header == PACKET_HEADER); // henuz paket gelmediyse gecersiz
    disp.vbatMv         = last_pkt.vbat_mv;
    oled.update(disp);
}
