#pragma once

#include "espnow_comm.hpp"
#include "wifi_manager.hpp"

class OtaSenderApp
{
public:
    OtaSenderApp();

    void init();
    void run();

private:
    void onPeerEvent(const PeerInfo &peer, bool added);
    void sendOtaCommand(uint8_t node_id);

    EspNowComm comm_;
    WiFiManager *wifi_manager_;
    bool command_sent_;
};
