#pragma once

#include "protocol_types.hpp"
#include <cstddef>
#include <cstdint>
#include "driver/gpio.h"

/**
 * @brief Converts a UsFailure enum to a human-readable string.
 * @param failure The failure code to convert.
 * @return A constant string describing the failure.
 */
const char *us_failure_to_string(UsFailure failure);

/**
 * @brief Ultrasonic distance measurement sensor driver.
 *
 * @details This class provides a robust interface for ultrasonic distance
 * measurement. It implements a configurable multi-ping strategy with statistical
 * filtering to improve measurement reliability and accuracy, especially in noisy
 * environments.
 */
class UltrasonicSensor
{
#ifdef UNIT_TEST
    friend class UltrasonicTestAccessor;
#endif

public:
    /**
     * @brief Statistical filtering algorithms for processing raw distance
     * measurements.
     */
    enum class Filter
    {
        MEDIAN, /**< Selects the median value from a series of measurements. */
        DOMINANT_CLUSTER, /**< Finds the most frequent cluster of values and returns
                             its average. */
    };

    /**
     * @brief Configuration parameters for the ultrasonic sensor.
     */
    struct UltrasonicConfig
    {
        uint8_t ping_count =
            7; /**< Number of pings per measurement cycle (max: 15). */
        const uint16_t ping_interval_ms =
            70; /**< Delay between consecutive pings in milliseconds. */
        const uint16_t ping_duration_us =
            20; /**< Duration of the trigger pulse in microseconds. */
        const uint32_t timeout_us =
            30000; /**< Maximum wait time for an echo pulse in microseconds. */
        Filter filter =
            Filter::MEDIAN; /**< Statistical filter to apply to the measurements. */
        bool blind_ping = true; /**< If true, performs and discards one ping before
                                   the main cycle. */
        const float min_distance_cm =
            10.0f; /**< The minimum valid distance in centimeters. */
        const float max_distance_cm =
            200.0f;               /**< The maximum valid distance in centimeters. */
        float max_dev_cm = 15.0f; /**< The maximum standard deviation allowed for a
                                     valid reading. */
        const uint16_t warmup_time_ms = 600;
    };

    /**
     * @brief Constructs an UltrasonicSensor instance.
     *
     * @param trig_pin GPIO pin number for the trigger signal output.
     * @param echo_pin GPIO pin number for the echo signal input.
     * @param cfg Configuration parameters for the sensor's operation.
     */
    UltrasonicSensor(const gpio_num_t trig_pin,
                     const gpio_num_t echo_pin,
                     UltrasonicConfig &cfg);

    /**
     * @brief Default destructor.
     */
    ~UltrasonicSensor() = default;

    /**
     * @brief Initializes the sensor's hardware interface.
     *
     * @return true if initialization succeeded, false otherwise.
     */
    bool init();

    /**
     * @brief Performs a complete distance measurement cycle.
     *
     * @details Executes multiple pings as configured, applies statistical filtering,
     * and reports the final distance along with its quality and any failures.
     *
     * @param[out] out_cm The calculated distance in centimeters.
     * @param[out] out_quality The quality classification of the measurement.
     * @param[out] out_failure The type of failure, if the measurement was
     * unsuccessful.
     * @return true if a valid measurement was obtained, false otherwise.
     */
    bool readDistance_cm(float &out_cm,
                         UsQuality &out_quality,
                         UsFailure &out_failure);

    /**
     * @brief Sets the number of pings for subsequent measurements.
     *
     * @param new_ping_count The new number of pings (will be capped by MAX_PINGS).
     */
    void setPingCount(uint8_t new_ping_count);

private:
    UltrasonicConfig cfg_; /**< Sensor configuration parameters. */
    gpio_num_t trig_pin_;  /**< GPIO pin for the trigger signal. */
    gpio_num_t echo_pin_;  /**< GPIO pin for the echo signal. */

    static constexpr size_t MAX_PINGS =
        15; /**< Maximum configurable pings per measurement. */
    static constexpr float SOUND_SPEED_CM_PER_US =
        0.0343f; /**< Speed of sound in cm/μs at 20°C. */
    static constexpr float US_VALID_PING_RATIO =
        0.7f; /**< Minimum ratio of valid pings for an 'OK' measurement. */
    static constexpr float US_INVALID_PING_RATIO =
        0.4f; /**< Minimum ratio of valid pings for any valid measurement. */

    /**
     * @brief Performs a single ping-and-read operation.
     *
     * @details Triggers the sensor and measures the echo pulse duration to calculate
     * the raw distance for a single ping.
     *
     * @param[out] cm The measured distance in centimeters.
     * @param[out] fail The failure type if the ping was unsuccessful.
     * @return true if the ping succeeded, false otherwise.
     */
    bool readRaw_cm_(float &cm, UsFailure &fail);

    /**
     * @brief Applies a median filter to an array of measurements.
     *
     * @param v Pointer to the array of distance measurements.
     * @param n The number of elements in the array.
     * @return The median distance value in centimeters.
     */
    float reduceMedian(float *v, size_t n);

    /**
     * @brief Applies a dominant cluster filter to an array of measurements.
     *
     * @details Identifies the largest cluster of similar values in the
     * measurement set and returns the average of that cluster.
     *
     * @param v Pointer to the array of distance measurements.
     * @param n The number of elements in the array.
     * @return The dominant cluster's average distance in centimeters.
     */
    float reduceDominantCluster(float *v, size_t n);

    /**
     * @brief Checks if the variance of the measured distances is within the allowed
     * range.
     *
     * @param samples Pointer to the array of distance measurements.
     * @param valids The number of valid measurements.
     * @param ratio The ratio of max deviation.
     * @return true if the variance is within the allowed range, false otherwise.
     */
    float getStdDev(float *samples, uint8_t valids);
};
