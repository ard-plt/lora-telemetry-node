#include <Arduino.h>
#include "lora_tx.h"
#include "mavlink_reader.h"
#include "crypto.h"
#include "packet.h"
#include "lora_modes.h"
#include "web_config.h"
#include "sync_command.h"
#include "link_led.h"

#define LED_PIN         2
#define CONFIG_BTN_PIN  9   // momentary buton: 3sn basili tutulunca web config AP'si acilir

// NOT: MavlinkReader kendi içinde Serial2'yi kullanıyor (mavlink_reader.cpp).
// LoraTx de kendi içinde Serial1'i kullanıyor (lora_tx.cpp). Aynı UART'a
// ikinci bir HardwareSerial nesnesiyle erişmiyoruz - çakışma olur. Kalibrasyon
// modunda E22'ye ham bayt yazmak için de doğrudan global Serial1'i kullanıyoruz.

static LoraTx         lora;   // uzun menzil modunda paket göndermek için
static MavlinkReader  mav;
static WebConfigPortal webConfig;
static LinkLed        linkLed;    // eslesme durumuna gore yanip-soner/sabit yanar (bkz. common/link_led.h)

static BridgeMode currentBridge;
static AirRate    currentRate;
static uint32_t   lastSendMs         = 0;
static uint32_t   lastPingReceivedMs = 0; // RX'ten son SYNC_TYPE_PING'in alindigi an - eslesme/LED icin
static uint8_t    seq                = 0;

// Mevcut air rate'e göre E22'yi ve UART hızını yeniden ayarla. BridgeMode'un
// REG0 üzerinde etkisi yok (yalnızca loop()'un hangi dalını çalıştıracağını
// belirler) — o yüzden burada sadece currentRate kullanılır.
void applyProfile() {
    LoraProfile p = lora_build_profile(currentRate);
    Serial.printf("\n[PROFIL] Bridge=%s Rate=%s\n", bridge_mode_name(currentBridge), p.name);

    bool ok = lora_apply_profile(Serial1, LORA_M0_PIN, LORA_M1_PIN, LORA_AUX_PIN, p);
    Serial1.updateBaudRate(p.uartBaud);

    if (!ok) Serial.println("[UYARI] E22 config yaniti alinamadi, modul zaten ayarli olabilir");

    if (currentBridge == BRIDGE_TRANSPARENT) {
        Serial.println("[MOD] Transparan kopru aktif - Mission Planner tam erisim");
    } else {
        Serial.println("[MOD] Sikistirilmis+sifreli telemetri aktif");
    }
}

// ===== WebConfigPortal callback'leri =====
static BridgeMode wcGetMode() { return currentBridge; }

static void wcSetMode(BridgeMode mode) {
    currentBridge = mode;
    applyProfile();
}

static AirRate wcGetAirRate() { return currentRate; }

static void wcSetAirRate(AirRate rate) {
    currentRate = rate;
    applyProfile();
}

static uint32_t wcGetPacketCount() { return seq; }
static uint32_t wcGetLastEventAgoMs() { return millis() - lastSendMs; }

// Pixhawk'tan okunan batarya voltajını /status JSON'una ekler
static void wcAppendExtraStatus(String& json) {
    json += ",\"vbat_mv\":" + String(mav.getData().vbat_mv);
}

// RX-master senkronizasyonu: RX'ten gecerli (magic+HMAC dogrulanmis) bir
// SYNC_TYPE_CMD alindiginda cagrilir. ONCE ACK'i MEVCUT (henuz degismemis)
// profille gonderiyoruz - RX hala eski air rate/REG0'da dinlerken ACK'i
// alabilsin diye. Sira TERSI olsaydi (once profili degistirip sonra ACK
// gondermek) air-rate degisen senkronizasyonlarda ACK, RX'in artik dinlemedigi
// bir REG0/baud'da gonderilecegi icin HICBIR ZAMAN ulasmazdi (bkz.
// rx-node/lib/sync_master/sync_master.h ustundeki ayni aciklama).
static void txHandleSyncCommand(BridgeMode mode, AirRate rate) {
    SyncPacket ack;
    sync_build_packet(SYNC_TYPE_ACK, mode, rate, ack);
    Serial1.write((uint8_t*)&ack, sizeof(ack));
    Serial1.flush();

    Serial.printf("[SYNC] RX'ten komut alindi: Bridge=%s Rate=%s - ACK gonderildi, profil uygulaniyor\n",
                  bridge_mode_name(mode), air_rate_name(rate));

    currentBridge = mode;
    currentRate   = rate;
    applyProfile();
}

