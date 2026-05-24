#pragma once
#include <Arduino.h>
#include <ardupilotmega/mavlink.h>
#include "packet.h"

// TelemetryPacket → MAVLink mesajları → Serial (QGroundControl)
// System ID=1, Component ID=1 (ArduPilot autopilot taklidi)
class MavlinkTx {
public:
    // HEARTBEAT gönder — QGC bağlantısı için 1 Hz gerekli
    void send_heartbeat(const TelemetryPacket& pkt);

    // Telemetri paketinden tüm MAVLink mesajlarını gönder
    // GLOBAL_POSITION_INT + ATTITUDE + VFR_HUD + SYS_STATUS
    void send_telemetry(const TelemetryPacket& pkt);

private:
    uint8_t _buf[MAVLINK_MAX_PACKET_LEN];

    // Mesajı seri porta yazar
    void _write(mavlink_message_t& msg);
};
