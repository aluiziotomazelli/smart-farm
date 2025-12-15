#pragma once

#include <cstddef>

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
        size_t ping_count;
        size_t ping_interval_ms;
        size_t ping_duration_us;
        size_t timeout_us;
        Filter filter;
        bool   blind_ping;
    };

    UltrasonicSensor(const UltrasonicConfig &cfg)
        : cfg(cfg){};
    virtual ~UltrasonicSensor() = default;

    virtual bool init() = 0;
    bool readDistanceCm(float &out_cm);

protected:
    virtual float readRawDistanceCm() = 0;

private:
    float reduce_median(float *v, size_t n);
    float reduce_dominant_cluster(float *v, size_t n);

protected:
    const UltrasonicConfig &cfg;
};
