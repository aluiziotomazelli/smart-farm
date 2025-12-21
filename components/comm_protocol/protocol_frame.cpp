// components/protocol/protocol_frame.cpp

#include "protocol_frame.hpp"

namespace protocol {

bool validate_frame(const WireHeader &header, size_t payload_len)
{
    if (header.version != PROTOCOL_VERSION_WIRE)
        return false;

    if (payload_len > MAX_PAYLOAD_SIZE)
        return false;

    if (header.type == MessageType::INVALID)
        return false;

    return true;
}

size_t frame_size(size_t payload_len)
{
    if (payload_len > MAX_PAYLOAD_SIZE)
        return 0;

    return sizeof(WireHeader) + payload_len;
}

} // namespace protocol
