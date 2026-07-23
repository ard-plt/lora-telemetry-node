#pragma once
#include <Arduino.h>
#include <string.h>
#include <mbedtls/md.h>
#include "lora_modes.h"
#include "secrets.h"   // WEB_CONFIG_SALT — bkz. common/secrets.h.example

#ifndef WEB_CONFIG_SALT
#error "common/secrets.h bulunamadi. common/secrets.h.example dosyasini common/secrets.h olarak kopyalayip WEB_CONFIG_SALT'i kendi tuzunuzla degistirin."
#endif

// ============================================================================
// RX-Master Mod/Rate Senkronizasyon Komutu — RX node'un web panelinden
// seçilen BridgeMode+AirRate'i TX'e RF üzerinden (Serial1 -> E22 -> hava ->
// E22 -> Serial1) bildirmesi ve TX'in bunu onaylaması (ACK) için kullanılan
// küçük, HMAC doğrulamalı paket formatı.
//
// TASARIM NOTLARI:
// - Bu paket, E22 Normal modda (M0=M1=LOW) Serial1'e yazılan HER byte gibi
//   doğrudan RF üzerinden karşı tarafa iletilir — BridgeMode'dan (transparan
//   ya da sıkıştırılmış+şifreli) bağımsız, ikisinde de AYNI ham UART/RF
//   kanalını kullanır. Bu yüzden gönderim tarafı (sync_master.h) tek bir
//   Serial1.write() ile yeterlidir; alım tarafı ise BridgeMode'a göre FARKLI
//   bir tarama stratejisi gerektirir (aşağıya bkz).
// - magic (2 byte) + HMAC-SHA256(WEB_CONFIG_SALT, ...) truncated (12 byte) ile
//   doğrulanır — aynı tuz zaten web config AP şifresi türetiminde kullanılıyor
//   (bkz. web_config.h generateApPassword), yeni bir gizli değer eklenmedi.
//   HMAC doğrulaması olmadan bu paketin AES-CTR şifreli telemetri çıktısıyla
//   (rastgele byte dizisi) ya da ham MAVLink akışıyla (0xFD/0xFE ile başlar,
//   E22 AT komutlarıyla 0xC0-0xC3 ile başlar) karışması matematiksel olarak
//   imkansız değil ama HMAC kontrolü sahte eşleşmeleri eler.
//
// ALIM TARAFI RİSK ANALİZİ (neden mod'a göre iki farklı tarama stratejisi var):
//   BRIDGE_TRANSPARENT: Serial1'deki akış ham MAVLink baytlarıdır ve MAVLink
//     KENDİ KENDİNİ senkronize eden bir çerçeveleme protokolüdür (frame magic
//     + length + CRC). sync_relay_with_filter() ilk magic byte'ı peek() eder;
//     eşleşmezse byte'ı OLDUĞU GİBİ iletir, eşleşip HMAC doğrulanamazsa
//     (yanlış pozitif, ~1/256 ihtimal — bkz. lora_rx.cpp'deki RSSI sorgusu
//     için kabul edilen aynı sınıf risk) o SYNC_PACKET_SIZE byte'ı da
//     OLDUĞU GİBİ (sırası/içeriği korunarak) iletir. MAVLink parser'ı (Pixhawk/
//     QGC) bunu görse bile CRC/length uyuşmazlığından tek bir mesajı atar ve
//     bir sonraki 0xFD/0xFE'de KENDİLİĞİNDEN yeniden senkronize olur — akışa
//     KALICI bir zarar vermez.
//   BRIDGE_COMPRESSED: Serial1'deki akış SABİT 48-byte'lık şifreli telemetri
//     bloklarıdır (ENCRYPTED_SIZE) ve bu blok sınırı hizası KENDİ KENDİNİ
//     düzeltmez (MAVLink'in aksine çerçeveleme/CRC yok, sadece sabit uzunluk).
//     Yanlış pozitif bir eşleşme yüzünden gerçek bir telemetri paketinden
//     SYNC_PACKET_SIZE kadar byte çalınırsa, sonraki TÜM 48-byte okumalar bu
//     kadar kayar ve KALICI olarak bozulur (bir daha kendiliğinden düzelmez).
//     Bu yüzden RX, sıkıştırılmış modda Serial1'i sync paketi için SÜREKLİ
//     DEĞİL, sadece kendisi aktif olarak bir senkronizasyon bekliyorken (bkz.
//     rx-node/lib/sync_master/sync_master.h SyncMaster::scanCompressed())
//     tarar — bu, maruziyet penceresini kullanıcının bir mod/rate değişikliği
//     talep ettiği birkaç saniyeyle sınırlar (RSSI sorgusunun UÇUŞ BOYUNCA
//     her 1.5sn'de bir sürekli çalışan penceresinden çok daha dar bir risk).
//   TX tarafında ise (hem transparan hem sıkıştırılmış modda) Serial1'in bu
//     yöndeki (RX->TX) trafiği ÖNCEDEN hiç kullanılmıyordu (TX sadece yazıyordu,
//     okumuyordu) — yani TX'in sürekli taraması hiçbir mevcut tüketiciyi
//     bozma riski taşımaz, sürekli/güvenle yapılabilir.
// ============================================================================

