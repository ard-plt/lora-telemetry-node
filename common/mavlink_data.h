#pragma once
#include <stdint.h>

// Pixhawk'tan parse edilen tüm MAVLink verisi — tx ve rx node'lar arası ortak struct
struct MavlinkData {
    // HEARTBEAT (#0)
    uint8_t  armed;          // 0=disarmed, 1=armed
    uint8_t  flight_mode;    // ArduPilot custom_mode low byte

    // GLOBAL_POSITION_INT (#33)
    int32_t  lat;            // degE7
    int32_t  lon;            // degE7
    int32_t  alt;            // mm (MSL)
    int32_t  relative_alt;   // mm (yerden)
    int16_t  vx;             // cm/s (kuzey)
    int16_t  vy;             // cm/s (doğu)
    uint16_t hdg;            // cdeg (0–36000)

    // ATTITUDE (#30)
    float roll;              // rad
    float pitch;             // rad
    float yaw;               // rad

    // VFR_HUD (#74)
    float airspeed;          // m/s
    float groundspeed;       // m/s
    float climb;             // m/s

    // SYS_STATUS (#1)
    uint16_t vbat_mv;        // mV
    int16_t  current_ca;     // centiamps (10 mA birimi), -1=bilinmiyor
    int8_t   battery_pct;    // %, -1=bilinmiyor

    // DISTANCE_SENSOR (#132)
    uint16_t lidar_cm;       // cm, 0xFFFF=geçersiz
};
