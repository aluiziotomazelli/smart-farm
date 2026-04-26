# Water Tank Application Refactoring Plan

## Architecture Overview

The current `WaterTankApp` is a monolithic "God Object". It handles hardware initialization, sensor readings, mathematical conversions, ESP-NOW communication, NVS storage, and OS-level sleep management. 

We will break this down into three distinct layers:
1. **Hardware Abstraction Layer (Interfaces):** Decouples the application from specific sensors (e.g., Ultrasonic vs. Pressure).
2. **Business Logic & Math Layer (Pure C++):** Contains the rules for sleep cycles, backup modes, and tank geometry. This layer will have **zero** ESP-IDF dependencies and will be fully testable on the host.
3. **Application/Orchestration Layer:** Glues the hardware implementations to the business logic and handles OS-level tasks (Deep Sleep, FreeRTOS delays).

---

## Detailed Class Breakdown

### 0. Core Component Abstractions
Located in `components/nvs_core/`

*   **`IHalNvs`**
    *   **Responsibility:** 1:1 Wrapper for ESP-IDF NVS functions (`nvs_open`, `nvs_get_blob`, etc.).
    *   **Why it exists:** Allows `NvsCore` to be tested on the host by mocking the underlying Flash interaction.

### 1. Hardware Abstraction Interfaces
Located in `app_water_tank/main/include/interfaces/`

*   **`ILevelSensor`**
    *   **Responsibility:** Provides the raw distance/level reading.
    *   **Why it exists:** Allows swapping the `UltrasonicSensor` for a `PressureSensor` in the future without changing the core application logic.
    *   **Key Methods:** `read_raw_distance_cm(float& out_cm, uint8_t& out_quality, uint8_t& out_failure)`

*   **`IFloatSwitch`**
    *   **Responsibility:** Provides the physical status of the backup float switch.
    *   **Why it exists:** Abstracts the hardware GPIO reads so we can mock the switch state during unit tests.
    *   **Key Methods:** `is_active()`, `should_enable_wakeup()`

*   **`IWaterTankStorage`**
    *   **Responsibility:** Handles persistence of application state (RTC memory and NVS).
    *   **Why it exists:** Prevents the application logic from interacting directly with the `NvsCore` or ESP-IDF RTC attributes, allowing us to mock storage states in tests.

### 2. Math & Geometry Layer
Located in `app_water_tank/main/include/`

*   **`TankGeometry`**
    *   **Responsibility:** Converts raw sensor data (e.g., distance in cm) into standardized volume metrics (0-1000 permille).
    *   **Why it exists:** The tank is an inverted cone. The math required to convert distance to volume is non-linear. Isolating this math in its own class makes it easy to unit test the formulas and swap the class if the physical tank is replaced with a cylindrical one.
    *   **Key Methods:** `calculate_permille(float distance_cm)`

### 3. Business Logic Layer
Located in `app_water_tank/main/include/`

*   **`WaterTankLogic`** (or `WaterTankController`)
    *   **Responsibility:** The "Brain". It evaluates current sensor readings, applies the `TankGeometry`, and decides the system's next state. It determines if Backup Mode should be active and calculates the optimal sleep duration.
    *   **Why it exists:** This isolates the complex decision-making process from the hardware and the OS. This class will be the primary target of GTest unit testing.
    *   **Key Methods:** `process_reading(...)`, `get_recommended_sleep_time_us()`, `is_backup_mode_active()`

### 4. Application / Orchestrator Layer
Located in `app_water_tank/main/include/`

*   **`WaterTankApp`**
    *   **Responsibility:** The orchestrator. It receives all the interfaces via its constructor (Dependency Injection). In its `run()` loop, it reads from the `ILevelSensor`, passes the data to `WaterTankLogic`, tells `IEspNowManager` to transmit the report, and finally calls `esp_deep_sleep_start()`.
    *   **Why it exists:** It bridges the pure C++ logic with the ESP-IDF OS environment.
    *   **Dependencies:** `ILevelSensor&`, `IFloatSwitch&`, `IWaterTankStorage&`, `IEspNowManager&` (Constructor Injection).

---

## Implementation Steps

0.  **NVS HAL Abstraction:** Update `NvsCore` to use `IHalNvs` via Dependency Injection.
1.  **Define Interfaces:** Create `ILevelSensor.hpp`, `IFloatSwitch.hpp`, and `IWaterTankStorage.hpp`.
2.  **Create Adapters:** Wrap the existing `UltrasonicSensor` and `FloatSwitch` classes in lightweight adapter classes that implement the new interfaces (if they don't already).
3.  **Implement `TankGeometry`:** Move the `distance_to_level_permille` logic into this new class and update the math for the inverted cone shape.
4.  **Extract `WaterTankLogic`:** Move the state machine (Backup Mode logic, Sleep calculation logic) out of `WaterTankApp` and into this new testable class.
5.  **Refactor `WaterTankApp`:** Update the constructor to use Dependency Injection. Clean up the `run()` method to act solely as an orchestrator.
6.  **Update `main.cpp`:** Instantiate the concrete hardware classes (Sensor, NVS, Comm) and inject them into the `WaterTankApp` instance.

## Verification Plan
*   **Host Tests (Future):** We will be able to create `test_water_tank_logic.cpp` using GMock to pass fake sensor readings and verify that `WaterTankLogic` calculates the correct sleep time and backup state.
*   **Target Tests:** Compile and flash to the ESP32 to ensure the refactored architecture behaves identically to the original implementation in a real-world scenario.
