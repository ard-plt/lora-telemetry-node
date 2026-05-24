#!/usr/bin/env python3
"""
TX Node Simülatörü — ESP32 TX kartını yazılımda taklit eder.
Sahte telemetri verisi üretir → TelemetryPacket'e serialize → AES-128-CTR şifrele → UDP gönder.

Kullanım:
    python3 sim_tx.py                   # varsayılan: localhost:14551 @ 2Hz
    python3 sim_tx.py --hz 4 --verbose
"""

import argparse
import math
import socket
import struct
import time
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

# ===== AES Sabitleri (common/aes_config.h ile aynı) =====
AES_KEY = bytes([
    0x2D, 0x4A, 0x6F, 0x8B, 0x1C, 0x3E, 0x5F, 0x7A,
    0x9D, 0xBE, 0xCF, 0xE0, 0x12, 0x34, 0x56, 0x78,
])

PACKET_HEADER = 0xAD
PACKET_SIZE   = 40
ENCRYPTED_SIZE = 48

# TelemetryPacket struct formatı (little-endian, packed, 40 byte)
# B  B  i  i  i  h  h  h  h  h  h  h  h  H  h  H  B  B  B  B
PKT_FMT = '<BBiiihhhhhhhhHhHBBBB'

def build_nonce(seq_id: int, ts_ms: int) -> bytes:
    """C++ build_nonce() ile birebir aynı: [seq | ts_ms 4byte LE | 000]"""
    nonce = bytearray(8)
    nonce[0] = seq_id & 0xFF
    nonce[1] = ts_ms & 0xFF
    nonce[2] = (ts_ms >> 8)  & 0xFF
    nonce[3] = (ts_ms >> 16) & 0xFF
    nonce[4] = (ts_ms >> 24) & 0xFF
    # nonce[5..7] = 0 (zaten bytearray sıfır)
    return bytes(nonce)

def encrypt_packet(plain: bytes, seq_id: int, ts_ms: int) -> bytes:
    """40 byte plaintext → 48 byte [nonce8 | cipher40]"""
    assert len(plain) == PACKET_SIZE
    nonce = build_nonce(seq_id, ts_ms)
    iv    = nonce + bytes(8)          # nonce + 8 sıfır = 16 byte IV
    cipher = Cipher(algorithms.AES(AES_KEY), modes.CTR(iv), backend=default_backend())
    enc = cipher.encryptor()
    ciphertext = enc.update(plain) + enc.finalize()
    return nonce + ciphertext

def calc_checksum(buf: bytes) -> int:
    """XOR of bytes 0..38"""
    cs = 0
    for b in buf[:PACKET_SIZE - 1]:
        cs ^= b
    return cs

def pack_telemetry(t: float, seq: int) -> bytes:
    """Sahte uçuş verisi → TelemetryPacket (40 byte)"""
    # Ankara çevresinde daire uçuş simülasyonu
    lat       = 399_000_000 + int(50_000 * math.sin(t * 0.3))   # degE7
    lon       = 328_600_000 + int(50_000 * math.cos(t * 0.3))
    alt_mm    = 150_000 + int(5_000 * math.sin(t * 0.1))         # mm MSL
    ralt_cm   = int(5_000 + 500 * math.sin(t * 0.1))             # cm AGL
    gspd_cms  = int(1_400 + 150 * math.sin(t * 0.2))             # cm/s
    aspd_cms  = int(1_500 + 100 * math.sin(t * 0.3))
    hdg_cd    = int((math.degrees(t * 0.3) % 360) * 100)         # centidegree
    roll_cd   = int(math.degrees(0.05 * math.sin(t)) * 100)
    pitch_cd  = int(math.degrees(0.03 * math.sin(t * 0.7)) * 100)
    yaw_cd    = hdg_cd
    climb_cms = int(20 * math.sin(t * 0.2))
    vbat_mv   = int(11_800 + 200 * math.sin(t * 0.05))           # mV ~12V
    curr_da   = int(150 + 50 * math.sin(t * 0.1))                # deciamps
    lidar_cm  = int(5_000 + 500 * math.sin(t * 0.8))             # cm
    batt_pct  = max(10, int(85 - t * 0.05))
    mode      = 4    # GUIDED
    armed     = 1

    # Checksum alanı geçici sıfır — sonra hesaplanacak
    raw = struct.pack(PKT_FMT,
        PACKET_HEADER, seq & 0xFF,
        lat, lon, alt_mm,
        ralt_cm, gspd_cms, aspd_cms, hdg_cd,
        roll_cd, pitch_cd, yaw_cd, climb_cms,
        vbat_mv, curr_da, lidar_cm,
        batt_pct, mode, armed,
        0,  # checksum placeholder
    )
    cs  = calc_checksum(raw)
    buf = bytearray(raw)
    buf[39] = cs
    return bytes(buf)

def run(host: str, port: int, hz: float, verbose: bool) -> None:
    interval = 1.0 / hz
    seq      = 0
    sock     = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    t_start  = time.time()

    print(f"[TX-SIM] Hedef: {host}:{port}  |  {hz} Hz  |  Ctrl+C ile dur")
    try:
        while True:
            t    = time.time() - t_start
            ts   = int(t * 1000) & 0xFFFFFFFF
            plain = pack_telemetry(t, seq)
            enc   = encrypt_packet(plain, seq, ts)
            sock.sendto(enc, (host, port))
            if verbose:
                pkt = struct.unpack(PKT_FMT, plain)
                lat_d = pkt[2] / 1e7
                lon_d = pkt[3] / 1e7
                print(f"[TX] seq={seq:3d}  lat={lat_d:.5f}  lon={lon_d:.5f}"
                      f"  alt={pkt[4]//1000}m  vbat={pkt[13]/1000:.2f}V  batt={pkt[16]}%")
            else:
                print(f"[TX] seq={seq:3d}  t={t:.1f}s  →  {ENCRYPTED_SIZE} byte gönderildi", end='\r')
            seq = (seq + 1) & 0xFF
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\n[TX-SIM] Durduruldu.")
    finally:
        sock.close()

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--host',    default='127.0.0.1')
    p.add_argument('--port',    type=int, default=14551)
    p.add_argument('--hz',      type=float, default=2.0)
    p.add_argument('--verbose', action='store_true')
    args = p.parse_args()
    run(args.host, args.port, args.hz, args.verbose)
