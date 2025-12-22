#pragma once

class CentralHubApp
{
public:
    static CentralHubApp& instance();
    void init();
    void run();

private:
    CentralHubApp() = default;
};
