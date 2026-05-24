#!/usr/bin/env python3
"""
Aşama 2b — MAVLink Simülatörü
Pixhawk olmadan test için ESP32 UART2'ye gerçekçi MAVLink 2 mesajları gönderir.
Gönderim: 4 Hz (her 250ms) — Pixhawk TELEM2 davranışını taklit eder.

Kullanım:
    pip install pyserial pymavlink
    python3 mavlink_sim.py --port /dev/ttyUSB0
"""

import argparse
import math
import time
import struct
import serial

# MAVLink 2 magic byte ve frame sabitleri
MAVLINK2_STX       = 0xFD
SYSTEM_ID          = 1     # Simülatörün sysid (Pixhawk gibi davranır)
COMPONENT_ID       = 1     # MAV_COMP_ID_AUTOPILOT1

# MAVLink mesaj ID'leri
MSG_HEARTBEAT            = 0
MSG_SYS_STATUS           = 1
MSG_GLOBAL_POSITION_INT  = 33
MSG_ATTITUDE             = 30
MSG_VFR_HUD              = 74
MSG_DISTANCE_SENSOR      = 132

# ---- CRC-16/MCRF4XX (MAVLink X25) ----
MAVLINK_CRC_EXTRA = {
    MSG_HEARTBEAT:           50,
    MSG_SYS_STATUS:         124,
    MSG_GLOBAL_POSITION_INT: 104,
    MSG_ATTITUDE:            39,
    MSG_VFR_HUD:             20,
    MSG_DISTANCE_SENSOR:    153,
}

def crc16_mcrf4xx(data: bytes, seed: int = 0xFFFF) -> int:
    crc = seed
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def pack_mavlink2(msg_id: int, payload: bytes, seq: int) -> bytes:
    """MAVLink 2 frame oluştur."""
    length = len(payload)
    # Header: STX | len | incompat | compat | seq | sysid | compid | msgid(3 byte LE)
    header = struct.pack(
        '<BBBBBBBBBB',
        MAVLINK2_STX,
        length,
        0,              # incompat_flags
        0,              # compat_flags
        seq & 0xFF,
        SYSTEM_ID,
        COMPONENT_ID,
        msg_id & 0xFF,
        (msg_id >> 8) & 0xFF,
        (msg_id >> 16) & 0xFF,
    )
    # CRC = CRC(header[1:] + payload + crc_extra)
    crc_data = header[1:] + payload + bytes([MAVLINK_CRC_EXTRA[msg_id]])
    crc = crc16_mcrf4xx(crc_data)
    return header + payload + struct.pack('<H', crc)


def make_heartbeat(seq: int) -> bytes:
    """HEARTBEAT (#0): uçuş modu=GUIDED(4), armed."""
    # custom_mode(4), type(1), autopilot(1), base_mode(1), sys_status(1), mavlink_version(1)
    armed      = 1
    base_mode  = 0x89 if armed else 0x01   # MAV_MODE_FLAG_SAFETY_ARMED | CUSTOM_MODE
    payload = struct.pack('<IBBBBB',
        4,          # custom_mode = GUIDED
        1,          # MAV_TYPE_FIXED_WING
        3,          # MAV_AUTOPILOT_ARDUPILOTMEGA
        base_mode,
        0,          # system_status: MAV_STATE_ACTIVE
        3,          # mavlink_version
    )
    return pack_mavlink2(MSG_HEARTBEAT, payload, seq)


def make_global_position_int(seq: int, t: float) -> bytes:
    """GLOBAL_POSITION_INT (#33): GPS konum simülasyonu."""
    # Ankara çevresinde küçük bir daire
    lat_base = 39_900_000   # degE7
    lon_base = 32_860_000
    radius   = 50_000       # ~50m yarıçap (degE7 biriminde)
    lat = lat_base + int(radius * math.sin(t * 0.5))
    lon = lon_base + int(radius * math.cos(t * 0.5))
    alt          = 150_000            # mm, 150m MSL
    relative_alt = 50_000             # mm, 50m AGL
    vx           = int(200 * math.cos(t * 0.5))   # cm/s
    vy           = int(200 * math.sin(t * 0.5))
    vz           = 10                 # cm/s
    hdg          = int((math.degrees(t * 0.5) % 360) * 100)  # cdeg
    # time_boot_ms(4), lat(4), lon(4), alt(4), relative_alt(4), vx(2), vy(2), vz(2), hdg(2)
    payload = struct.pack('<IiiiihhhH',
        int(t * 1000) & 0xFFFFFFFF,
        lat, lon, alt, relative_alt,
        vx, vy, vz, hdg,
    )
    return pack_mavlink2(MSG_GLOBAL_POSITION_INT, payload, seq)


