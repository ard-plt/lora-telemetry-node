# Breadboard Bağlantı Rehberi — ESP32 + E22-900T30D

## Önemli: E22 Güç Hattı

E22-900T30D TX anında **~500mA** akım çeker.
ESP32'nin 3.3V pininden BESLEME — E22 VCC direkt ESP32 3V3'e BAĞLAMA.

**Doğru güç yapısı:**
```
USB (5V) veya LiPo
    │
    ├── ESP32 5V (VIN pini) ──→ ESP32 dahili regülatör ──→ ESP32 çalışır
    │
    └── AMS1117-3.3 LDO (ayrı)
            │ IN: 5V
            │ OUT: 3.3V (1A kapasiteli)
            └──→ E22 VCC
                 (GND → ortak GND hattına)
```

Eğer AMS1117 yoksa geçici test için: ESP32 3V3 pininden besle ama
TX gücü maksimum **17 dBm'e düşür** (E22 register ile) — aksi halde
ESP32 regülatörü aşırı ısınır / reset atar.

---

## E22-900T30D Pin Düzeni

E22'nin modül üzerindeki pin sırası (tipik EBYTE layout, 2×8 pin):

```
       [ Anten ]
  ┌─────────────┐
  │  VCC   GND  │
  │  TXD   RXD  │
  │  AUX   M0   │
  │  M1    ...  │
  └─────────────┘
```

> **Datasheet kontrolü:** Elindeki E22'nin üzerindeki pin etiketlerine bak.
> TXD = E22'nin veri çıkışı → ESP32'nin RX pinine bağlanır.
> RXD = E22'nin veri girişi → ESP32'nin TX pinine bağlanır.

---

## Config Butonu + Web UI (KALİBRASYON / UZUN MENZİL)

Her iki node'da E22'nin çalışma modu artık fiziksel bir SPDT switch ile değil,
GPIO9'a bağlı **momentary bir buton** ve buradan açılan bir web arayüzü ile
seçiliyor. GPIO9 `INPUT_PULLUP` olarak ayarlı — harici direnç gerekmez, butonu
doğrudan GPIO9 ↔ GND arasına bağla (basınca kısa devre, bırakınca açık).

| Davranış                          | Sonuç |
|-----------------------------------|-------|
| Normal açılış (buton basılı değil) | NVS'te (`Preferences`) en son kaydedilen mod otomatik uygulanır, WiFi kapalı, LoRa relay loop'u hemen çalışır |
| Buton 3sn basılı tutulur           | `WiFi.softAP()` ile WPA2 şifreli config AP'si açılır, captive portal (DNSServer + WebServer) başlar |
| Web UI'de mod seçimi               | E22 çalışma zamanında yeniden yapılandırılır (canlı test), NVS'e henüz yazılmaz |
| "Uçuşa Hazır / Kaydet ve Kapat"    | Seçili mod NVS'e kaydedilir, AP kapanır, node normal moda döner |
| AP açıkken 5dk hiç istek gelmezse  | AP otomatik kapanır, normal moda döner (watchdog) |

| Mod (BridgeMode)  | Varsayılan E22 UART Baud | Varsayılan Air Rate |
|-------------------|--------------------------|----------------------|
| KALİBRASYON (BRIDGE_TRANSPARENT) | 115200 | 62.5kbps |
| UZUN MENZİL (BRIDGE_COMPRESSED)  | 9600   | 2.4kbps  |

