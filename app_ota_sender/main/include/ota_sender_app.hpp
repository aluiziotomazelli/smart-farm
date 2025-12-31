#pragma once

#include "espnow_comm.hpp"

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
    bool command_sent_;
};