def make_attitude(seq: int, t: float) -> bytes:
    """ATTITUDE (#30): hafif titreşimli dönüşler."""
    roll  = 0.05 * math.sin(t * 1.0)
    pitch = 0.03 * math.sin(t * 0.7)
    yaw   = t * 0.5 % (2 * math.pi)
    rollspeed  = 0.01 * math.cos(t)
    pitchspeed = 0.01 * math.cos(t * 0.7)
    yawspeed   = 0.5
    # time_boot_ms(4), roll(4f), pitch(4f), yaw(4f), rollspeed(4f), pitchspeed(4f), yawspeed(4f)
    payload = struct.pack('<Iffffff',
        int(t * 1000) & 0xFFFFFFFF,
        roll, pitch, yaw,
        rollspeed, pitchspeed, yawspeed,
    )
    return pack_mavlink2(MSG_ATTITUDE, payload, seq)


def make_vfr_hud(seq: int, t: float) -> bytes:
    """VFR_HUD (#74): hız ve irtifa."""
    airspeed    = 15.0 + 2.0 * math.sin(t * 0.3)   # m/s
    groundspeed = 14.5 + 1.5 * math.sin(t * 0.3)
    alt         = 150.0                               # m
    climb       = 0.2 * math.sin(t * 0.2)            # m/s
    heading     = int(math.degrees(t * 0.5) % 360)   # deg
    throttle    = 65                                  # %
    # airspeed(4f), groundspeed(4f), alt(4f), climb(4f), heading(2i), throttle(2H)
    payload = struct.pack('<ffffhH',
        airspeed, groundspeed, alt, climb,
        heading, throttle,
    )
    return pack_mavlink2(MSG_VFR_HUD, payload, seq)


def make_sys_status(seq: int, t: float) -> bytes:
    """SYS_STATUS (#1): batarya durumu."""
    voltage  = int(11800 + 200 * math.sin(t * 0.1))   # mV, ~12V
    current  = int(1500 + 500 * math.sin(t * 0.05))   # cA, ~15A
    batt_pct = max(10, int(85 - t * 0.1))              # % azalan
    # sensors_present(4), enabled(4), health(4), load(2), voltage(2), current(2), batt_remaining(1) ...
    # Sadece ilgili alanları doldur, geri kalan 0
    payload = struct.pack('<IIIHHhb',
        0x0000FFFF,  # sensors_present
        0x0000FFFF,  # sensors_enabled
        0x0000FFFF,  # sensors_health
        300,         # load (milli%)
        voltage,
        current,
        batt_pct,
    )
    # Kalan alanlar (errors ve drop counters): 12 byte sıfır
    payload += bytes(12)
    return pack_mavlink2(MSG_SYS_STATUS, payload, seq)


def make_distance_sensor(seq: int, t: float) -> bytes:
    """DISTANCE_SENSOR (#132): lidar mesafe."""
    dist_cm = int(4500 + 500 * math.sin(t * 0.8))   # cm, ~45m
    # time_boot_ms(4), min_distance(2), max_distance(2), current_distance(2),
    # type(1), id(1), orientation(1), covariance(1)
    payload = struct.pack('<IHHHBBBb',
        int(t * 1000) & 0xFFFFFFFF,
        10,       # min_distance cm
        10000,    # max_distance cm
        dist_cm,
        0,        # MAV_DISTANCE_SENSOR_LASER
        0,        # id
        25,       # MAV_SENSOR_ROTATION_PITCH_270 (downward)
        -1,       # covariance unknown (uint8 255 → int8 -1)
    )
    return pack_mavlink2(MSG_DISTANCE_SENSOR, payload, seq)


def run(port: str, baud: int, hz: float) -> None:
    interval = 1.0 / hz
    seq      = 0
    print(f"[SIM] {port} @ {baud} baud  —  {hz} Hz")
    print("[SIM] Ctrl+C ile dur")

    with serial.Serial(port, baud, timeout=1) as ser:
        t_start = time.time()
        while True:
            t = time.time() - t_start
            frames = [
                make_heartbeat(seq),
                make_global_position_int(seq + 1, t),
                make_attitude(seq + 2, t),
                make_vfr_hud(seq + 3, t),
                make_sys_status(seq + 4, t),
                make_distance_sensor(seq + 5, t),
            ]
            for f in frames:
                ser.write(f)
            seq = (seq + 6) & 0xFF
            print(f"[SIM] t={t:.1f}s  lat={39.9 + math.sin(t*0.5)*0.0005:.6f}"
                  f"  aspd={15+2*math.sin(t*0.3):.1f}m/s", end='\r')
            time.sleep(interval)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='MAVLink Simülatörü')
    parser.add_argument('--port', required=True, help='Seri port (örn. /dev/ttyUSB0)')
    parser.add_argument('--baud', type=int, default=57600, help='Baud rate (varsayılan: 57600)')
    parser.add_argument('--hz',   type=float, default=4.0, help='Gönderim hızı Hz (varsayılan: 4)')
    args = parser.parse_args()
    run(args.port, args.baud, args.hz)
