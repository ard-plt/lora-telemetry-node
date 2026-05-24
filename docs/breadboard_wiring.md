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

## TX Node Bağlantıları

TX node: E22 + ESP32 + Pixhawk (TELEM2)

### E22 ↔ ESP32 (UART1)

| E22 Pin | ESP32 Pin | Not                          |
|---------|-----------|------------------------------|
| VCC     | —         | Ayrı AMS1117-3.3 çıkışına    |
| GND     | GND       | Ortak GND hattı              |
| TXD     | GPIO33    | E22 çıkış → ESP32 UART1 RX  |
| RXD     | GPIO32    | E22 giriş ← ESP32 UART1 TX  |
| AUX     | GPIO27    | Modül hazır sinyali (INPUT)  |
| M0      | GPIO26    | Mod seçimi (OUTPUT)          |
| M1      | GPIO25    | Mod seçimi (OUTPUT)          |

### Pixhawk TELEM2 ↔ ESP32 (UART2)

| Pixhawk TELEM2 | ESP32 Pin | Not                         |
|----------------|-----------|-----------------------------|
| TX             | GPIO16    | Pixhawk çıkış → ESP32 RX   |
| RX             | GPIO17    | Pixhawk giriş ← ESP32 TX   |
| GND            | GND       | Ortak GND — MUTLAKA bağla  |
| VCC (5V)       | —         | Bağlama, ESP32'yi ayrı besle|

---

## RX Node Bağlantıları

RX node: E22 + ESP32 + FT232 USB-UART

### E22 ↔ ESP32 (UART1)

| E22 Pin | ESP32 Pin | Not                          |
|---------|-----------|------------------------------|
| VCC     | —         | Ayrı AMS1117-3.3 çıkışına    |
| GND     | GND       | Ortak GND hattı              |
| TXD     | GPIO16    | E22 çıkış → ESP32 UART1 RX  |
| RXD     | GPIO17    | E22 giriş ← ESP32 UART1 TX  |
| AUX     | GPIO27    | Modül hazır sinyali (INPUT)  |
| M0      | GPIO26    | Mod seçimi (OUTPUT)          |
| M1      | GPIO25    | Mod seçimi (OUTPUT)          |

### ESP32 ↔ FT232 USB-UART (QGC çıkışı)

| ESP32 Pin | FT232 Pin | Not                              |
|-----------|-----------|----------------------------------|
| GPIO1 (TX0) | RXD     | ESP32 Serial → FT232 giriş      |
| GPIO3 (RX0) | TXD     | (isteğe bağlı, tek yön yeterli) |
| GND       | GND       | Ortak GND                        |

> ESP32'nin USB-Micro portu varsa FT232 gerekmez — doğrudan PC'ye bağla.
> Serial0 (GPIO1/3) USB üzerinden çalışır.

---

## Breadboard Kurulum Sırası

1. **GND hattı:** Breadboard'un (-) rayını ESP32 GND, E22 GND, FT232 GND ile birleştir
2. **Güç:** AMS1117-3.3 çıkışını E22 VCC'ye bağla; ESP32'yi USB veya VIN'den besle
3. **M0/M1:** GPIO26 → M0, GPIO25 → M1 (kod LOW yapacak, pull-down direnci gerekmez)
4. **AUX:** GPIO27 → AUX (seri direnci yok, doğrudan bağla)
5. **UART:** TXD/RXD çapraz bağla (TX→RX, RX→TX)
6. Programla, Serial monitörde `[LoRa] TX/RX hazir` mesajını bekle

---

## Kontrol Listesi

- [ ] E22 VCC ayrı LDO'dan besleniyor (ESP32 3V3'ten değil)
- [ ] GND hatları birleşik (ESP32 + E22 + FT232 + LDO)
- [ ] TXD/RXD çapraz bağlı (E22 TXD → ESP32 RX pini)
- [ ] M0, M1 doğru ESP32 pinlerine bağlı
- [ ] AUX bağlı (bağlanmazsa kod AUX timeout hatası verir)
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
