#include "comm_interface.hpp"
#include "comm_espnow.hpp"

namespace comm {

CommInterface& CommInterface::get_default_instance()
{
    return CommEspNow::instance();
}

} // namespace comm
