#include "mavlink_reader.h"
#include "packet.h"   // MAVLINK_BAUD, MAVLINK_RX_PIN, MAVLINK_TX_PIN, LIDAR_INVALID

MavlinkReader::MavlinkReader() {
    memset(&_data, 0, sizeof(_data));
    // Geçersiz varsayılan değerler
    _data.lidar_cm    = LIDAR_INVALID;
    _data.battery_pct = -1;
    _data.current_ca  = -1;
}

void MavlinkReader::begin() {
    Serial2.begin(MAVLINK_BAUD, SERIAL_8N1, MAVLINK_RX_PIN, MAVLINK_TX_PIN);
}

void MavlinkReader::update() {
    mavlink_message_t msg;
    mavlink_status_t  status;

    while (Serial2.available()) {
        uint8_t c = (uint8_t)Serial2.read();
        if (mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &status)) {
            _handleMessage(msg);
        }
    }
}

void MavlinkReader::_handleMessage(const mavlink_message_t& msg) {
    switch (msg.msgid) {

        case MAVLINK_MSG_ID_HEARTBEAT: {
            mavlink_heartbeat_t hb;
            mavlink_msg_heartbeat_decode(&msg, &hb);
            // GCS heartbeat'lerini filtrele
            if (hb.type == MAV_TYPE_GCS) break;
            _data.armed       = (hb.base_mode & MAV_MODE_FLAG_SAFETY_ARMED) ? 1 : 0;
            _data.flight_mode = (uint8_t)(hb.custom_mode & 0xFF);
            break;
        }

        case MAVLINK_MSG_ID_GLOBAL_POSITION_INT: {
            mavlink_global_position_int_t pos;
            mavlink_msg_global_position_int_decode(&msg, &pos);
            _data.lat          = pos.lat;
            _data.lon          = pos.lon;
            _data.alt          = pos.alt;
            _data.relative_alt = pos.relative_alt;
            _data.vx           = pos.vx;
            _data.vy           = pos.vy;
            _data.hdg          = pos.hdg;
            break;
        }

        case MAVLINK_MSG_ID_ATTITUDE: {
            mavlink_attitude_t att;
            mavlink_msg_attitude_decode(&msg, &att);
            _data.roll  = att.roll;
            _data.pitch = att.pitch;
            _data.yaw   = att.yaw;
            break;
        }

        case MAVLINK_MSG_ID_VFR_HUD: {
            mavlink_vfr_hud_t hud;
            mavlink_msg_vfr_hud_decode(&msg, &hud);
            _data.airspeed    = hud.airspeed;
            _data.groundspeed = hud.groundspeed;
            _data.climb       = hud.climb;
            break;
        }

        case MAVLINK_MSG_ID_SYS_STATUS: {
            mavlink_sys_status_t sys;
            mavlink_msg_sys_status_decode(&msg, &sys);
            _data.vbat_mv     = sys.voltage_battery;
            _data.current_ca  = sys.current_battery;   // centiamps
            _data.battery_pct = sys.battery_remaining;
            break;
        }

        case MAVLINK_MSG_ID_DISTANCE_SENSOR: {
            mavlink_distance_sensor_t ds;
            mavlink_msg_distance_sensor_decode(&msg, &ds);
            _data.lidar_cm = ds.current_distance;
            break;
        }

        default:
            break;
    }
}