KALİBRASYON modunda node transparan köprüye geçer (Mission Planner E22
üzerinden Pixhawk'a doğrudan erişir); UZUN MENZİL modu uçuş sırasında
kullanılacak asıl şifreli telemetri modudur. **Bridge mode (yukarıdaki
tablo) ve air rate artık BAĞIMSIZ seçilir** — web UI'daki air-rate
dropdown'undan 0.3/1.2/2.4/4.8/9.6/19.2/38.4/62.5 kbps arasından herhangi
biri, seçili bridge mode'dan bağımsız olarak seçilebilir; yukarıdaki tablo
sadece hiç değiştirilmemişse (ilk açılış) geçerli varsayılanı gösterir.
**TX ve RX'in konuşabilmesi için ikisinin de aynı air rate'te olması
gerekir** — bu LoRa'nın fiziksel katman kısıtıdır. RX'in web panelinden
yapılan değişiklikler artık TX'i RF üzerinden (ek kablolama YOK — aynı
E22↔E22 telemetri hattı üzerinden) otomatik senkronize eder: RX önce eski
profille bir komut gönderir, TX ACK'lerse RX de kendi profilini uygular; TX
yanıt vermezse (menzil dışı vb.) RX kendi modunu değiştirmez ve panelde
hata gösterir. Ayrıntı ve paket formatı: `common/sync_command.h`,
`rx-node/lib/sync_master/sync_master.h`, ana `CLAUDE.md`'deki **RX-Master
Mod/Rate Senkronizasyonu** bölümü. TX'in panelinden DOĞRUDAN yapılan bir
değişiklik bu mekanizmayı TETİKLEMEZ (RX'e senkronize edilmez). Profil
detayı: `common/lora_modes.h` (`BridgeMode`, `AirRate`,
`lora_build_profile`). Web config paneli detayı: `common/web_config.h` ve
ana `CLAUDE.md`'deki **Web Config Paneli** / **Air Data Rate** bölümleri.

E22 REG1 bit5 (RSSI ambient noise enable, `LORA_REG1_VALUE`) her iki node'da
da her zaman açık tutulur — RX node'un periyodik RSSI sorgusu (bkz. **Link
İstatistikleri & OLED**, aşağıda) bu bite bağımlıdır; TX'te bu bitin bir
etkisi yoktur (zararsız).

**Güvenlik notu:** Config AP'si sadece buton 3sn basılı tutulunca açılır ve
WPA2 ile şifrelidir. Fabrika varsayılan şifresi **tüm cihazlarda aynıdır**
(`tuav-loralink`) — kapalı/kontrollü ortamlarda (TEKNOFEST sahası gibi)
bilinçli olarak kabul edilmiş bir tercihtir. Açık/ticari dağıtımda web
panelindeki "AP Şifresini Değiştir" alanından (min 8 karakter) kendi
şifrenizi belirlemeniz ŞİDDETLE önerilir — aksi halde herkes aynı şifreyle
bağlanabilir; yeni şifre NVS'e yazılır ve bir sonraki AP açılışında geçerli
olur. `common/secrets.h`'deki `WEB_CONFIG_SALT` artık AP şifresiyle İLGİLİ
DEĞİLDİR — yalnızca **RX-Master Mod/Rate Senkronizasyonu** komut paketinin
HMAC doğrulamasında kullanılır (bkz. `CLAUDE.md`); yine de repoya commit
edilmez, kurulumdan önce `common/secrets.h.example`'ı kopyalayıp kendi
rastgele tuzunuzu üretin (`openssl rand -hex 32`). Uçuş/telemetri sırasında
WiFi tamamen kapalıdır. AP'nin kimlik doğrulaması/HTTPS/CSRF koruması yoktur;
tasarım fiziksel erişimi olan güvenilir bir operatör varsayar.

---

## Eşleşme LED'i (GPIO2, her iki node)

Her iki node'daki dahili LED (GPIO2) artık **eşleşme durumunu** gösterir —
eski "her paket alımında/gönderiminde kısa yanıp sönme" davranışı kaldırıldı:

| LED durumu | Anlamı |
|------------|--------|
| ~300ms periyotla yanıp sönüyor | Eşleşmemiş — karşı taraftan (RX: telemetri, TX: PING) son 3sn'de veri gelmedi, "aranıyor" |
| Sabit yanıyor | Eşleşik — link açık |

RX kendi eşleşme durumunu zaten bildiği telemetri akışından (son başarılı
paketin zamanı) çıkarır. TX'in bunu bilebilmesi için RX, ~1-1.5 saniyede bir
hafif bir `SYNC_TYPE_PING` paketi gönderir (ek donanım/kablolama YOK — aynı
E22↔E22 hattı üzerinden, RX-Master senkronizasyon komutlarıyla AYNI
HMAC-doğrulamalı mekanizmayı paylaşır); TX bunu pasif dinler, ACK göndermez.
Ayrıntı: `common/sync_command.h` (`SYNC_TYPE_PING`), `common/link_led.h`
(`LinkLed`), ana `CLAUDE.md`'deki **Eşleşme PING'i ve LED Durumu** bölümü.

---

## TX Node Bağlantıları

TX node: E22 + ESP32-S3 + Pixhawk (TELEM2)

> Not: ESP32-S3-WROOM-1'de GPIO 26-37 dahili Flash/PSRAM'a ayrılmış, kullanılamaz.

### E22 ↔ ESP32-S3 (UART1)

