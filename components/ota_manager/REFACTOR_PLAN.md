# OTA Manager Refactor Plan

## Summary

Refactor `components/ota_manager` into a passive, dependency-injected, host-testable OTA component following the ESP-IDF expert-agent standards.

Initial real integration target is `app_water_tank` only. That app is the best reference in the repository because it already uses NVS core, dependency injection, deep sleep, and an OTA-capable partition table. The plan below is structured so it can be implemented manually in small steps without leaving the codebase in an ambiguous state.

Official ESP-IDF references:

- OTA rollback/app states: https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/system/ota.html
- ESP HTTPS OTA advanced flow: https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/system/esp_https_ota.html

## Goals

- Keep the OTA logic isolated from business logic and from direct ESP-IDF/FreeRTOS usage outside HAL implementation files.
- Make the OTA flow testable on host before wiring it into the app.
- Support local HTTP only.
- Support version gate, hash verification, and rollback validation.
- Make the trigger model app-owned, not component-owned.

## Non-Goals

- No internet OTA.
- No HTTPS certificate management.
- No automatic OTA polling loop inside `ota_manager`.
- No migration of `app_central_hub` or `app_slave` in the first pass.
- No attempt to preserve the current singleton API as the primary design.

## Target Structure

The component should end up with this shape:

- `components/ota_manager/include/interfaces/`
- `components/ota_manager/include/`
- `components/ota_manager/src/`
- `components/ota_manager/host_test/`

Concrete responsibilities:

- `include/interfaces/`: public contracts for the OTA manager and every dependency it consumes.
- `include/`: public DTOs, config, status types, and the concrete OTA manager header.
- `src/`: implementation files and thin HAL adapters.
- `host_test/`: mocks and host tests for orchestration logic.

## Proposed Boundary

`OtaManager` should only orchestrate the flow.

It should not own raw ESP-IDF policy decisions, transport details, or app-specific validation rules.

The following logic belongs in the app, not in the component:

- when the OTA window opens
- whether the app wants to accept an OTA trigger at that moment
- app-specific diagnostics before confirming a pending image
- any app-specific NVS updates beyond the shared firmware metadata sync

The following logic belongs in `ota_manager`:

- fetch and parse the manifest
- compare versions
- start and drive the OTA session
- verify the downloaded image hash
- manage task lifecycle and in-progress state
- query pending-verify state
- confirm or reject the image at boot

## Public API And Data Model

- Define a public `IOtaManager` interface with snake_case methods.
- Expose `init()`, `deinit()`, `start_ota()`, `cancel_ota()`, `get_status()`, `check_pending_verify()`, `confirm_app_valid()`, and `rollback_and_reboot()`.
- Add an `ota_config_t` or equivalent C++ config type with fields for `device_type`, `manifest_url`, `task_stack_size`, `task_priority`, `http_timeout_ms`, `allow_same_version`, and `restart_on_success`.
- Add an `ota_manifest_t` or equivalent model with `device_type`, `version`, `firmware_url`, `firmware_size`, and `sha256_hex`.
- Add an `ota_version_t` helper with `major`, `minor`, and `patch`.
- Add an `ota_status_t` enum with states such as `idle`, `manifest_fetch`, `version_check`, `downloading`, `verifying`, `ready_to_restart`, `failed`, and `pending_verify`.
- Keep the public API free of direct ESP-IDF and FreeRTOS headers; those details belong only in HAL and implementation files.

## Naming Rules

- File names use snake_case.
- Function and method names use snake_case.
- Type names stay PascalCase where that is already the local project convention for classes and interfaces.
- Interface headers use the local `i_<name>.hpp` pattern.
- HAL files use `hal_<subsystem>.hpp` and `hal_<subsystem>.cpp`.

## Version Strategy

- Use an HTTP manifest as the update metadata source.
- Manifest includes `device_type`, `version`, `firmware_url`, `firmware_size`, and `sha256_hex`.
- Use SemVer as `major.minor.patch`.
- Use `esp_app_desc_t.version` from the running firmware as the canonical current version for downgrade prevention.
- Implement a robust SemVer parser to handle the conversion from the `esp_app_desc_t.version` string to `ota_version_t` for reliable comparison.
- Integrate `CoreStorage::fw_major`, `CoreStorage::fw_minor`, and `CoreStorage::fw_patch` as persisted/reporting metadata, not as the only source of truth.
- After a new image is confirmed valid, sync `CoreStorage` firmware fields to the validated running app version.
- Reject OTA when `new_version <= current_version`, unless a future config explicitly enables same-version testing.
- Keep `firmware_version[3]` in the ESP-NOW command format as an integration convenience, but do not treat it as the source of truth for validation.

## Component Split

Break the implementation into the following pieces:

1. `OtaManager`
   - owns orchestration
   - owns in-progress state
   - owns worker task lifecycle
   - exposes the public API

2. Manifest client
   - fetches the manifest over HTTP
   - parses it into `ota_manifest_t`
   - validates required fields and hash format

3. OTA session wrapper
   - begins the OTA session
   - exposes image descriptor reading
   - performs the download
   - finishes or aborts the session

4. Rollback/app-state wrapper
   - checks pending verify
   - confirms app validity
   - requests rollback and reboot

5. System wrapper
   - performs restart
   - fetches running app metadata
   - fetches boot partition metadata

6. Partition wrapper
   - reads SHA-256 for the selected image partition

7. FreeRTOS wrapper
   - creates the worker task
   - handles task deletion
   - manages the mutex or equivalent state protection

