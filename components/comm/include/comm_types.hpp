#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace comm {

enum class CommStatus : uint8_t
{
    UNINITIALIZED,
    READY,
    RUNNING,
    ERROR
};

enum class CommError : uint8_t
{
    NONE,
    INIT_FAILED,
    NOT_READY,
    SEND_FAILED,
    RECEIVE_FAILED,
    INTERNAL_ERROR
};

struct CommStats
{
    uint32_t tx_ok   = 0;
    uint32_t tx_fail = 0;
    uint32_t rx_ok   = 0;
    uint32_t rx_drop = 0;
};

struct CommMessage
{
    uint16_t               type    = 0;
    std::vector<uint8_t> payload;
    uint32_t               flags   = 0;
};

} // namespace comm
