#include "central_hub_app.hpp"

extern "C" void app_main()
{
    CentralHubApp app;
    app.init();
    app.run();
}
