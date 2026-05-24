#!/usr/bin/env python3
"""
RX Node Simülatörü — ESP32 RX kartını yazılımda taklit eder.
UDP'den şifreli paket al → AES-128-CTR çöz → TelemetryPacket → MAVLink → QGC UDP.

Kullanım:
    python3 sim_rx.py                   # sim_tx'den dinle, QGC'ye 14550 gönder
    python3 sim_rx.py --rx-port 14551 --qgc-port 14550 --qgc-host 127.0.0.1

QGC ayarı:
    Comm Links → Add → UDP → Listening Port: 14550
"""

import argparse
import math
import socket
import struct
import time
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend
from pymavlink import mavutil
from pymavlink.dialects.v20 import ardupilotmega as mavlink2

# ===== AES Sabitleri (sim_tx.py ile aynı) =====
AES_KEY = bytes([
    0x2D, 0x4A, 0x6F, 0x8B, 0x1C, 0x3E, 0x5F, 0x7A,
    0x9D, 0xBE, 0xCF, 0xE0, 0x12, 0x34, 0x56, 0x78,
])

PACKET_HEADER  = 0xAD
PACKET_SIZE    = 40
ENCRYPTED_SIZE = 48
PKT_FMT        = '<BBiiihhhhhhhhHhHBBBB'

SYS_ID  = 1
COMP_ID = 1

def decrypt_packet(data: bytes) -> bytes | None:
    """48 byte şifreli → 40 byte plaintext. Hata durumunda None."""
    if len(data) != ENCRYPTED_SIZE:
        return None
    nonce = data[:8]
    iv    = nonce + bytes(8)
    cipher = Cipher(algorithms.AES(AES_KEY), modes.CTR(iv), backend=default_backend())
    dec = cipher.decryptor()
    return dec.update(data[8:]) + dec.finalize()

def calc_checksum(buf: bytes) -> int:
    cs = 0
    for b in buf[:PACKET_SIZE - 1]:
        cs ^= b
    return cs

def unpack_telemetry(raw: bytes):
    """bytes → tuple. Checksum/header geçersizse None döndür."""
    if len(raw) != PACKET_SIZE:
        return None
    fields = struct.unpack(PKT_FMT, raw)
    header, seq = fields[0], fields[1]
    if header != PACKET_HEADER:
        return None
    expected_cs = calc_checksum(raw)
    if fields[19] != expected_cs:
        return None
    return fields

class MavlinkQGC:
    """QGC'ye UDP üzerinden MAVLink 2 gönderir."""

    def __init__(self, host: str, port: int):
        self._mav  = mavutil.mavlink_connection(
            f'udpout:{host}:{port}',
            source_system=SYS_ID,
            source_component=COMP_ID,
        )
        self._boot = time.monotonic()

    def _ms(self) -> int:
        return int((time.monotonic() - self._boot) * 1000)

    def send_heartbeat(self, armed: int, flight_mode: int) -> None:
        base_mode = mavlink2.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED
        if armed:
            base_mode |= mavlink2.MAV_MODE_FLAG_SAFETY_ARMED
        self._mav.mav.heartbeat_send(
            mavlink2.MAV_TYPE_QUADROTOR,
            mavlink2.MAV_AUTOPILOT_ARDUPILOTMEGA,
            base_mode,
            flight_mode,
            mavlink2.MAV_STATE_ACTIVE,
        )

    def send_telemetry(self, f) -> None:
        """f = unpack_telemetry() tuple"""
        # f indeksleri: 0=header,1=seq,2=lat,3=lon,4=alt_mm,5=ralt_cm,
        #               6=gspd,7=aspd,8=hdg,9=roll,10=pitch,11=yaw,12=climb,
        #               13=vbat,14=curr,15=lidar,16=batt,17=mode,18=armed,19=cs
        ms  = self._ms()
        hdg_rad = f[8] * (math.pi / 18000.0)
        vx  = int(f[6] * math.cos(hdg_rad))
        vy  = int(f[6] * math.sin(hdg_rad))

        # GLOBAL_POSITION_INT (#33)
        self._mav.mav.global_position_int_send(
            ms,
            f[2],           # lat degE7
            f[3],           # lon degE7
            f[4],           # alt mm MSL
            f[5] * 10,      # relative alt mm (cm→mm)
            vx, vy,
            -f[12],         # vz (MAVLink: aşağı pozitif)
            f[8],           # hdg cdeg
        )

        # ATTITUDE (#30)
        CD2RAD = math.pi / 18000.0
        self._mav.mav.attitude_send(
            ms,
            f[9]  * CD2RAD,  # roll rad
            f[10] * CD2RAD,  # pitch rad
            f[11] * CD2RAD,  # yaw rad
            0.0, 0.0, 0.0,
        )

        # VFR_HUD (#74)
        self._mav.mav.vfr_hud_send(
            f[7]  / 100.0,   # airspeed m/s
            f[6]  / 100.0,   # groundspeed m/s
            f[8]  // 100,    # heading deg
            0,               # throttle %
            f[4]  / 1000.0,  # alt MSL m
            f[12] / 100.0,   # climb m/s
        )

        # SYS_STATUS (#1)
        curr_ca = f[14] * 10 if f[14] >= 0 else -1    # deciamps → centiamps
        bpct    = f[16] if f[16] != 0xFF else -1
        self._mav.mav.sys_status_send(
            0, 0, 0,     # sensors
            0,           # load
            f[13],       # vbat mV
            curr_ca,     # current cA
            bpct,        # battery %
            0, 0,        # drop_rate, errors_comm
            0, 0, 0, 0,  # errors_count
            0,           # onboard_control_sensors_present_extended
        )