// sync_scan()/sync_relay_with_filter() duz fonksiyon pointer bekliyor - RX'ten
// gelen SYNC_TYPE_CMD ve SYNC_TYPE_PING'i isler (ayni HMAC dogrulamali
// mekanizmayi paylasirlar, bkz. sync_command.h), kendi urettigimiz
// SYNC_TYPE_ACK'i (normalde bu yonde hic gelmez ama savunmaci olsun diye) yoksayar.
static void txSyncPacketHandler(SyncPacketType type, BridgeMode mode, AirRate rate) {
    if (type == SYNC_TYPE_PING) {
        lastPingReceivedMs = millis(); // eslesme/LED durumu icin - ACK gerekmez
        return;
    }
    if (type != SYNC_TYPE_CMD) return;
    txHandleSyncCommand(mode, rate);
}

void setup() {
    Serial.begin(115200);

    linkLed.begin(LED_PIN);
    pinMode(CONFIG_BTN_PIN, INPUT_PULLUP);
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);

    // Pixhawk TELEM2 -> Serial2 (mav.begin() bunu içeride başlatıyor)
    mav.begin();

    // E22 UART'ı başlat (başlangıç hızı önemsiz, applyMode hemen düzeltecek)
    Serial1.begin(9600, SERIAL_8N1, LORA_ESP_RX_PIN, LORA_ESP_TX_PIN);
    delay(100);

    // Açılışta NVS'te kayıtlı son bridge mode + air rate'i uygula (ilk açılışta
    // varsayılan: UZUN MENZIL + o bridge mode'un varsayılan air rate'i)
    currentBridge = WebConfigPortal::loadSavedMode("txcfg", BRIDGE_COMPRESSED);
    currentRate   = WebConfigPortal::loadSavedAirRate("txcfg", default_air_rate_for(currentBridge));
    applyProfile();

    WebConfigCallbacks cb;
    cb.apSsid                 = "TUAV-TX-Config";
    cb.nvsNamespace            = "txcfg";
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

    // Eslesme durumuna gore LED: eslesmemisken yanip soner, eslesince sabit
    // yanar (bkz. common/link_led.h). Compressed daldaki erken "return"den
    // ETKİLENMEMESİ icin en basta, her iki BridgeMode icin ortak guncelleniyor -
    // "eslesik" = son WEB_CONFIG_PAIRED_WINDOW_MS (3sn) icinde RX'ten gecerli
    // bir PING alindi mi.
    bool paired = (millis() - lastPingReceivedMs) < WEB_CONFIG_PAIRED_WINDOW_MS;
    linkLed.update(paired);

    if (currentBridge == BRIDGE_TRANSPARENT) {
        // ---- TRANSPARAN KÖPRÜ: Pixhawk (Serial2) <-> E22 ham MAVLink ----
        while (Serial2.available()) Serial1.write(Serial2.read());
        // RX'ten gelen bir senkronizasyon komutunu Pixhawk'a sizdirmadan
        // yakalar; geri kalan her seyi (ham MAVLink) oldugu gibi Serial2'ye
        // iletir - bkz. sync_command.h dosya-ustu yorum (MAVLink kendi
        // kendini senkronize eder, yanlis pozitif akisa kalici zarar vermez).
        sync_relay_with_filter(Serial1, Serial2, txSyncPacketHandler);

    } else {
        // RX'ten bir senkronizasyon komutu gelmis mi diye bak - TX bu yonde
        // (Serial1 RX) baska hicbir seyi tuketmedigi icin (yalnizca yaziyordu)
        // surekli taramak guvenli, hicbir mevcut tuketiciyi bozmaz.
        sync_scan(Serial1, txSyncPacketHandler);

        // ---- UZUN MENZİL: parse -> pack -> sifrele -> gonder (2Hz) ----
        mav.update();

        uint32_t now = millis();
        if (now - lastSendMs < LORA_SEND_INTERVAL_MS) return;
        lastSendMs = now;

        TelemetryPacket pkt;
        pack_telemetry(&mav.getData(), &pkt, seq++);

        uint8_t enc_buf[ENCRYPTED_SIZE];
        encrypt_packet((const uint8_t*)&pkt, enc_buf, pkt.seq_id, now);

        bool ok = lora.send_raw(enc_buf, ENCRYPTED_SIZE);

        Serial.printf("[TX] seq=%u %s\n", pkt.seq_id, ok ? "gonderildi" : "AUX timeout!");
    }
}
