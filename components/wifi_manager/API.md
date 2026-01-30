# WiFiManager API Reference

The `WiFiManager` is a singleton class designed for robust WiFi management on ESP32 using a dedicated background task.

## Public API

### `static WiFiManager& instance()`
Returns the singleton instance of the manager.

### `esp_err_t init()`
Initializes the manager. This must be called before any other operation.
- **Returns**: `ESP_OK` on success, or an error code.
- **Actions**: Sets up NVS, Netif, Event Loop, and starts the internal `wifiTask`.

### `esp_err_t deinit()`
Cleans up all resources.
- **Returns**: `ESP_OK` on success.
- **Actions**: Stops WiFi if running, kills the task, and deletes RTOS objects.

### `esp_err_t start(uint32_t timeout_ms = 5000)`
Synchronously starts the WiFi driver in Station mode.
- **Parameters**: `timeout_ms` - Max wait time.
- **Returns**: `ESP_OK`, `ESP_ERR_TIMEOUT`, or `ESP_ERR_INVALID_STATE`.

### `esp_err_t start_async()`
Asynchronously starts the WiFi driver.
- **Returns**: `ESP_OK` if the command was queued.

### `esp_err_t stop(uint32_t timeout_ms = 5000)`
Synchronously stops the WiFi driver.
- **Returns**: `ESP_OK` or `ESP_ERR_TIMEOUT`.

### `esp_err_t stop_async()`
Asynchronously stops the WiFi driver.

### `esp_err_t connect(const std::string& ssid, const std::string& password, uint32_t timeout_ms)`
Synchronously connects to an Access Point.
- **Parameters**:
  - `ssid`: The network name.
  - `password`: The network password.
  - `timeout_ms`: Max time to wait for connection AND IP acquisition.
- **Returns**: `ESP_OK` on success.

### `esp_err_t connect_async(const std::string& ssid, const std::string& password)`
Asynchronously connects to an Access Point.

### `esp_err_t disconnect(uint32_t timeout_ms = 5000)`
Synchronously disconnects from the current network.

### `esp_err_t disconnect_async()`
Asynchronously disconnects from the current network.

### `State getState() const`
Returns the current internal state of the manager.

### `esp_err_t storeCredentials(const std::string& ssid, const std::string& password)`
Stores credentials in NVS.

### `esp_err_t loadCredentials(std::string& ssid, std::string& password)`
Loads credentials from NVS.

### `bool hasCredentials()`
Checks if credentials exist in NVS.

---

## State Enum Reference

| State | Description |
| :--- | :--- |
| `UNINITIALIZED` | Initial state. |
| `INITIALIZED` | Manager task is running, ready for commands. |
| `STARTED` | WiFi driver is active in STA mode. |
| `CONNECTED_GOT_IP` | Fully connected with a valid IP address. |
| `DISCONNECTED` | WiFi is started but not connected to an AP. |
| `...` | Various transitioning states (`STARTING`, `STOPPING`, `CONNECTING`, etc). |

---

## Design Notes

- **Thread Safety**: All public methods use an internal mutex and command queue, making the class safe to use from multiple FreeRTOS tasks.
- **Non-Blocking Task**: The actual work (calling `esp_wifi_*` functions) happens in a dedicated task (`wifiTask`) with priority 5.
- **Rollback Logic**: If a synchronous `start` or `connect` fails or times out, the manager automatically attempts a rollback to a stable state (`STOPPED` or `DISCONNECTED`).
