# QGroundControl Kurulum Rehberi

## Genel Bakış

RX node, LoRa'dan gelen şifreli paketi çözüp MAVLink 2 stream'e dönüştürür
ve USB-UART üzerinden PC'ye iletir. QGroundControl bu stream'i doğrudan
ArduPilot aracı olarak tanır.

```
[TX Node] --LoRa--> [RX Node] --USB (57600 baud)--> [PC: QGroundControl]
```

---

## 1. Donanım Bağlantısı

1. RX node'un **FT232 USB-UART** adaptörünü PC'ye tak
2. Linux'ta port: `/dev/ttyUSB0` (veya ttyUSB1, ttyACM0)
   Windows'ta port: `COMx` (Aygıt Yöneticisi'nden kontrol et)
3. Baud rate: **57600** (RX node `Serial.begin(57600)` ile başlatılıyor)

> **Not:** ESP32 USB'si değil, FT232 adaptörünün USB kablosu bağlanmalı.
> ESP32 USB'si sadece programlama/debug içindir.

---

## 2. QGroundControl Comm Link Ayarı

1. QGC'yi aç → sol üst **Q** logosuna tıkla → **Application Settings**
2. Sol menüden **Comm Links** seç
3. **Add** butonuna bas, şu değerleri gir:

| Alan        | Değer                        |
|-------------|------------------------------|
| Name        | LoRa RX Node                 |
| Type        | Serial                       |
| Serial Port | `/dev/ttyUSB0` veya `COMx`   |
| Baud Rate   | 57600                        |

4. **OK** → listede "LoRa RX Node" görünür
5. Üzerine tıkla → **Connect**

---

## 3. Beklenen Davranış

Bağlantı kurulduktan sonra QGC'de görünecekler:

| QGC Bileşeni       | Kaynak MAVLink Mesajı    | Güncelleme Hızı |
|--------------------|--------------------------|-----------------|
| Harita konumu      | GLOBAL_POSITION_INT (#33)| 2 Hz (LoRa hızı)|
| HUD roll/pitch/yaw | ATTITUDE (#30)           | 2 Hz            |
| Airspeed, altitude | VFR_HUD (#74)            | 2 Hz            |
| Batarya göstergesi | SYS_STATUS (#1)          | 2 Hz            |
| Armed / uçuş modu  | HEARTBEAT (#0)           | 1 Hz            |

> **GPS kilidi:** QGC haritada konum göstermek için geçerli GPS koordinatı ister.
> Pixhawk GPS fix almadıysa harita güncellenmez; HUD ve batarya yine de çalışır.

---

## 4. Linux Port İzni (tek seferlik)

```bash
sudo usermod -aG dialout $USER
# Çıkış yapıp tekrar giriş yap
```

Veya geçici:
```bash
sudo chmod 666 /dev/ttyUSB0
```

---

## 5. Bağlantı Testi — Serial Monitor ile Doğrulama

QGC'ye geçmeden önce RX node'un MAVLink binary çıkardığını doğrula:

```bash
# Binary akışı gör (satırlar karmaşık görünür — bu normal, MAVLink binary)
cat /dev/ttyUSB0

# Veya python ile mesaj sayısını say
python3 -c "
import serial, time
s = serial.Serial('/dev/ttyUSB0', 57600, timeout=1)
count = 0
t = time.time()
while time.time() - t < 5:
    data = s.read(256)
    if data: count += len(data)
print(f'5 saniyede {count} byte alındı')
# Beklenen: ~500 byte/s (MAVLink mesajları @ 2Hz)
"
```

---

## 6. Sorun Giderme

| Belirti | Olası Neden | Çözüm |
|---------|-------------|-------|
| QGC "Waiting for Vehicle" | Yanlış port veya baud | Port ve baud rate'i kontrol et |
| QGC "Waiting for Vehicle" | TX node göndermiyordur | TX node Serial logunu kontrol et |
| Harita güncellenmez | GPS fix yok | Pixhawk GPS fix alana kadar bekle |
| Port listede görünmüyor | FT232 driver yok | `lsusb` ile FT232 tespit edildiğini doğrula |
| Binary çıkış yok | RX node LoRa paketi almıyor | LoRa frekans/SF parametrelerini kontrol et |

---

## 7. Sistem Parametreleri (Hatırlatma)

```
LoRa Frekans : 868.0 MHz
SF           : 9
Bandwidth    : 125 kHz
TX Power     : 30 dBm
Gönderim     : 2 Hz
MAVLink Baud : 57600
SysID        : 1  (ArduPilot autopilot taklidi)
CompID       : 1
```
