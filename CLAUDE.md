# LoRa Telemetry Node — CLAUDE.md

## Proje Tanımı

Pixhawk'tan MAVLink 2 üzerinden gelen telemetri verisini ESP32-WROOM-32
ile alıp EBYTE E22-900T30D (LoRa 868MHz) üzerinden yer istasyonuna
ileten uçtan uca telemetri sistemi.

Veri kaynağı Pixhawk'tır. ESP32 sensör okumaz — sadece MAVLink parse
eder, paketi serialize eder ve LoRa ile iletir.

---

## Sistem Mimarisi

```
[Pixhawk TELEM2] --UART MAVLink2--> [ESP32 TX Node] --LoRa 868MHz--> [ESP32 RX Node] --USB UART--> [PC Python GS]
```

---

## Donanım

### TX Node (Hava)
- MCU: ESP32-WROOM-32
- LoRa: EBYTE E22-900T30D (SX1262, 868MHz, 30dBm)
- Pixhawk bağlantısı: TELEM2 portu → ESP32 UART2 (MAVLink 2, 57600 baud)

### RX Node (Yer)
- MCU: ESP32-WROOM-32
- LoRa: EBYTE E22-900T30D (SX1262, 868MHz, 30dBm)
- Çıkış: USB-UART (FT232) → PC, 115200 baud

### Kritik — E22-900T30D Güç Hattı
TX anında ~500mA akım çeker. ESP32'nin 3.3V regülatörüne BAĞLAMA.
- E22 VCC → ayrı AMS1117-3.3 LDO (1A rated)
- ESP32 GND ve E22 GND ortak olmalı

---

## Pin Mapping — ESP32-WROOM-32

### TX Node
| Bağlantı           | ESP32 Pin | Protokol     |
|--------------------|-----------|--------------|
| E22 TXD            | GPIO33    | UART1 RX     |
| E22 RXD            | GPIO32    | UART1 TX     |
| E22 AUX            | GPIO27    | GPIO (INPUT) |
| E22 M0             | GPIO26    | GPIO (OUTPUT)|
| E22 M1             | GPIO25    | GPIO (OUTPUT)|
| Pixhawk TELEM2 TX  | GPIO16    | UART2 RX     |
| Pixhawk TELEM2 RX  | GPIO17    | UART2 TX     |
| Pixhawk GND        | GND       | Ortak GND    |

### RX Node
| Bağlantı  | ESP32 Pin | Protokol     |
|-----------|-----------|--------------|
| E22 TXD   | GPIO16    | UART1 RX     |
| E22 RXD   | GPIO17    | UART1 TX     |
| E22 AUX   | GPIO27    | GPIO (INPUT) |
| E22 M0    | GPIO26    | GPIO (OUTPUT)|
| E22 M1    | GPIO25    | GPIO (OUTPUT)|

