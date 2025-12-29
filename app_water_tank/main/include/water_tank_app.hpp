#pragma once

#include "espnow_comm.hpp"
#include "water_tank_nvs.hpp"
#include "water_tank_types.hpp"

/**
 * @class WaterTankApp
 * @brief Main application class for the Water Tank monitoring device.
 *
 * This class orchestrates the hardware components, data processing,
 * communication, and sleep cycles for the water tank sensor.
 */
class WaterTankApp
{
public:
    /**
     * @brief Construct a new Water Tank App object.
     */
    WaterTankApp();

    /**
     * @brief Initializes all hardware, NVS, and communication components.
     */
    void init();

    /**
     * @brief Contains the main application loop.
     */
    void run();

    /**
     * @brief Creates a comprehensive report with sensor and float switch data.
     * @return A WaterLevelReport struct containing the measurement data.
     */
    WaterLevelReport createWaterLevelReport();

    /**
     * @brief Updates the operation mode (normal vs. backup) based on the current state.
     */
    void updateOperationMode();

    /**
     * @brief Sends the water level report to the central hub via ESP-NOW.
     * @param report The WaterLevelReport to send.
     */
    void sendWaterLevelReport(const WaterLevelReport &report);

    /**
     * @brief Determines the optimal deep sleep duration based on the current tank fill
     * state.
     * @return The sleep duration in microseconds.
     */
    uint64_t decideSleepTimeUs();

    /**
     * @brief Configures the ESP32's wakeup sources (Timer and GPIO) for the next sleep
     * cycle.
     * @param timer_us The sleep duration in microseconds.
     */
    void configureSleepPolicy(uint64_t timer_us);

    /**
     * @brief Callback function for handling incoming ESP-NOW messages.
     * @param node_id The ID of the sending node.
     * @param data Pointer to the received data.
     * @param len Length of the received data.
     * @param rssi The Received Signal Strength Indicator.
     */
    void onEspNowReceive(uint8_t node_id, const uint8_t *data, int len, int8_t rssi);

    /**
     * @brief Callback function for handling ESP-NOW send status.
     * @param node_id The ID of the destination node.
     * @param status The status of the send operation.
     */
    void onEspNowSend(uint8_t node_id, esp_now_send_status_t status);

private:
    WaterTankNvs storage_; ///< Handles persistence of application data and statistics.
    EspNowComm comm_;      ///< Manages ESP-NOW communication with the central hub.
};