| E22 Pin | ESP32 Pin | Not                          |
|---------|-----------|------------------------------|
| VCC     | —         | Ayrı AMS1117-3.3 çıkışına    |
| GND     | GND       | Ortak GND hattı              |
| TXD     | GPIO4     | E22 çıkış → ESP32 UART1 RX  |
| RXD     | GPIO5     | E22 giriş ← ESP32 UART1 TX  |
| AUX     | GPIO6     | Modül hazır sinyali (INPUT)  |
| M0      | GPIO7     | Mod seçimi (OUTPUT)          |
| M1      | GPIO8     | Mod seçimi (OUTPUT)          |

### Config Butonu ↔ ESP32-S3

| Buton Pin | ESP32 Pin | Not                                       |
|-----------|-----------|-------------------------------------------|
| Bacak 1   | GPIO9     | INPUT_PULLUP — harici direnç yok          |
| Bacak 2   | GND       | Basınca kısa devre — 3sn basılı tutma web config AP'sini açar |

### Pixhawk TELEM2 ↔ ESP32-S3 (UART2)

| Pixhawk TELEM2 | ESP32 Pin | Not                         |
|----------------|-----------|-----------------------------|
| TX             | GPIO16    | Pixhawk çıkış → ESP32 RX   |
| RX             | GPIO17    | Pixhawk giriş ← ESP32 TX   |
| GND            | GND       | Ortak GND — MUTLAKA bağla  |
| VCC (5V)       | —         | Bağlama, ESP32'yi ayrı besle|

---

## RX Node Bağlantıları

RX node: E22 + ESP32-S3 native USB

> Not: ESP32-S3-WROOM-1'de GPIO 26-37 dahili Flash/PSRAM'a ayrılmış, kullanılamaz.

### E22 ↔ ESP32-S3 (UART1)

| E22 Pin | ESP32 Pin | Not                          |
|---------|-----------|------------------------------|
| VCC     | —         | Ayrı AMS1117-3.3 çıkışına    |
| GND     | GND       | Ortak GND hattı              |
| TXD     | GPIO4     | E22 çıkış → ESP32 UART1 RX  |
| RXD     | GPIO5     | E22 giriş ← ESP32 UART1 TX  |
| AUX     | GPIO6     | Modül hazır sinyali (INPUT)  |
| M0      | GPIO7     | Mod seçimi (OUTPUT)          |
| M1      | GPIO8     | Mod seçimi (OUTPUT)          |

### Config Butonu ↔ ESP32-S3

| Buton Pin | ESP32 Pin | Not                                       |
|-----------|-----------|-------------------------------------------|
| Bacak 1   | GPIO9     | INPUT_PULLUP — harici direnç yok          |
| Bacak 2   | GND       | Basınca kısa devre — 3sn basılı tutma web config AP'sini açar |

### OLED (SSD1306 128x64 I2C) ↔ ESP32-S3 — sadece RX node

| OLED Pin | ESP32 Pin | Not                                  |
|----------|-----------|----------------------------------------|
| VCC      | 3V3       | SSD1306 modülleri genelde 3.3V toleranslı (modülünün etiketini kontrol et) |
| GND      | GND       | Ortak GND hattı                        |
| SDA      | GPIO11    | I2C veri hattı                         |
| SCL      | GPIO12    | I2C saat hattı                         |

**Çakışma kontrolü:** GPIO11/12, RX node'da kullanımdaki diğer pinlerle
(2,4,5,6,7,8,9) çakışmıyor; ESP32-S3-WROOM-1'in Flash/PSRAM için ayırdığı
GPIO26-37 aralığının dışında; S3'ün strapping pinleri (GPIO0/3/45/46)
arasında da değil — I2C için güvenle kullanılabilir. I2C adresi çoğu 128x64
SSD1306 modülünde `0x3C`'tir (bazılarında `0x3D` — modülün üzerindeki
etikete/satıcı sayfasına bak, uymazsa `common/oled_display.h`'deki
`OLED_I2C_ADDR`'ı güncelle).

### ESP32-S3 native USB → PC (QGC çıkışı, birincil yöntem)

RX node `platformio.ini`'de `board = esp32s3box` kullanıyor; bu board tanımı
derleme zamanında `-DARDUINO_USB_CDC_ON_BOOT=1` bayrağını otomatik ekliyor.
Bu ayar aktifken kodda kullanılan `Serial` nesnesi **fiziksel UART0 pinlerine
(GPIO1/GPIO3) değil**, ESP32-S3'ün dahili USB-OTG çevre birimine (native USB
CDC) map olur. Yani PC bağlantısı doğrudan ESP32-S3'ün USB konnektöründen
yapılır — ayrı bir USB-UART köprü çipine gerek yoktur.

