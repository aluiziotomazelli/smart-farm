#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol_frame.hpp"

namespace protocol {

/* =========================================================
 * Codec result
 * ========================================================= */
enum class CodecResult : uint8_t
{
    OK = 0,
    INVALID_ARGUMENT,
    BUFFER_TOO_SMALL,
    INVALID_FRAME,
    UNSUPPORTED_VERSION
};

/* =========================================================
 * Encoding
 * Frame -> raw bytes
 * ========================================================= */

/**
 * Encode a protocol Frame into a raw byte buffer.
 *
 * @param frame        Logical frame (header + payload pointer)
 * @param out_buffer   Destination buffer
 * @param out_len      Size of destination buffer
 * @param written_len  Number of bytes written
 *
 * @return CodecResult
 */
CodecResult encode_frame(const Frame &frame,
                         uint8_t     *out_buffer,
                         size_t       out_len,
                         size_t      &written_len);

/* =========================================================
 * Decoding
 * Raw bytes -> Frame
 * ========================================================= */

/**
 * Decode raw bytes into a protocol Frame.
 *
 * Payload pointer will reference memory inside input buffer.
 *
 * @param buffer     Raw input buffer
 * @param buffer_len Length of input buffer
 * @param out_frame  Decoded frame (non-owning payload)
 *
 * @return CodecResult
 */
CodecResult decode_frame(const uint8_t *buffer, size_t buffer_len, Frame &out_frame);

} // namespace protocol