E22 M0=LOW, M1=LOW → Normal TX/RX modu (kod setup'ta otomatik set eder).
E22 ↔ ESP32 arasında UART baud = 9600 (E22 varsayılanı).

---

## MAVLink Mesajları — Kullanılacaklar

| MAVLink MSG ID | Mesaj Adı            | Alınan Alanlar                        |
|----------------|----------------------|---------------------------------------|
| #33            | GLOBAL_POSITION_INT  | lat, lon, alt, relative_alt, vx, vy, vz, hdg |
| #30            | ATTITUDE             | roll, pitch, yaw, rollspeed, pitchspeed, yawspeed |
| #74            | VFR_HUD              | airspeed, groundspeed, heading, throttle, alt, climb |
| #1             | SYS_STATUS           | voltage_battery, current_battery, battery_remaining |
| #216           | SCALED_IMU3 / #27 RAW_IMU | xacc, yacc, zacc, xgyro, ygyro, zgyro |
| #132           | DISTANCE_SENSOR      | current_distance (lidar) |
| #147           | BATTERY_STATUS       | voltages[], current_battery, battery_remaining |
| #0             | HEARTBEAT            | base_mode, custom_mode (arm + uçuş modu) |

Pixhawk TELEM2 portu varsayılan olarak bu mesajları 4–10 Hz aralığında yayar.
Ek mesaj istenirse MAVLink `REQUEST_DATA_STREAM` komutu gönderilebilir.

---

## Paket Formatı (Binary, 40 byte)

Tüm Pixhawk verisini tek bir LoRa paketine sıkıştırıyoruz.

```
Offset  Len  Type    Alan
0       1    uint8   header (0xAD sabit)
1       1    uint8   seq_id
2       4    int32   latitude    (degE7, örn. 39.9° → 399000000)
6       4    int32   longitude   (degE7)
10      4    int32   altitude_mm (mm cinsinden MSL)
14      2    int16   relative_alt_cm
16      2    int16   groundspeed_cms
18      2    int16   airspeed_cms  (pitot)
20      2    int16   heading_cd  (centidegree, 0–36000)
22      2    int16   roll_cd     (centidegree)
24      2    int16   pitch_cd    (centidegree)
26      2    int16   yaw_cd      (centidegree)
28      2    int16   climb_rate_cms
30      2    uint16  vbat_mv
32      2    int16   current_da  (deciamps, x10)
34      2    uint16  lidar_cm    (0xFFFF = geçersiz)
36      1    uint8   battery_pct
37      1    uint8   flight_mode
38      1    uint8   armed       (0=disarmed, 1=armed)
39      1    uint8   checksum    (XOR of bytes 0–38)
```

---

## LoRa Parametreleri

```
Frekans   : 868.0 MHz
SF        : 9
Bandwidth : 125 kHz
CR        : 4/5
Preamble  : 8
TX Power  : 30 dBm
Gönderim  : 2 Hz (her 500ms)
```

40 byte @ SF9 → airtime ~180ms. 2 Hz güvenli, çakışma yok.

---

## Geliştirme Aşamaları

Sırayı koru. Her aşamayı bitirmeden sonrakine geçme.

### Aşama 1 — PlatformIO İskeleti
Yapılacaklar:
- `lora-telemetry-node/` altında `tx-node/` ve `rx-node/`
- Her biri için `platformio.ini` (board: esp32dev, framework: arduino)
- `common/packet.h` — TelemetryPacket struct ve tüm sabitler
- `common/` her iki projeye dahil
- `.gitignore`, `README.md` iskeleti

Bitti kriteri: `pio run` her iki projede hatasız derleniyor.

### Aşama 2 — MAVLink Parse Katmanı
Yapılacaklar:
- `lib/mavlink/mavlink_reader.h` ve `mavlink_reader.cpp`
- `mavlink2` Arduino kütüphanesi (platformio: `mavlink/mavlink`)
- UART2'den byte byte okuma, `mavlink_parse_char()` ile frame tespiti
- Şu mesajları parse et ve `MavlinkData` struct'a doldur:
  - HEARTBEAT (#0) → armed, flight_mode
  - GLOBAL_POSITION_INT (#33) → lat, lon, alt, relative_alt, vx, vy, hdg
  - ATTITUDE (#30) → roll, pitch, yaw
  - VFR_HUD (#74) → airspeed, groundspeed, climb
  - SYS_STATUS (#1) → vbat, current, battery_pct
  - DISTANCE_SENSOR (#132) → lidar_cm
- Her mesaj geldiğinde ilgili alanı güncelle, diğerleri eski değerini korusun
- Serial'da 1 Hz özet log: tüm alanlar

Bitti kriteri: Pixhawk bağlıyken Serial monitörde gerçek veriler akıyor.
Pixhawk yoksa: SITL veya `mavlink_generator` scripti ile simüle et (Aşama 2b).

### Aşama 2b — MAVLink Simülatör (Pixhawk olmadan test)
Yapılacaklar:
- `tools/mavlink_sim.py` — Python scripti
- pyserial ile ESP32 UART2'ye bağlan
- Gerçekçi GLOBAL_POSITION_INT, ATTITUDE, SYS_STATUS, HEARTBEAT mesajları üret
- 4 Hz gönderim (Pixhawk davranışını taklit et)

Bitti kriteri: simülatör çalışırken Aşama 2 parse kodu doğru veri üretiyor.

### Aşama 3 — Paket Serialize / Deserialize
Yapılacaklar:
- `common/packet.h` içinde `pack_telemetry(MavlinkData*, TelemetryPacket*)`
  ve `unpack_telemetry(uint8_t*, TelemetryPacket*)` fonksiyonları
- XOR checksum hesaplama ve doğrulama
- Loopback test: serialize → deserialize, tüm alanlar eşleşiyor

Bitti kriteri: loopback test Serial'da geçiyor, checksum hatası yok.

### Aşama 4 — LoRa TX
Yapılacaklar:
- `RadioLib` kütüphanesi, SX1262 driver
- `lib/lora/lora_tx.h` ve `lora_tx.cpp`
- `send_packet(TelemetryPacket*)` fonksiyonu
- M0=LOW, M1=LOW (Normal mod) setup'ta set et
- 2 Hz gönderim döngüsü (her 500ms)
- serialize → encrypt → 48 byte LoRa gönder
- Serial'da "sent seq=X (encrypted)" logu

Bitti kriteri: Serial'da her 500ms "packet sent" yazıyor.

### Aşama 4.5 — AES-128 Şifreleme Katmanı
Yapılacaklar:
- `common/crypto.h` — AES_KEY sabiti ve nonce üreteci
- `lib/crypto/crypto.h` ve `crypto.cpp`
- `encrypt_packet(uint8_t* plain, uint8_t* out, uint8_t seq_id)` — 40 → 48 byte
- `decrypt_packet(uint8_t* in, uint8_t* plain)` — 48 → 40 byte
- mbedtls AES-CTR kullan (`mbedtls/aes.h`)
- Loopback test: encrypt → decrypt → orijinal veriyle karşılaştır

Bitti kriteri: encrypt/decrypt loopback testi Serial'da geçiyor.

### Aşama 5 — LoRa RX
Yapılacaklar:
- `lib/lora/lora_rx.h` ve `lora_rx.cpp`
- Aynı LoRa parametreleriyle sürekli dinleme
- Gelen 48 byte → decrypt → 40 byte → deserialize + checksum doğrula
- Başarılı paketi USB-UART'a şu formatta yaz:
  `$TEL,seq,lat,lon,alt,ralt,gspd,aspd,hdg,roll,pitch,yaw,climb,vbat,curr,lidar,bpct,mode,armed\n`
- RSSI ve SNR değerlerini de logla
- Checksum hata sayacı görünsün

Bitti kriteri: TX gönderince RX Serial'da decode edilmiş satır çıkıyor.

### Aşama 6 — QGroundControl Ground Station
RX node zaten MAVLink 2 stream çıkışı yapıyor (`mavlink_tx` kütüphanesi).
QGroundControl doğrudan bağlanır — custom Python yazımına gerek yok.

Yapılacaklar:
- RX node USB-UART (FT232) → PC, **57600 baud**
- QGroundControl → Application Settings → Comm Links → Add:
  - Type: Serial
  - Port: RX node'un COM/ttyUSB portu
  - Baud Rate: 57600
- RX node gönderdiği MAVLink mesajları:
  - HEARTBEAT (#0) — 1 Hz, QGC bağlantı kopmasın diye
  - GLOBAL_POSITION_INT (#33) — GPS harita görünümü
  - ATTITUDE (#30) — yapay ufuk (artificial horizon)
  - VFR_HUD (#74) — airspeed, groundspeed, climb rate, throttle
  - SYS_STATUS (#1) — batarya voltaj & akım
  - BATTERY_STATUS (#147) — batarya yüzdesi
- QGC'de görünecekler:
  - Harita üzerinde canlı konum
  - HUD: roll, pitch, yaw, airspeed, altitude
  - Batarya göstergesi
  - Armed / uçuş modu
- `docs/qgc_setup.md` — bağlantı kurulum adımları (port, baud, test)

Bitti kriteri: TX-RX çalışırken QGC haritada canlı konum ve HUD gösteriyor.

### Aşama 7 — SF Karşılaştırma & Link Budget
Yapılacaklar:
- SF7 / SF9 / SF12 paket kayıp oranı ve airtime ölç
- `docs/link_budget.md`: Friis denklemi ile teorik menzil,
  E22-900T30D (30dBm TX, -148dBm sensitivite) parametreleriyle hesap
- Kullanılacak SF'i karara bağla

Bitti kriteri: tablo ve hesap doldurulmuş.

### Aşama 8 — GitHub Hazırlığı
Yapılacaklar:
- README.md: mimari diyagram, güç uyarısı, pin tablosu,
  MAVLink mesaj tablosu, paket format tablosu, kurulum
- `docs/link_budget.md`
- `hardware/` bağlantı şeması (Fritzing / KiCad)
- MIT lisans

Bitti kriteri: yabancı biri README'yi okuyarak kurabilir.

---

## Şifreleme — AES-128 CTR

ESP32'nin hardware AES accelerator'ı kullanılır (mbedTLS üzerinden).
40 byte plaintext → 40 byte ciphertext (CTR mod — padding yok, boyut değişmez).

### Tasarım

```
[TelemetryPacket 40 byte]
        ↓
  AES-128-CTR encrypt
  key: 16 byte sabit
  nonce: seq_id + timestamp (8 byte)
        ↓
[Encrypted payload 40 byte] → LoRa TX
```

CTR modu seçildi çünkü:
- Padding gerektirmez → paket boyutu sabit kalır
- Her pakette farklı nonce (seq_id tabanlı) → replay saldırısına karşı koruma
- Blok şifre olmadığı için kısmi paket bile decrypt edilebilir

### Anahtar Tanımı (`common/crypto.h`)

```cpp
// Her iki ESP'ye aynı key yüklenecek
// Üretim öncesi değiştir — bu örnek değer
static const uint8_t AES_KEY[16] = {
    0x2D, 0x4A, 0x6F, 0x8B, 0x1C, 0x3E, 0x5F, 0x7A,
    0x9D, 0xBE, 0xCF, 0xE0, 0x12, 0x34, 0x56, 0x78
};

// Nonce: [seq_id (1 byte) | millis() low 7 byte] = 8 byte
// AES-CTR için 16 byte IV → nonce + 8 byte sıfır padding
```

### Paket Yapısı (şifreli, 48 byte toplam)

```
Offset  Len  Alan
0       8    Nonce (seq_id + millis, plaintext — IV oluşturmak için)
8       40   AES-128-CTR encrypted TelemetryPacket
```

Nonce plaintext gider — bu normaldir, CTR modda gizli olmasına gerek yok.
Anahtar gizlidir, nonce tekrar etmediği sürece güvenli.

### Decrypt (RX tarafı)
1. İlk 8 byte nonce oku
2. Nonce + 8 byte sıfır → 16 byte IV oluştur
3. AES-128-CTR decrypt → 40 byte TelemetryPacket
4. Checksum doğrula

### Geliştirme Aşaması — Aşama 4.5 (TX-RX arasına girer)
TX: serialize → encrypt → LoRa gönder
RX: LoRa al → decrypt → deserialize → checksum doğrula

Kütüphane: `mbedtls/aes.h` (ESP-IDF ile birlikte gelir, ekstra bağımlılık yok)

---

## Kütüphane Tercihleri

| Görev          | Kütüphane        | platformio.ini lib_deps      |
|----------------|------------------|------------------------------|
| LoRa (SX1262)  | RadioLib         | `jgromes/RadioLib`           |
| MAVLink parse  | mavlink          | `mavlink/mavlink`            |
| Python serial  | pyserial         | `pip install pyserial`       |
| Python plot    | matplotlib       | `pip install matplotlib`     |

---

## Kod Standartları

- C++: `.h` / `.cpp` çiftleri, her modül `lib/` altında kendi klasöründe
- Magic number yok — tüm sabitler `common/packet.h`'de
- Her fonksiyon üzerinde tek satır yorum
- Aşama bitmeden bir sonrakine geçme
- Her aşama sonunda: `git commit -m "feat: aşama X tamamlandı"`

---

## Benden İsteyeceklerin

"Aşama X'e başlayalım" dediğinde Claude şunu yapacak:
1. O aşamanın dosya yapısını göster
2. Kodu yaz
3. "Bitti kriteri"ni nasıl test edeceğini açıkla