def run(rx_host: str, rx_port: int, qgc_host: str, qgc_port: int, verbose: bool) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((rx_host, rx_port))
    sock.settimeout(0.1)

    qgc = MavlinkQGC(qgc_host, qgc_port)

    rx_count      = 0
    err_count     = 0
    last_hb       = time.time()
    last_pkt      = None

    print(f"[RX-SIM] Dinleniyor: {rx_host}:{rx_port}")
    print(f"[RX-SIM] QGC hedef:  {qgc_host}:{qgc_port}")
    print("[RX-SIM] Ctrl+C ile dur\n")

    try:
        while True:
            # Paket al
            try:
                data, addr = sock.recvfrom(256)
                plain = decrypt_packet(data)
                if plain is None:
                    err_count += 1
                    print(f"[RX] HATA: decrypt başarısız (boyut={len(data)})")
                    continue

                fields = unpack_telemetry(plain)
                if fields is None:
                    err_count += 1
                    print("[RX] HATA: checksum veya header geçersiz")
                    continue

                last_pkt = fields
                rx_count += 1
                qgc.send_telemetry(fields)

                if verbose:
                    lat_d = fields[2] / 1e7
                    lon_d = fields[3] / 1e7
                    print(f"[RX] seq={fields[1]:3d}  lat={lat_d:.5f}  lon={lon_d:.5f}"
                          f"  alt={fields[4]//1000}m  vbat={fields[13]/1000:.2f}V"
                          f"  batt={fields[16]}%  armed={fields[18]}  rx={rx_count}  err={err_count}")
                else:
                    print(f"[RX] seq={fields[1]:3d}  rx={rx_count}  err={err_count}"
                          f"  batt={fields[16]}%  armed={fields[18]}", end='\r')

            except socket.timeout:
                pass

            # HEARTBEAT: 1 Hz
            now = time.time()
            if now - last_hb >= 1.0:
                last_hb = now
                armed = last_pkt[18] if last_pkt else 0
                mode  = last_pkt[17] if last_pkt else 0
                qgc.send_heartbeat(armed, mode)

    except KeyboardInterrupt:
        print(f"\n[RX-SIM] Durduruldu — alınan: {rx_count}  hata: {err_count}")
    finally:
        sock.close()

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--rx-host',  default='0.0.0.0')
    p.add_argument('--rx-port',  type=int, default=14551)
    p.add_argument('--qgc-host', default='127.0.0.1')
    p.add_argument('--qgc-port', type=int, default=14550)
    p.add_argument('--verbose',  action='store_true')
    args = p.parse_args()
    run(args.rx_host, args.rx_port, args.qgc_host, args.qgc_port, args.verbose)