#define SYNC_MAGIC_0        0x9E
#define SYNC_MAGIC_1        0x71
#define SYNC_HMAC_LEN       12
#define SYNC_SEND_REPEAT    4          // ilk gönderimde kaç kez tekrarlanacağı (3-5 arası)
#define SYNC_RESEND_INTERVAL_MS 200UL  // tekrarlar arası bekleme
#define SYNC_ACK_TIMEOUT_MS 3000UL     // tekrarlar bitince ACK için ek bekleme

enum SyncPacketType : uint8_t {
    SYNC_TYPE_CMD  = 0x01,   // RX -> TX: yeni mod/rate talebi
    SYNC_TYPE_ACK  = 0x02,   // TX -> RX: talebi uyguladim onayi
    SYNC_TYPE_PING = 0x03    // RX -> TX: "hala buradayim" - eslesme/LED durumu icin, ACK BEKLENMEZ
};

// PING'in mode/rate alanlari ANLAMSIZDIR (kullanilmaz) - CMD/ACK ile AYNI
// sabit-boyutlu formati ve AYNI HMAC dogrulamasini paylasmak icin (boylece
// sync_scan()/sync_relay_with_filter() TEK bir SYNC_PACKET_SIZE varsayimiyla
// calismaya devam eder, TX'in ayri bir "ikinci paket boyu" tanimasina gerek
// kalmaz) yer tutucu bir gecerli deger yazilir - bkz. sync_build_ping().
struct __attribute__((packed)) SyncPacket {
    uint8_t magic0;
    uint8_t magic1;
    uint8_t type;
    uint8_t mode;
    uint8_t rate;
    uint8_t hmac[SYNC_HMAC_LEN];
};

#define SYNC_PACKET_SIZE sizeof(SyncPacket)

// Fonksiyon pointer tipi — TX/RX main.cpp'lerinde static handler'lar bu imzayla
// tanımlanır (WebConfigCallbacks'teki gibi düz fonksiyon pointer stili, lambda/
// template yok — projenin geri kalanıyla aynı desen).
typedef void (*SyncPacketHandler)(SyncPacketType type, BridgeMode mode, AirRate rate);

inline void _sync_compute_hmac(uint8_t type, uint8_t mode, uint8_t rate, uint8_t* out) {
    uint8_t msg[5] = {SYNC_MAGIC_0, SYNC_MAGIC_1, type, mode, rate};
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t full[32];
    mbedtls_md_hmac(mdInfo,
                     (const uint8_t*)WEB_CONFIG_SALT, strlen(WEB_CONFIG_SALT),
                     msg, sizeof(msg),
                     full);
    memcpy(out, full, SYNC_HMAC_LEN);
}

inline void sync_build_packet(SyncPacketType type, BridgeMode mode, AirRate rate, SyncPacket& pkt) {
    pkt.magic0 = SYNC_MAGIC_0;
    pkt.magic1 = SYNC_MAGIC_1;
    pkt.type   = (uint8_t)type;
    pkt.mode   = (uint8_t)mode;
    pkt.rate   = (uint8_t)rate;
    _sync_compute_hmac(pkt.type, pkt.mode, pkt.rate, pkt.hmac);
}