The wrappers should be thin. They should not add policy, retries, fallback logic, or format conversion beyond what is necessary to call the underlying API.

## OTA Flow

- `app_water_tank` wires `OtaManager` through dependency injection during app setup.
- `start_ota()` queues a request and returns immediately.
- The worker task processes one request at a time.
- Fetch the HTTP manifest and reject the request if the manifest is missing required fields, the device type does not match, or the hash is malformed.
- Compare the manifest version with the running app version before downloading.
- Reject same-version or downgrade images.
- Start the OTA session using the advanced `esp_https_ota` sequence.
- Call `esp_https_ota_get_img_desc()` before full download and verify the descriptor version against the manifest.
- Download with an `esp_https_ota_perform()` loop.
- Call `esp_https_ota_is_complete_data_received()` before finish.
- Call `esp_https_ota_finish()` only after the image is complete.
- Read the boot partition SHA-256 after finish and compare it to the manifest hash.
- Restart only after successful finish and hash verification.
- Abort and return to idle on any failure before restart.
- **Worker Task Safety**: Ensure `deinit()` or destructor handles the worker task lifecycle safely. Implement a mechanism to signal the task to stop and wait for its completion (e.g., using a task handle or notification) to prevent use-after-free if the manager is destroyed while an OTA is in flight.

Recommended failure handling:

- manifest fetch failure -> `failed`
- version gate failure -> `failed`
- OTA begin failure -> `failed`
- download failure -> `failed`
- incomplete data -> `failed`
- hash mismatch -> `failed`
- boot partition validation failure -> `failed`

## Rollback And Water Tank Integration

- Enable rollback for `app_water_tank` with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`.
- At boot, before normal deep sleep flow, `app_water_tank` calls `check_pending_verify()`.
- If pending verify is true, `app_water_tank` runs diagnostics appropriate for the app: NVS load, WiFi/ESP-NOW init, core sensor/power initialization, and any minimum sanity checks.
- If diagnostics pass, call `confirm_app_valid()` and sync `CoreStorage` firmware version fields.
- If diagnostics fail, call `rollback_and_reboot()`.
- OTA trigger handling should be app-owned: ESP-NOW command or physical input calls `start_ota()` only when the app intentionally opens an OTA window.
- The OTA manager should not silently validate the image at boot just because the app started successfully.

## app_water_tank Integration

Use `app_water_tank` as the first real integration target.

Suggested wiring:

- `WaterTankNvs` remains the owner of persisted firmware metadata.
- `WaterTankApp` decides when the OTA window opens.
- `WaterTankApp` decides when to call `confirm_app_valid()`.
- `WaterTankApp` calls `start_ota()` only from explicit app logic, not from background polling.
- `WaterTankApp` should read and sync the firmware version fields after a successful update confirmation.

Recommended boot flow:

1. Initialize NVS and shared services.
2. Check `ota_manager.check_pending_verify()`.
3. If pending verify, run a small diagnostic set.
4. If diagnostics pass, call `confirm_app_valid()`.
5. Sync stored firmware metadata.
6. Continue normal app setup.

Recommended OTA trigger flow:

1. Receive an ESP-NOW command or other external trigger.
2. Validate that the app currently allows OTA.
3. Open the OTA window.
4. Call `start_ota()`.
5. Close the window after the worker starts or on timeout.

## Migration Steps

Implement in this order:

1. Create the new public types and interfaces in the component headers.
2. Introduce the thin HAL interfaces and mocks.
3. Port the OTA orchestration into the new worker-based `OtaManager`.
4. Add manifest parsing and version comparison.
5. Add hash verification.
6. Add rollback and pending-verify handling.
7. Add host tests.
8. Wire `app_water_tank`.
9. Remove the old singleton usage from the app.

This order keeps the component buildable after each major step if the old API is temporarily preserved behind a compatibility layer.

## Tests

- Add host tests with mocks for every OTA HAL.
- Cover config validation, double init/deinit, non-blocking `start_ota()`, concurrent start rejection, manifest parsing, version comparison, descriptor mismatch, OTA begin/perform/finish errors, incomplete download, hash mismatch, successful restart path, and rollback confirmation APIs.
- Add focused tests proving NVS firmware fields are updated only after app-valid confirmation.
- Add a small test for the version comparator, because that logic is easy to get subtly wrong.
- Add a small test for manifest hash validation, including lowercase and uppercase hex handling if you choose to accept both.
- Build-check `app_water_tank` after integration:

```bash
cd app_water_tank && . $HOME/esp/esp-idf/export.sh && idf.py build
```

## Acceptance Criteria

The refactor is complete when all of the following are true:

- `ota_manager` no longer exposes a singleton as the primary API.
- The public API uses snake_case.
- The component does not call direct ESP-IDF or FreeRTOS APIs outside HAL implementation files.
- `app_water_tank` can trigger OTA explicitly and remain in control of the OTA window.
- Same-version and downgrade images are rejected.
- Hash verification runs after the OTA completes.
- Pending-verify rollback handling is implemented and testable.
- Host tests exist for the orchestration logic.
- `app_water_tank` builds successfully with the new integration.

## Assumptions

- Initial integration is `app_water_tank` only.
- `app_test`, `app_slave`, and `app_central_hub` are not migrated in this first pass.
- `CoreStorage::fw_major`, `CoreStorage::fw_minor`, and `CoreStorage::fw_patch` are useful for persisted state and ESP-NOW reporting, but `esp_app_desc_t.version` remains canonical for update validation.
- The implementation must not introduce direct ESP-IDF or FreeRTOS calls outside HAL implementation files.
