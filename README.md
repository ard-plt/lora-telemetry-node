# LoRa Telemetry Node

An end-to-end telemetry link that reads MAVLink 2 data from a Pixhawk over
UART, packs it into a compact binary frame, encrypts it, and relays it over
a long-range 868 MHz LoRa link to a ground station running QGroundControl.

```
[Pixhawk TELEM2] --UART MAVLink2--> [ESP32-S3 TX Node] --LoRa 868MHz--> [ESP32-S3 RX Node] --USB--> [QGroundControl]
```

The ESP32 does not read any sensors itself — Pixhawk is the only data
source. The TX node parses MAVLink, serializes + encrypts a fixed-size
packet, and transmits it over LoRa; the RX node decrypts and re-emits it
as MAVLink 2 over USB so QGroundControl sees a normal telemetry stream.

## LoRa Module

Both nodes use an **EBYTE E22-900T30D** (SX1262, 868 MHz, up to 30 dBm)
LoRa transceiver module, talking to the ESP32 over UART in the module's
Normal (transparent) mode.

| Parameter | Value |
|---|---|
| Frequency | 868.0 MHz |
| Spreading Factor | SF9 |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| TX Power | 30 dBm |
| Air Data Rate | selectable, 0.3–62.5 kbps (see below) |

**Power warning:** the E22-900T30D draws up to ~500 mA while transmitting.
Do **not** power it from the ESP32's own 3.3 V regulator — use a dedicated
AMS1117-3.3 LDO (1 A rated), with a shared ground between the ESP32 and
the E22.

### Bridge Mode + Air Data Rate

Each node can run in one of two modes, selected (along with the LoRa air
data rate) from a web configuration panel rather than a physical switch:

- **BRIDGE_TRANSPARENT** — the node becomes a transparent UART bridge so
  Mission Planner can reach the Pixhawk directly through the LoRa link for
  parameter/calibration/waypoint work.
- **BRIDGE_COMPRESSED** — normal telemetry mode: TX parses → packs →
  encrypts → transmits; RX receives → decrypts → unpacks → re-emits
  MAVLink.

| Air Rate | UART Baud |
|---|---|
| 0.3 / 1.2 / 2.4 / 4.8 kbps | 9600 |
| 9.6 kbps | 19200 |
| 19.2 kbps | 38400 |
| 38.4 kbps | 57600 |
| 62.5 kbps | 115200 |

Holding the config button (GPIO9) for 3 seconds opens a WPA2-protected
Wi-Fi access point (`TUAV-TX-Config` / `TUAV-RX-Config`) with a captive
portal for selecting bridge mode, air rate, and Wi-Fi password. Changing
the mode/rate from the **RX** node automatically re-synchronizes the
**TX** node over the RF link itself — no need to configure both ends by
hand. See `CLAUDE.md` for the full protocol details.

## PCB

A complete KiCad 10 PCB design lives under [`hardware/pcb/`](hardware/pcb),
covering both nodes as a two-board stack:

