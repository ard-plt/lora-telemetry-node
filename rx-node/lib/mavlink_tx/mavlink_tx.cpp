#include "mavlink_tx.h"

#define SYS_ID   1   // QGC'nin beklediği araç system ID
#define COMP_ID  1   // MAV_COMP_ID_AUTOPILOT1

// Mesajı seri porta binary olarak yazar
void MavlinkTx::_write(mavlink_message_t& msg) {
    uint16_t len = mavlink_msg_to_send_buffer(_buf, &msg);
    Serial.write(_buf, len);
}

// HEARTBEAT — QGC bağlantı için 1 Hz gönderilmeli
void MavlinkTx::send_heartbeat(const TelemetryPacket& pkt) {
    // armed durumuna göre base_mode flag'i ayarla
    uint8_t base_mode = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
    if (pkt.armed) base_mode |= MAV_MODE_FLAG_SAFETY_ARMED;

    mavlink_message_t msg;
    mavlink_msg_heartbeat_pack(SYS_ID, COMP_ID, &msg,
        MAV_TYPE_QUADROTOR,
        MAV_AUTOPILOT_ARDUPILOTMEGA,
        base_mode,
        pkt.flight_mode,    // ArduPilot custom_mode
        MAV_STATE_ACTIVE
    );
    _write(msg);
}

// GLOBAL_POSITION_INT — GPS, yükseklik, hız
static void _send_gps(mavlink_message_t& msg, const TelemetryPacket& p) {
    // vx/vy pakette yok — groundspeed + heading'den hesapla
    float hdg_rad = p.heading_cd * (float)(M_PI / 18000.0);
    int16_t vx = (int16_t)(p.groundspeed_cms * cosf(hdg_rad));
    int16_t vy = (int16_t)(p.groundspeed_cms * sinf(hdg_rad));

    mavlink_msg_global_position_int_pack(SYS_ID, COMP_ID, &msg,
        millis(),
        p.latitude,                    // degE7
        p.longitude,                   // degE7
        p.altitude_mm,                 // MSL mm
        p.relative_alt_cm * 10,        // yerden mm (cm→mm)
        vx, vy,
        -p.climb_rate_cms,             // MAVLink vz: aşağı pozitif
        p.heading_cd                   // cdeg
    );
}

void MavlinkTx::send_telemetry(const TelemetryPacket& p) {
    mavlink_message_t msg;

    // GLOBAL_POSITION_INT (#33)
    _send_gps(msg, p);
    _write(msg);

    // ATTITUDE (#30) — centidegree → radyan
    const float CD2RAD = (float)(M_PI / 18000.0);
    mavlink_msg_attitude_pack(SYS_ID, COMP_ID, &msg,
        millis(),
        p.roll_cd  * CD2RAD,
        p.pitch_cd * CD2RAD,
        p.yaw_cd   * CD2RAD,
        0.0f, 0.0f, 0.0f    // açısal hızlar — pakette yok
    );
    _write(msg);

    // VFR_HUD (#74)
    mavlink_msg_vfr_hud_pack(SYS_ID, COMP_ID, &msg,
        p.airspeed_cms    / 100.0f,   // m/s
        p.groundspeed_cms / 100.0f,   // m/s
        p.heading_cd / 100,           // derece (int16)
        0,                            // throttle % — pakette yok
        p.altitude_mm / 1000.0f,      // MSL m
        p.climb_rate_cms / 100.0f     // m/s
    );
    _write(msg);

    // SYS_STATUS (#1)
    // current_battery: MAVLink centiamps (10mA) = deciamps × 10
    int16_t curr_ca  = (p.current_da >= 0) ? p.current_da * 10 : -1;
    int8_t  bpct     = (p.battery_pct == 0xFF) ? -1 : (int8_t)p.battery_pct;
    mavlink_msg_sys_status_pack(SYS_ID, COMP_ID, &msg,
        0, 0, 0,           // sensors present/enabled/health
        0,                 // system load
        p.vbat_mv,         // mV
        curr_ca,           // 10 mA birimi
        bpct,              // %
        0, 0,              // drop_rate_comm, errors_comm
        0, 0, 0, 0,        // errors_count 1-4
        0, 0, 0            // extended sensor flags (MAVLink2)
    );
    _write(msg);
}
