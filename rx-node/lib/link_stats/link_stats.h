#pragma once
#include <Arduino.h>

// ============================================================================
// RX node link istatistik muhasebesi — TelemetryPacket'in seq_id'si üzerinden
// başarı/kayıp/bozuk paket sayımı + son 30sn'lik (anlık) link verimliliği.
//
// Tek nesne, TEK yerden beslenir (LoraRx::update() içinden — bkz. lora_rx.cpp)
// ve hem OLED (common/oled_display.h) hem de web /status (common/web_config.h,
// appendExtraStatusJson callback'i) bu nesnenin getter'larını okur; hesaplama
// iki kere yazılmaz.
// ============================================================================

#define LINK_STATS_WINDOW_MS 30000UL   // verimlilik penceresi
#define LINK_STATS_BUCKET_MS 1000UL    // pencere 1sn'lik bucket'lara bölünür

class LinkStatsTracker {
public:
    // Checksum/header doğrulaması geçmiş (başarıyla decode edilmiş) her paket
    // için çağır. seq_id, TelemetryPacket.seq_id (uint8_t, 0-255 döngüsel).
    void onPacketOk(uint8_t seq_id) {
        uint32_t now = millis();

        if (_haveLastSeq) {
            // uint8_t çıkarma MODÜLER aritmetik yapar: 250'den 2'ye geçiş (gerçek
            // wraparound) ile 250'den 300'e (hipotetik) aynı sonucu verir - yani
            // "0-255 döngüsü" burada OTOMATİK doğru ele alınır, özel durum kodu
            // gerekmez. gap==1 -> ardışık paket (kayıp yok). gap>1 -> (gap-1)
            // paket kayıp. gap==0 -> aynı seq tekrar geldi, sayma.
            // BİLİNEN SINIR: seq_id 8 bit olduğundan 255'ten fazla ardışık kayıp
            // (ör. TX resetlendi, seq 0'dan yeniden başladı) yanlışlıkla küçük
            // bir kayıp gibi görünebilir - paket formatına dokunmadan (görev
            // kısıtı) bunu ayırt etmenin yolu yok, kabul edilen bir sınırlama.
            uint8_t gap = (uint8_t)(seq_id - _lastSeq);
            if (gap > 1) {
                uint32_t lost = gap - 1;
                _lostTotal += lost;
                _recordBucket(now, 0, lost);
            }
        }
        _lastSeq     = seq_id;
        _haveLastSeq = true;

        _successTotal++;
        _recordBucket(now, 1, 0);
    }

    // LoRa'dan bayt geldi ama decrypt/checksum doğrulaması başarısız oldu.
    void onCorrupt() { _corruptTotal++; }

    uint32_t successCount() const { return _successTotal; }
    uint32_t lostCount()    const { return _lostTotal; }
    uint32_t corruptCount() const { return _corruptTotal; }

    // Son 30sn penceresindeki basari/(basari+kayip) oranı (0-100). Pencerede
    // hiç veri yoksa (henüz paket gelmemiş) 0 döner.
    uint8_t efficiencyPct() {
        _rotateBuckets(millis());
        uint32_t s = 0, l = 0;
        for (int i = 0; i < kNumBuckets; i++) { s += _bucketSuccess[i]; l += _bucketLost[i]; }
        uint32_t total = s + l;
        if (total == 0) return 0;
        return (uint8_t)((s * 100UL) / total);
    }

private:
    static const int kNumBuckets = LINK_STATS_WINDOW_MS / LINK_STATS_BUCKET_MS; // 30

    void _recordBucket(uint32_t now, uint32_t success, uint32_t lost) {
        _rotateBuckets(now);
        _bucketSuccess[_curBucketIdx] += success;
        _bucketLost[_curBucketIdx]    += lost;
    }

    // "now"a karşılık gelen 1sn'lik bucket'a geçer, aradaki (kayan pencere
    // dışına çıkan) eski bucket'ları sıfırlar.
    void _rotateBuckets(uint32_t now) {
        uint32_t curBucket = now / LINK_STATS_BUCKET_MS;

        if (!_haveBucket) {
            _curBucketIdx = (int)(curBucket % (uint32_t)kNumBuckets);
            _lastBucket   = curBucket;
            _haveBucket   = true;
            return;
        }

        uint32_t diff = curBucket - _lastBucket; // aynı modüler mantık, geri sarma güvenli
        if (diff == 0) return;
        if (diff > (uint32_t)kNumBuckets) diff = (uint32_t)kNumBuckets;

        for (uint32_t i = 0; i < diff; i++) {
            _curBucketIdx = (_curBucketIdx + 1) % kNumBuckets;
            _bucketSuccess[_curBucketIdx] = 0;
            _bucketLost[_curBucketIdx]    = 0;
        }
        _lastBucket = curBucket;
    }

    uint32_t _successTotal = 0;
    uint32_t _lostTotal    = 0;
    uint32_t _corruptTotal = 0;

    bool    _haveLastSeq = false;
    uint8_t _lastSeq     = 0;

    bool     _haveBucket   = false;
    uint32_t _lastBucket   = 0;
    int      _curBucketIdx = 0;
    uint32_t _bucketSuccess[kNumBuckets] = {};
    uint32_t _bucketLost[kNumBuckets]    = {};
};
