#include "test_app.hpp"

extern "C" void app_main(void)
{
    TestApp app;
    app.init();
    app.run();
}