- **`main`** — ESP32-S3-DevKitC-1 socket + power section (UBEC input →
  PTC fuse → reverse-polarity protection → a dedicated buck regulator
  that isolates the E22's transmit current pulses from the ESP32 rail).
  Identical design for both TX and RX; fabricate two copies.
- **`tx_rf`** / **`rx_rf`** — the E22-900T30D module plus node-specific
  connectors (TX: Pixhawk TELEM2 JST-GH; RX: SSD1306 OLED header), on a
  small board that plugs directly onto `main` via a 16-pin header/socket
  (`J4`), stacked so the assembly stays compact.

Each project folder includes the schematic, PCB layout, custom footprint/
symbol libraries, and ready-to-fabricate Gerber + drill files
(`gerbers/`). Signal-net routing is left for completion in the KiCad GUI;
net assignments are DRC-verified and the ratsnest will guide it.

| Board | Size |
|---|---|
| `main` | 50 × 71 mm |
| `tx_rf` / `rx_rf` | 50 × 60 mm |

## Hardware Overview

| Component | Model |
|---|---|
| MCU | ESP32-S3-WROOM-1 (via ESP32-S3-DevKitC-1), ×2 |
| LoRa module | EBYTE E22-900T30D (SX1262, 868 MHz, 30 dBm) |
| Pixhawk link | TELEM2 → ESP32 UART2, MAVLink 2, 57600 baud |
| Ground station link | RX node's native USB (USB-OTG), 57600 baud — no FT232 needed |
| OLED (RX only) | SSD1306 128×64, I2C |

## Pin Mapping

### TX Node (air)

| Signal | ESP32 Pin |
|---|---|
| E22 TXD | GPIO4 (UART1 RX) |
| E22 RXD | GPIO5 (UART1 TX) |
| E22 AUX | GPIO6 |
| E22 M0 | GPIO7 |
| E22 M1 | GPIO8 |
| Config button | GPIO9 |
| Pixhawk TELEM2 TX | GPIO16 (UART2 RX) |
| Pixhawk TELEM2 RX | GPIO17 (UART2 TX) |

### RX Node (ground)

| Signal | ESP32 Pin |
|---|---|
| E22 TXD | GPIO4 (UART1 RX) |
| E22 RXD | GPIO5 (UART1 TX) |
| E22 AUX | GPIO6 |
| E22 M0 | GPIO7 |
| E22 M1 | GPIO8 |
| Config button | GPIO9 |
| OLED SDA | GPIO11 |
| OLED SCL | GPIO12 |

E22 M0=LOW, M1=LOW selects Normal TX/RX mode (set automatically at boot).
The E22↔ESP32 UART baud follows the selected air data rate (see table
above); the Pixhawk and ground-station links stay fixed at 57600 baud
regardless of LoRa mode.

## Packet Format (40 bytes, plaintext)

| Offset | Len | Type | Field |
|---|---|---|---|
| 0 | 1 | uint8 | header (fixed 0xAD) |
| 1 | 1 | uint8 | seq_id |
| 2 | 4 | int32 | latitude (degE7) |
| 6 | 4 | int32 | longitude (degE7) |
| 10 | 4 | int32 | altitude_mm (MSL) |
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
| 34 | 2 | uint16 | lidar_cm (0xFFFF = invalid) |
| 36 | 1 | uint8 | battery_pct |
| 37 | 1 | uint8 | flight_mode |
| 38 | 1 | uint8 | armed |
| 39 | 1 | uint8 | checksum (XOR of bytes 0–38) |

## Encryption

Telemetry is encrypted with AES-128-CTR (ESP32 hardware AES accelerator
via mbedTLS) before transmission: 40-byte plaintext → 40-byte ciphertext,
plus an 8-byte nonce (`seq_id` + low bytes of `millis()`) sent alongside
it. The key lives in `common/aes_config.h` as `AES_KEY` — the checked-in
value is a placeholder, **change it before any real deployment**, and
flash both nodes with the same key.

Separately, copy `common/secrets.h.example` to `common/secrets.h` and
fill in your own `WEB_CONFIG_SALT` (generate one with `openssl rand -hex
32`). This salt authenticates the HMAC on the RX→TX mode/rate sync
command over the RF link; `secrets.h` is gitignored and must never be
committed.

## Build & Flash

```bash
pip install platformio

# TX node
cd tx-node && pio run --target upload

# RX node
cd rx-node && pio run --target upload
```

## QGroundControl Setup

Application Settings → Comm Links → Add:
- Type: **Serial**
- Port: the RX node's COM / `ttyACM` port
- Baud rate: **57600**

See [`docs/qgc_setup.md`](docs/qgc_setup.md) for details and
[`docs/breadboard_wiring.md`](docs/breadboard_wiring.md) for a breadboard
wiring guide (or use the PCB design above instead).

## License

MIT
