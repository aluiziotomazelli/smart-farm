#pragma once

#include <cstddef>
#include <cstdint>
#include "driver/gpio.h"

/**
 * @brief Quality classification for ultrasonic measurements.
 */
enum class UsQuality
{
    OK,     /**< Valid measurement within expected range */
    WEAK,   /**< Measurement may be less reliable */
    INVALID /**< Measurement is invalid or out of bounds */
};

/**
 * @brief Failure modes for ultrasonic measurement operations.
 */
enum class UsFailure
{
    NONE,    /**< No failure occurred */
    TIMEOUT, /**< No echo received within timeout period */
    HW_ERROR /**< Electrical or driver-level error */
};

/**
 * @brief Ultrasonic distance measurement sensor driver.
 *
 * This class provides a interface for ultrasonic distance measurement
 * using GPIO method. It implements configurable multi-ping measurement
 * strategies with statistical filtering to improve measurement
 * reliability and accuracy.
 */
class UltrasonicSensor
{
public:
    /**
     * @brief Filtering algorithms for distance measurement processing.
     */
    enum class Filter
    {
        MEDIAN,          /**< Median value of multiple measurements */
        DOMINANT_CLUSTER /**< Most frequent value cluster in measurements */
    };

    /**
     * @brief Configuration structure for ultrasonic sensor operation.
     */
    struct UltrasonicConfig
    {
        uint8_t ping_count = 7; /**< Number of pings per measurement (max: 15) */
        const uint16_t ping_interval_ms =
            70; /**< Delay between consecutive pings in milliseconds */
        const uint16_t ping_duration_us =
            20; /**< Duration of trigger pulse in microseconds */
        const uint32_t timeout_us = 30000; /**< Maximum wait time for echo in microseconds */
        Filter filter             = Filter::MEDIAN; /**< Statistical filter to apply */
        bool blind_ping           = true; /**< If true, ignores first ping to clear environment */
    };

    /**
     * @brief Constructs an UltrasonicSensor instance.
     *
     * @param trig_pin GPIO pin number for trigger signal output.
     * @param echo_pin GPIO pin number for echo signal input.
     * @param cfg Configuration parameters for sensor operation.
     */
    UltrasonicSensor(gpio_num_t trig_pin,
                     gpio_num_t echo_pin,
                     const UltrasonicConfig &cfg);

    /**
     * @brief Default destructor.
     */
    ~UltrasonicSensor() = default;

    /**
     * @brief Initializes the sensor hardware and GPIO/RMT peripherals.
     *
     * @return true if initialization succeeded, false otherwise.
     */
    bool init();

    /**
     * @brief Performs distance measurement with quality and failure reporting.
     *
     * Executes multiple pings according to configuration, applies statistical
     * filtering, and returns the processed distance measurement.
     *
     * @param[out] out_cm Calculated distance in centimeters.
     * @param[out] out_quality Quality classification of the measurement.
     * @param[out] out_failure Type of failure if measurement unsuccessful.
     * @return true if a valid measurement was obtained, false otherwise.
     */
    bool read_distance_cm(float &out_cm, UsQuality &out_quality, UsFailure &out_failure);

    /**
     * @brief Sets the number of pings for subsequent measurements.
     *
     * @param new_ping_count The new number of pings (will be capped by MAX_PINGS).
     */
    void setPingCount(uint8_t new_ping_count);

private:
    static constexpr size_t MAX_PINGS = 15; /**< Maximum allowed pings per measurement */
    static constexpr float SOUND_SPEED_CM_PER_US = 0.034300f; /**< in cm/μs at 20°C */
    static constexpr float US_VALID_PING_RATIO =
        0.7f; /**< Minimum ratio of valid pings for reliable measurement */

    /**
     * @brief Performs a single ping measurement.
     *
     * Triggers the sensor and measures the echo pulse duration to calculate
     * distance for a single ping operation.
     *
     * @param[out] cm Distance in centimeters from single ping.
     * @param[out] fail Failure type if ping unsuccessful.
     * @return true if ping succeeded, false otherwise.
     */
    bool read_raw_cm_(float &cm, UsFailure &fail);

    UltrasonicConfig cfg_;             /**< Configuration parameters for sensor operation */
    const gpio_num_t trig_pin_;        /**< GPIO pin for trigger signal */
    const gpio_num_t echo_pin_;        /**< GPIO pin for echo signal */

    /**
     * @brief Applies median filter to measurement array.
     *
     * @param v Array of distance measurements.
     * @param n Number of elements in array.
     * @return Median distance value in centimeters.
     */
    float reduce_median(float *v, size_t n);

    /**
     * @brief Applies dominant cluster filter to measurement array.
     *
     * Identifies the most frequent cluster of similar values in the
     * measurement set and returns its representative value.
     *
     * @param v Array of distance measurements.
     * @param n Number of elements in array.
     * @return Dominant cluster distance value in centimeters.
     */
    float reduce_dominant_cluster(float *v, size_t n);
};