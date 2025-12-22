#include "protocol_codec.hpp"

#include <cstring>

namespace protocol {

/* =========================================================
 * Encode
 * ========================================================= */
protocol::CodecResult protocol::encode_frame(const Frame &frame,
                                             uint8_t     *out_buffer,
                                             size_t       out_len,
                                             size_t      &written_len)
{
    written_len = 0;

    if (!out_buffer)
        return CodecResult::INVALID_ARGUMENT;

    if (!is_header_valid(frame.header))
        return CodecResult::UNSUPPORTED_VERSION;

    if (!is_payload_size_valid(frame.payload_len))
        return CodecResult::INVALID_FRAME;

    const size_t total_size = sizeof(WireHeader) + frame.payload_len;

    if (out_len < total_size)
        return CodecResult::BUFFER_TOO_SMALL;

    // copy header
    std::memcpy(out_buffer, &frame.header, sizeof(WireHeader));

    // copy payload (if any)
    if (frame.payload_len > 0) {
        std::memcpy(out_buffer + sizeof(WireHeader), frame.payload, frame.payload_len);
    }

    written_len = total_size;
    return CodecResult::OK;
}

/* =========================================================
 * Decode
 * ========================================================= */
protocol::CodecResult protocol::decode_frame(const uint8_t *buffer,
                                             size_t         buffer_len,
                                             Frame         &out_frame)
{
    if (!buffer)
        return CodecResult::INVALID_ARGUMENT;

    if (buffer_len < sizeof(WireHeader))
        return CodecResult::INVALID_FRAME;

    // copia bytes do header para struct
    const WireHeader *hdr = reinterpret_cast<const WireHeader *>(buffer);

    if (!is_header_valid(*hdr))
        return CodecResult::UNSUPPORTED_VERSION;

    const size_t payload_len = buffer_len - sizeof(WireHeader);

    if (!is_payload_size_valid(payload_len))
        return CodecResult::INVALID_FRAME;

    out_frame.header      = *hdr;
    out_frame.payload_len = payload_len;

    if (payload_len > 0) {
        memcpy(out_frame.payload, buffer + sizeof(WireHeader), payload_len);
    }

    return CodecResult::OK;
}

} // namespace protocol
