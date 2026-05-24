# LoRa Telemetry Node

Pixhawk'tan MAVLink 2 üzerinden gelen telemetri verisini ESP32-WROOM-32
ile alıp EBYTE E22-900T30D (LoRa 868MHz) üzerinden yer istasyonuna ileten
uçtan uca telemetri sistemi.

## Mimari

```
[Pixhawk TELEM2] --UART MAVLink2--> [ESP32 TX Node] --LoRa 868MHz--> [ESP32 RX Node] --USB UART--> [QGroundControl]
```

## Donanım

| Bileşen | Model |
|---------|-------|
| MCU | ESP32-WROOM-32 (×2) |
| LoRa | EBYTE E22-900T30D (SX1262, 868 MHz, 30 dBm) |
| Pixhawk bağlantısı | TELEM2 → ESP32 UART2 (MAVLink 2, 57600 baud) |
| Yer istasyonu çıkışı | USB-UART (FT232) → PC, 57600 baud |

> **Güç Uyarısı:** E22-900T30D TX anında ~500 mA çeker.
> ESP32'nin 3.3 V regülatörüne **BAĞLAMA** — ayrı AMS1117-3.3 LDO (1 A rated) kullan.
> ESP32 GND ve E22 GND ortak olmalı.

## Pin Mapping

### TX Node (Hava)

| Bağlantı | ESP32 Pin | Protokol |
|----------|-----------|----------|
| E22 TXD | GPIO33 | UART1 RX |
| E22 RXD | GPIO32 | UART1 TX |
| E22 AUX | GPIO27 | GPIO (INPUT) |
| E22 M0 | GPIO26 | GPIO (OUTPUT) |
| E22 M1 | GPIO25 | GPIO (OUTPUT) |
| Pixhawk TELEM2 TX | GPIO16 | UART2 RX |
| Pixhawk TELEM2 RX | GPIO17 | UART2 TX |
| Pixhawk GND | GND | Ortak GND |

### RX Node (Yer)

| Bağlantı | ESP32 Pin | Protokol |
|----------|-----------|----------|
| E22 TXD | GPIO16 | UART1 RX |
| E22 RXD | GPIO17 | UART1 TX |
| E22 AUX | GPIO15 | GPIO (INPUT) |
| E22 M0 | GPIO4 | GPIO (OUTPUT) |
| E22 M1 | GPIO2 | GPIO (OUTPUT) |

E22 M0=LOW, M1=LOW → Normal TX/RX modu (kod setup'ta otomatik set eder).
E22 ↔ ESP32 UART baud = 9600 (E22 varsayılanı).

## LoRa Parametreleri

| Parametre | Değer |
|-----------|-------|
| Frekans | 868.0 MHz |
| Air Data Rate | 2.4 kbps |
| TX Gücü | 30 dBm |
| Gönderim | 2 Hz |
| Paket boyutu | 48 byte (8 nonce + 40 şifreli) |

## Güvenlik

Veriler AES-128-CTR ile şifrelenir. `common/aes_config.h` içindeki
`AES_KEY` sabiti örnek değerdir — **üretim öncesi mutlaka değiştir.**

## Kurulum

```bash
# PlatformIO CLI gerekli
pip install platformio

# TX Node derle & yükle
cd tx-node && pio run --target upload

# RX Node derle & yükle
cd rx-node && pio run --target upload
```

### QGroundControl Bağlantısı

Application Settings → Comm Links → Add:
- Type: **Serial**
- Port: RX node'un COM / ttyUSB portu
- Baud Rate: **57600**

## Paket Formatı (40 byte, little-endian)

| Offset | Len | Tip | Alan |
|--------|-----|-----|------|
| 0 | 1 | uint8 | header (0xAD sabit) |
| 1 | 1 | uint8 | seq_id |
| 2 | 4 | int32 | latitude (degE7) |
| 6 | 4 | int32 | longitude (degE7) |
| 10 | 4 | int32 | altitude_mm (mm MSL) |
| 14 | 2 | int16 | relative_alt_cm |
| 16 | 2 | int16 | groundspeed_cms |
| 18 | 2 | int16 | airspeed_cms |
| 20 | 2 | int16 | heading_cd |
| 22 | 2 | int16 | roll_cd |
| 24 | 2 | int16 | pitch_cd |
| 26 | 2 | int16 | yaw_cd |
| 28 | 2 | int16 | climb_rate_cms |
| 30 | 2 | uint16 | vbat_mv |
| 32 | 2 | int16 | current_da |
| 34 | 2 | uint16 | lidar_cm (0xFFFF=geçersiz) |
| 36 | 1 | uint8 | battery_pct |
| 37 | 1 | uint8 | flight_mode |
| 38 | 1 | uint8 | armed |
| 39 | 1 | uint8 | checksum (XOR 0–38) |

## Geliştirme Aşamaları

- [x] Aşama 1 — PlatformIO İskeleti
- [x] Aşama 2 — MAVLink Parse Katmanı
- [x] Aşama 2b — MAVLink Simülatör (tools/)
- [x] Aşama 3 — Paket Serialize / Deserialize
- [x] Aşama 4 — LoRa TX (E22 UART transparent mod)
- [x] Aşama 4.5 — AES-128-CTR Şifreleme
- [x] Aşama 5 — LoRa RX
- [x] Aşama 6 — QGroundControl MAVLink çıkışı
- [ ] Aşama 7 — SF Karşılaştırma & Link Budget
- [ ] Aşama 8 — GitHub Hazırlığı (donanım şeması, link budget)

## Lisans

MIT