// RX'in eslesme/LED durumu icin ~1-1.5sn'de bir gonderdigi hafif "hala
// buradayim" paketi - ayrica bir ACK beklenmez. mode/rate alanlari kullanilmaz,
// gecerli (aralik-ici) sabit degerler yazilir ki sync_parse_packet() diger
// tiplerle AYNI sekilde dogrulayabilsin.
inline void sync_build_ping(SyncPacket& pkt) {
    sync_build_packet(SYNC_TYPE_PING, BRIDGE_COMPRESSED, AIR_RATE_2_4K, pkt);
}

// buf, SYNC_PACKET_SIZE byte olmalı. Magic + HMAC doğrulanırsa true döner ve
// type/mode/rate'i doldurur. mode/rate aralık dışıysa da reddedilir (bozuk paket).
inline bool sync_parse_packet(const uint8_t* buf, SyncPacketType& type, BridgeMode& mode, AirRate& rate) {
    if (buf[0] != SYNC_MAGIC_0 || buf[1] != SYNC_MAGIC_1) return false;
    if (buf[3] > (uint8_t)BRIDGE_COMPRESSED)   return false;
    if (buf[4] >= LORA_AIR_RATE_COUNT)         return false;

    uint8_t expected[SYNC_HMAC_LEN];
    _sync_compute_hmac(buf[2], buf[3], buf[4], expected);
    if (memcmp(buf + 5, expected, SYNC_HMAC_LEN) != 0) return false;

    type = (SyncPacketType)buf[2];
    mode = (BridgeMode)buf[3];
    rate = (AirRate)buf[4];
    return true;
}

// BRIDGE_COMPRESSED / TX-sürekli-dinleme için: sadece "yeterli byte VE ilk byte
// magic'e uyuyorsa" tam paketi okuyup doğrular; eşleşmezse HİÇBİR ŞEY tüketmez
// (tek tek byte okumaz) — çağıran, kendi normal tüketim döngüsünü (ör. 48-byte
// telemetri okuma) bu fonksiyondan SONRA çalıştırmalı ki eşleşmeyen veri ona
// kalsın. BridgeMode/risk ayrımı için dosya üstündeki yorum bloğuna bakın.
inline void sync_scan(Stream& in, SyncPacketHandler handler) {
    if (in.available() < (int)SYNC_PACKET_SIZE) return;
    if (in.peek() != SYNC_MAGIC_0) return;

    uint8_t buf[SYNC_PACKET_SIZE];
    in.readBytes(buf, SYNC_PACKET_SIZE);

    SyncPacketType type; BridgeMode mode; AirRate rate;
    if (sync_parse_packet(buf, type, mode, rate)) handler(type, mode, rate);
    // Doğrulama başarısızsa (yanlış pozitif magic) bu byte'lar burada TÜKETİLMİŞ
    // olur — sync_scan() sadece bunun zararsız olduğu yerlerde (TX'in hiç
    // dinlemediği yön, ya da RX'in sınırlı aktif-senkronizasyon penceresi)
    // çağrılmalıdır, ham MAVLink akışı gibi başka bir tüketicisi olan bir
    // akışta DEĞİL (onun için sync_relay_with_filter kullanılır).
}

// BRIDGE_TRANSPARENT için: in'den out'a byte aktarırken, magic+HMAC doğrulanan
// bir sync paketi tespit edilirse onu TÜKETİR (out'a iletmez, handler'a verir);
// yanlış pozitif (magic eşleşti ama HMAC tutmadı) durumunda okunan byte'ları
// OLDUĞU GİBİ (sırası bozulmadan) out'a yazar — MAVLink kendi kendini
// senkronize ettiği için bu güvenlidir (bkz. dosya üstü yorum).
inline void sync_relay_with_filter(Stream& in, Stream& out, SyncPacketHandler handler) {
    while (in.available()) {
        if (in.available() >= (int)SYNC_PACKET_SIZE && in.peek() == SYNC_MAGIC_0) {
            uint8_t buf[SYNC_PACKET_SIZE];
            in.readBytes(buf, SYNC_PACKET_SIZE);

            SyncPacketType type; BridgeMode mode; AirRate rate;
            if (sync_parse_packet(buf, type, mode, rate)) {
                handler(type, mode, rate);
            } else {
                out.write(buf, SYNC_PACKET_SIZE);
            }
        } else {
            out.write((uint8_t)in.read());
        }
    }
}
