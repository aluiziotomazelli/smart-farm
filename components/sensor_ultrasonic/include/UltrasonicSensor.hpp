#pragma once

#include <cstddef>
#include <cstdint>

static constexpr uint32_t ULTRASONIC_WARMUP_MS = 600;
static constexpr float    US_VALID_PING_RATIO  = 0.7f;

enum class UsQuality
{
    OK,
    WEAK,
    INVALID
};

enum class UsFailure
{
    NONE,    // não houve falha
    TIMEOUT, // nenhum eco
    HW_ERROR // erro elétrico / driver
};
class UltrasonicSensor
{
public:
    static constexpr float SOUND_SPEED_CM_PER_US = 0.034300f;

    enum class Filter
    {
        MEDIAN,
        DOMINANT_CLUSTER
    };

    struct UltrasonicConfig
    {
        uint8_t  ping_count       = 7;
        uint16_t ping_interval_ms = 70;
        uint16_t ping_duration_us = 20;
        uint32_t timeout_us       = 25000;
        Filter   filter           = Filter::DOMINANT_CLUSTER;
        bool     blind_ping       = true;
    };

    UltrasonicSensor(const UltrasonicConfig &cfg)
        : cfg(cfg) {};
    virtual ~UltrasonicSensor() = default;

    virtual bool init() = 0;
    bool         readDistanceCm(float &out_cm, UsQuality &out_quality, UsFailure &out_failure);

protected:
    virtual bool readRawDistanceCm(float &out_cm, UsFailure &out_failure) = 0;

    UsFailure last_failure = UsFailure::NONE;

private:
    float reduce_median(float *v, size_t n);
    float reduce_dominant_cluster(float *v, size_t n);

protected:
    const UltrasonicConfig cfg;
};
