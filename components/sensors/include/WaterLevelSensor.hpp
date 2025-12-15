#pragma once

class WaterLevelSensor
{
public:
    virtual ~WaterLevelSensor() = default;

    virtual bool  init()             = 0;
    virtual float readLevelPercent() = 0;
};