# OTA Manager

A passive, dependency-injected OTA (Over-the-Air) update component designed for ESP-IDF.

## Architecture

This component follows a domain-oriented HAL (Hardware Abstraction Layer) architecture, ensuring that core orchestration logic remains decoupled from platform-specific APIs and is fully testable on the host.

### Key Features
- **Passive Design:** OTA process is triggered and owned by the application.
- **Dependency Injection:** All hardware/SDK dependencies are injected via interfaces, enabling host-based unit testing.
- **Robustness:** Includes version gate, hash verification, and rollback validation.
- **Task-Based:** Uses a dedicated worker task with lightweight FreeRTOS notifications.

## Directory Structure

- `include/interfaces/`: Public contracts for the OTA manager and its HAL dependencies.
- `include/`: Public DTOs, configurations, and the main `OtaManager` header.
- `src/`: Implementation files and thin HAL adapters.

## How to Test

Tests are located in `host_test/test_water_tank/` (or similar depending on the integration). Use GTest and Google Mock to verify the `OtaManager` orchestration logic by injecting mocks for the HAL interfaces.
