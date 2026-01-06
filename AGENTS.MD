# Agent Guidelines for This Project

Hello Agent! Welcome to the project. Please follow these guidelines to ensure your contributions are consistent with the existing codebase.

## 1. General Philosophy & Architecture

-   **C++ and Object-Oriented:** The project is written in C++ on top of the ESP-IDF framework. New functionality should be encapsulated within classes.
-   **Non-Blocking Operations:** Components that perform long-running or asynchronous operations (like communication or complex sensor readings) **MUST** use a dedicated FreeRTOS task. The public API should be non-blocking, typically posting messages to a private queue for the task to process. Avoid blocking `app_main` or other critical tasks.
-   **Clean `main`:** The `app_main` function should be kept as simple as possible. Its primary role is to initialize the main application class and start its primary execution loop or task.

## 2. Code Style and Conventions

-   **Naming Conventions:**
    -   **Functions/Methods:** `camelCase()`
    -   **Variables:** `snake_case`
    -   **Private Class Members:** `snake_case` suffixed with an underscore (e.g., `task_handle_`, `config_`).
    -   **Filenames:** `snake_case` (e.g., `water_tank_nvs.cpp`).

-   **Comments and Documentation:**
    -   All comments, documentation, and commit messages **MUST** be in **English**.
    -   Header files (`.hpp`) should use Doxygen-style comments for public APIs.
    -   Implementation files (`.cpp`) can use simpler `//` comments for clarification.

-   **Logging:**
    -   Use the ESP-IDF logging framework (`ESP_LOGI`, `ESP_LOGE`, etc.).
    -   Define a static `TAG` constant at the beginning of each `.cpp` file for consistent log messages.

## 3. Error Handling

-   Functions that can fail (e.g., hardware interaction, memory allocation) **MUST** return an `esp_err_t`.
-   Callers **MUST** check the return value of these functions against `ESP_OK` and handle any potential failures gracefully. Do not ignore error codes.

## 4. Project Structure

-   **Applications (`apps/`):** The main application logic resides here (e.g., `apps/water_tank`). Each app is a self-contained firmware target.
-   **Components (`components/`):** Reusable modules (drivers, utilities, etc.) are placed here. Components should be generic and decoupled from specific application logic.

## 5. Build System

-   The project uses CMake and the ESP-IDF build system.
-   When adding new files, they must be registered in the appropriate `CMakeLists.txt` file using `idf_component_register`.
