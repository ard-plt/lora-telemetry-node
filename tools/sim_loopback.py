#!/usr/bin/env python3
"""
Loopback Testi — QGC ve UDP olmadan tüm pipeline'ı doğrular.
Çalıştırıldığında: veri üret → serialize → encrypt → decrypt → deserialize → karşılaştır

Kullanım:
    python3 sim_loopback.py
"""

import math
import struct
import sys
import time
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.backends import default_backend

AES_KEY = bytes([
    0x2D, 0x4A, 0x6F, 0x8B, 0x1C, 0x3E, 0x5F, 0x7A,
    0x9D, 0xBE, 0xCF, 0xE0, 0x12, 0x34, 0x56, 0x78,
])

PACKET_HEADER  = 0xAD
PACKET_SIZE    = 40
ENCRYPTED_SIZE = 48
PKT_FMT        = '<BBiiihhhhhhhhHhHBBBB'

def build_nonce(seq_id, ts_ms):
    n = bytearray(8)
    n[0] = seq_id & 0xFF
    n[1] = ts_ms & 0xFF
    n[2] = (ts_ms >> 8)  & 0xFF
    n[3] = (ts_ms >> 16) & 0xFF
    n[4] = (ts_ms >> 24) & 0xFF
    return bytes(n)

def aes_ctr(key, iv, data):
    c = Cipher(algorithms.AES(key), modes.CTR(iv), backend=default_backend())
    e = c.encryptor()
    return e.update(data) + e.finalize()

def encrypt(plain, seq_id, ts_ms):
    nonce = build_nonce(seq_id, ts_ms)
    iv    = nonce + bytes(8)
    return nonce + aes_ctr(AES_KEY, iv, plain)

def decrypt(enc):
    nonce = enc[:8]
    iv    = nonce + bytes(8)
    return aes_ctr(AES_KEY, iv, enc[8:])

def calc_checksum(buf):
    cs = 0
    for b in buf[:PACKET_SIZE - 1]:
        cs ^= b
    return cs

def make_packet(seq, t):
    lat      = 399_000_000 + int(50_000 * math.sin(t * 0.3))
    lon      = 328_600_000 + int(50_000 * math.cos(t * 0.3))
    alt_mm   = 150_000
    ralt_cm  = 5_000
    gspd_cms = 1_400
    aspd_cms = 1_500
    hdg_cd   = 9_000
    roll_cd  = 286
    pitch_cd = 172
    yaw_cd   = 9_000
    climb    = 20
    vbat_mv  = 11_800
    curr_da  = 150
    lidar_cm = 5_000
    batt_pct = 85
    mode     = 4
    armed    = 1
    raw = struct.pack(PKT_FMT,
        PACKET_HEADER, seq & 0xFF,
        lat, lon, alt_mm,
        ralt_cm, gspd_cms, aspd_cms, hdg_cd,
        roll_cd, pitch_cd, yaw_cd, climb,
        vbat_mv, curr_da, lidar_cm,
        batt_pct, mode, armed, 0,
    )
    buf = bytearray(raw)
    buf[39] = calc_checksum(raw)
    return bytes(buf)

def run_tests():
    print("=" * 55)
    print("  LoRa Telemetry — Pipeline Loopback Testi")
    print("=" * 55)
    passed = 0
    failed = 0

    for i in range(10):
        seq  = i
        ts   = int(time.time() * 1000 + i * 500) & 0xFFFFFFFF
        t    = i * 0.5

        # 1. Paket oluştur
        plain_orig = make_packet(seq, t)
        assert len(plain_orig) == PACKET_SIZE

        # 2. Checksum doğrula
        cs_ok = (plain_orig[39] == calc_checksum(plain_orig))

        # 3. Şifrele
        enc = encrypt(plain_orig, seq, ts)
        assert len(enc) == ENCRYPTED_SIZE

        # 4. Şifreli veri orijinalden farklı mı?
        enc_differs = (enc[8:] != plain_orig)

        # 5. Şifre çöz
        plain_dec = decrypt(enc)
        assert len(plain_dec) == PACKET_SIZE

        # 6. Orijinalle karşılaştır
        match = (plain_dec == plain_orig)

        # 7. Farklı seq/ts ile şifreleme farklı sonuç vermeli
        enc2 = encrypt(plain_orig, (seq + 1) & 0xFF, ts)
        unique_nonce = (enc2[:8] != enc[:8])

        ok = cs_ok and enc_differs and match and unique_nonce
        status = "PASS" if ok else "FAIL"
        if ok:
            passed += 1
        else:
            failed += 1

        f = struct.unpack(PKT_FMT, plain_dec)
        lat_d = f[2] / 1e7
        print(f"  [{status}] seq={seq:2d}  lat={lat_d:.5f}"
              f"  cs={'OK' if cs_ok else 'ERR'}"
              f"  enc={'diff' if enc_differs else 'SAME!'}"
              f"  dec={'match' if match else 'MISMATCH!'}"
              f"  nonce={'unique' if unique_nonce else 'SAME!'}")

    print("-" * 55)
    print(f"  Sonuç: {passed}/10 PASS  |  {failed}/10 FAIL")

    # Ek: farklı seq ile şifrelenmiş iki paket birbirinden farklı olmalı
    p1 = encrypt(plain_orig, 0, 1000)
    p2 = encrypt(plain_orig, 1, 1000)
    p3 = encrypt(plain_orig, 0, 2000)
    print(f"\n  Nonce benzersizliği:")
    print(f"    seq0 vs seq1 : {'farklı ✓' if p1 != p2 else 'AYNI! ✗'}")
    print(f"    ts1000 vs ts2000 : {'farklı ✓' if p1 != p3 else 'AYNI! ✗'}")
    print("=" * 55)

    return failed == 0

if __name__ == '__main__':
    ok = run_tests()
    sys.exit(0 if ok else 1)
