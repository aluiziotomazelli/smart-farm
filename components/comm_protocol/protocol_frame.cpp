// components/protocol/protocol_frame.cpp

#include "protocol_frame.hpp"

namespace protocol {

bool validate_frame(const Frame &frame)
{
    if (frame.header.version != PROTOCOL_VERSION_WIRE)
        return false;

    if (frame.payload.size() > MAX_PAYLOAD_SIZE)
        return false;

    if (frame.header.type == MessageType::INVALID)
        return false;

    return true;
}

size_t frame_size(const Frame &frame)
{
    if (frame.payload.size() > MAX_PAYLOAD_SIZE)
        return 0;

    return sizeof(WireHeader) + frame.payload.size();
}

} // namespace protocol
