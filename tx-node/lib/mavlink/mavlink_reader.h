#pragma once
#include <Arduino.h>
#include <ardupilotmega/mavlink.h>
#include "mavlink_data.h"  // MavlinkData struct — common/mavlink_data.h

class MavlinkReader {
public:
    MavlinkReader();

    // UART2 başlat: GPIO16(RX), GPIO17(TX), 57600 baud
    void begin();

    // Gelen byte'ları oku ve parse et — loop'ta her iterasyonda çağır
    void update();

    const MavlinkData& getData() const { return _data; }

private:
    MavlinkData _data;

    // Çözümlenen mesajı ilgili alana yaz
    void _handleMessage(const mavlink_message_t& msg);
};