| Bağlantı        | Not                                             |
|-----------------|--------------------------------------------------|
| ESP32-S3 USB portu | PC'ye doğrudan USB kablosuyla bağla            |
| GND             | USB kablosu üzerinden zaten ortak                |

QGC/Mission Planner tarafında bu bağlantı PC'de sanal bir COM/ttyACM portu
olarak görünür; baud rate ayarı yine `GS_BAUD` (57600) ile eşleşmeli.

### (Opsiyonel/yedek) ESP32 ↔ FT232 USB-UART

Native USB portu kullanılamıyorsa (örn. debug sırasında `Serial` başka bir
amaçla meşgulse) yedek olarak harici FT232 üzerinden UART0 pinleri de
kullanılabilir — ancak bu artık birincil yöntem değildir ve `ARDUINO_USB_CDC_ON_BOOT=1`
aktifken `Serial.begin()` bu pinlere çıkmaz; UART0'a doğrudan erişmek için
koddan `Serial0` nesnesi kullanılmalıdır.

| ESP32 Pin | FT232 Pin | Not                              |
|-----------|-----------|----------------------------------|
| GPIO1 (TX0) | RXD     | Serial0 → FT232 giriş            |
| GPIO3 (RX0) | TXD     | (isteğe bağlı, tek yön yeterli) |
| GND       | GND       | Ortak GND                        |

---

## Breadboard Kurulum Sırası

1. **GND hattı:** Breadboard'un (-) rayını ESP32 GND, E22 GND (ve kullanılıyorsa FT232 GND) ile birleştir
2. **Güç:** AMS1117-3.3 çıkışını E22 VCC'ye bağla; ESP32'yi USB veya VIN'den besle
3. **M0/M1:** GPIO7 → M0, GPIO8 → M1 (kod modu değiştirirken otomatik sürer, pull-down direnci gerekmez)
4. **AUX:** GPIO6 → AUX (seri direnci yok, doğrudan bağla)
5. **UART:** TXD/RXD çapraz bağla (TX→RX, RX→TX)
6. **Config Butonu:** GPIO9 ↔ GND arasına momentary buton bağla (INPUT_PULLUP, harici direnç yok)
7. **(Sadece RX) OLED:** SDA→GPIO11, SCL→GPIO12, VCC→3V3, GND→GND
8. Programla, Serial monitörde `[LoRa] TX/RX hazir` mesajını bekle (RX'te OLED bağlıysa ekranda mod/RSSI/istatistik görünür)

---

## Kontrol Listesi

- [ ] E22 VCC ayrı LDO'dan besleniyor (ESP32 3V3'ten değil)
- [ ] GND hatları birleşik (ESP32 + E22 + LDO, FT232 kullanılıyorsa o da dahil)
- [ ] TXD/RXD çapraz bağlı (E22 TXD → ESP32 RX pini)
- [ ] M0, M1 doğru ESP32 pinlerine bağlı
- [ ] AUX bağlı (bağlanmazsa kod AUX timeout hatası verir)
- [ ] Config butonu GPIO9 ↔ GND arasına bağlı (momentary — sürekli kısa devre BIRAKMA, aksi halde her açılışta config AP'si tetiklenir)
- [ ] (Sadece RX) OLED SDA/SCL sırasıyla GPIO11/GPIO12'ye bağlı, VCC 3V3'ten besleniyor
- [ ] Anten E22'ye takılı (antensiz TX yapma — modül hasar görebilir)

---

## Beklenen Serial Çıktısı (başarılı kurulum)

**TX node:**
```
[LoRa] TX hazir | 868 MHz, 2.4kbps air rate
[MAVLink] Baslaniyor...
[TX] seq=1 gonderildi
[TX] seq=2 gonderildi
```

**RX node:**
```
# LoRa Telemetry RX Node starting...
# [LoRa] RX hazir | 868 MHz, 2.4kbps — dinleniyor...
# [RX] Paket alindi seq=1
```

OLED bağlıysa ekranda: bridge mode + air rate, `RSSI: -72 dBm` (+ sinyal
çubuğu), `Durum: BAGLI`/`ARANIYOR`, `Pkt:N Kayip:N`, `Bozuk:N Verim:%N`,
sağ üstte batarya voltajı görünür. OLED bağlı değilse/yanıt vermezse Serial'da
tek satırlık bir uyarı basılır (`# [OLED] Baslatilamadi...`) ve kod OLED'siz
çalışmaya devam eder — diğer işlevsellik etkilenmez.
