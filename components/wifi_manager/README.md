# WiFiManager Component

A robust, thread-safe WiFi Station manager for ESP-IDF (v5.x).

## Overview

The `WiFiManager` simplifies WiFi operations on the ESP32 by wrapping the low-level `esp_wifi` driver in a state-machine driven singleton. It handles NVS initialization, event propagation, and provides both synchronous (blocking) and asynchronous (non-blocking) APIs.

## Features

- **Singleton Pattern**: Easy access from anywhere in the application.
- **Dedicated Task**: WiFi operations are decoupled from the main application thread.
- **Automatic Reconnection**: Built-in exponential backoff retry loop for accidental disconnections.
- **Automatic Rollback**: Handles timeouts and connection failures gracefully.
- **Thread-Safe**: Uses Mutexes and Queues for concurrency protection.
- **Persistent Credentials**: Built-in methods for storing/loading credentials in NVS.

## Quick Start

```cpp
#include "wifi_manager.hpp"

void app_main() {
    auto &wm = WiFiManager::instance();

    // 1. Initialize
    wm.init();

    // 2. Start WiFi
    wm.start();

    // 3. Set credentials
    wm.setCredentials("MySSID", "MyPassword");

    // 4. Connect (blocking until IP obtained)
    if (wm.connect(10000) == ESP_OK) {
        printf("Connected successfully!\n");
    }
}
```

## Documentation

For a full technical reference of all methods and states, see the [API Reference](API.md).

## Unit Testing

This component includes a comprehensive test suite. To run the tests:

1. Navigate to `test/`.
2. Build using `idf.py build`.
3. Flash and monitor.

The tests include a "warmup" phase to stabilize memory drivers and perform strict leak detection checks.

## License

MIT
