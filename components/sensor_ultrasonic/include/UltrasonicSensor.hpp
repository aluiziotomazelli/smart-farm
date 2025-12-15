#pragma once
#include <cstddef>
#include <cstdint>

class UltrasonicSensor
{
public:
    enum class Filter
    {
        MEDIAN,
        DOMINANT_CLUSTER
    };

    struct UltrasonicConfig
    {
        uint8_t  ping_count       = 9;
        uint16_t ping_interval_ms = 70;
        uint16_t ping_duration_us = 20;
        uint32_t timeout_us       = 25000;
        Filter   filter           = Filter::DOMINANT_CLUSTER;
        bool     blind_ping       = false;
    };

    explicit UltrasonicSensor(const UltrasonicConfig &cfg)
        : cfg(cfg)
    {
    }
    virtual ~UltrasonicSensor() = default;

    virtual bool init() = 0;

    bool readDistanceCm(float &out_cm); // template method

protected:
    UltrasonicConfig cfg;

    virtual float readRawDistanceCm() = 0;

    float reduce_median(float *samples, size_t n);
    float reduce_dominant_cluster(float *samples, size_t n);
};
