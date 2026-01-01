#include "ota_sender_app.hpp"

extern "C" void app_main(void)
{
    OtaSenderApp app;
    app.init();
    app.run();
}
