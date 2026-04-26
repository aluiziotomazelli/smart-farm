#include "water_tank_app.hpp"
#include "ultrasonic_adapter.hpp"
#include "floatswitch.hpp"
#include "water_tank_storage_adapter.hpp"
#include "tank_geometry.hpp"
#include "water_tank_logic.hpp"
#include "espnow_manager.hpp"
#include "power_control.hpp"
#include "hal_nvs.hpp" // Adicionado para HalNvs

#include "esp_log.h"

static const char *TAG = "main";

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Initializing Smart Farm Water Tank...");

    // 1. Hardware Initialization (Power Control)
    power_control::PowerControl power(GPIO_NUM_20); // POWER_GPIO
    power.init();
    power.turn_on();

    // 2. Physical Drivers
    // Ultrasonic Sensor
    ultrasonic::UsConfig us_cfg;
    us_cfg.min_distance_cm = 20.0f;
    us_cfg.max_distance_cm = 200.0f;
    ultrasonic::UsSensor sensor(GPIO_NUM_21, GPIO_NUM_7, us_cfg); // TRIGGER, ECHO
    sensor.init();

    // Float Switch
    floatswitch::FloatSwitch fs(GPIO_NUM_1, floatswitch::ActiveLevel::LOW); // GPIO, ActiveLevel
    fs.init();

    // NVS Storage
    HalNvs hal; // HAL para NVS
    WaterTankNvs nvs(hal); // Injeção do HAL
    nvs.init_partition();

    // ESP-NOW Communication
    EspNowManager &comm = EspNowManager::instance();
    EspNowConfig comm_cfg = {}; // Default config
    comm.init(comm_cfg);

    // 3. Adapters (Interfaces)
    UltrasonicLevelSensorAdapter sensor_adapter(sensor);
    WaterTankStorageAdapter storage_adapter(nvs);

    // 4. Logic & Geometry
    TankGeometry geometry(LEVEL_MIN_CM, LEVEL_MAX_CM);
    WaterTankLogic logic(geometry, fs);

    // 5. Orchestrator
    WaterTankApp app(sensor_adapter, fs, storage_adapter, comm, logic);

    // 6. Run Application
    app.run();
}
