# FloatSwitch Component

The `FloatSwitch` component provides a robust abstraction for float level sensors. It translates electrical GPIO signals into meaningful tank states (Full/Empty) while handling hardware debouncing and power-efficient wake-up logic for Deep Sleep cycles.

## Logic & Architecture

The component operates by decoupling the physical wiring from the application logic. It uses a majority-vote sampling algorithm to ensure that electrical noise doesn't trigger false state changes.

### Key Enumerations (`hpp`)

To provide flexibility for different hardware setups, the component uses three main enums:

1.  **`ActiveLevel`**: Defines the electrical state when the switch is physically closed.
    * `LOW`: The contact pulls the signal to GND (requires internal Pull-Up).
    * `HIGH`: The contact pulls the signal to VCC (requires internal Pull-Down).

2.  **`WakeupCondition`**: Defines when the system should wake up from Deep Sleep.
    * `NEVER`: Never Wakeup.
    * `WHEN_TANK_IS_EMPTY`: Wake up only when the water level drops.
    * `WHEN_TANK_IS_FULL`: Wake up only when the water level rises.

3.  **`normally_open` (Boolean Logic)**: 
    * If `true` (NO): A closed contact means the tank is **Full**.
    * If `false` (NC): A closed contact means the tank is **Empty**.



## Anti-Loop Wakeup Logic

One of the most critical features is the `shouldEnableWakeup()` method. It prevents the ESP32 from entering a "Wakeup Loop" (where the device wakes up, sees the trigger condition is already met, goes to sleep, and immediately wakes up again).

* **Logic**: It compares the **current stable state** with the **desired wakeup condition**.
* **Example**: If configured to wake up `WHEN_TANK_IS_EMPTY`, the function will only return `true` if the tank is currently `FULL`. If the tank is already empty, it prevents arming the trigger to save power and prevent useless wake cycles.

## Implementation Example

```cpp
#include "float_switch.hpp"

// Configuration for a sensor that pulls to GND when closed (NO)
FloatSwitch::Config cfg = {
    .gpio = GPIO_NUM_4,
    .normally_open = true,                          // Normally Open
    .active_level = FloatSwitch::ActiveLevel::LOW,  // Pulls to GND
    .wakeup_on = FloatSwitch::WakeupCondition::WHEN_TANK_IS_EMPTY
};

FloatSwitch sensor(cfg);

void app_main() {
    if (sensor.init() == ESP_OK) {
        if (sensor.isTankFull()) {
            // Business logic for full tank
        }
        
        // Check if we should arm the wake-up pin before Deep Sleep
        bool arm_gpio = sensor.shouldEnableWakeup();
    }
}
```

### Technical Specifications

* **Debouncing:** Performs 5 samples with a 5ms delay between each, requiring a majority (3/5) for state confirmation. This results in a stable 25ms filter window.

* **Validation:** Integrates with GpioValidator to ensure the selected pin is compatible with the requested Pull-Up/Down configuration.

* **Memory Safety:** Full Lifecycle management with deinit() called on destruction to reset GPIO states.


## Unit Testing

This component includes a comprehensive test suite located in the `test/` directory, following the ESP-IDF unit testing pattern. The tests verify electrical logic, timing accuracy for debouncing, and power management rules.

### Test Categories
- `[lifecycle]`: Verifies memory-safe initialization and deinitialization.
- `[logic]`: Validates the truth table for all `NO/NC` and `ActiveLevel` combinations.
- `[timing]`: Measures the majority-vote sampling delay (expected ~25ms).
- `[wakeup]`: Tests the anti-loop logic for Deep Sleep triggers.

### Running the Tests
To execute the tests, use the ESP-IDF Unit Test App. It is recommended to connect the `INPUT_PIN` to a `CTRL_PIN` (defined in the test file) via a jumper wire to allow automated signal simulation.

1. Configure the `tool/unit-test-app` to include this component.
2. Build and flash the test application:
   ```bash
   idf.py -T float_switch flash monitor