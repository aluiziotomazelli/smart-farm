# Implementation Plan: Geometry and Logic Precision Refactoring

## Objective
Refactor the tank geometry calculation to eliminate the 1cm "staircase" effect and update the water tank logic to use volume variation (permille) for more accurate fill state detection.

## Proposed Changes

### 1. TankGeometry (`app_water_tank/main/src/tank_geometry.cpp` & `.hpp`)
*   **Refactor `height_to_permille`**: Change signature from `uint16_t height_to_permille(uint8_t height_cm)` to `float height_to_permille(float height_cm)`.
*   **Update Interpolation**: Implement continuous linear interpolation using `float` variables to avoid rounding errors during the lookup process.
*   **Update `calculate_permille`**: Modify implementation to use `float` for water height calculation and internal interpolation. The return type will remain `uint16_t` for protocol compatibility, but it will be derived from a more precise float calculation.

### 2. WaterTankLogic (`app_water_tank/main/src/water_tank_logic.cpp` & `.hpp`)
*   **Update `update_fill_state`**:
    *   Change signature to `void update_fill_state(uint16_t current_permille, WaterTankStats& stats)`.
    *   Implement threshold-based detection. Since the pump provides ~8 permille/min, a threshold of ~2-3 permille (approx. 0.3-0.5cm) will be used to distinguish between `STABLE`, `FILLING`, and `DRAINING`, effectively filtering noise while remaining sensitive to pump flow.
*   **Refactor `process_reading`**:
    *   Rearrange the flow so `level_permille` is calculated *before* calling `update_fill_state`.
    *   Pass the newly calculated `permille` to the logic update.

### 3. Documentation & Clean-up
*   Update comments regarding the use of permille for fill state detection.
*   Remove obsolete distance-based threshold constants.

## Impact Analysis
*   **Accuracy**: Fill state detection will be consistent across all tank segments regardless of diameter changes.
*   **Robustness**: Using permille (volume) instead of distance makes the logic invariant to the specific geometry of the tank.
*   **Compatibility**: No changes to the communication protocol or external interfaces are required.

## Verification Plan
1.  **Unit Tests**: Verify `TankGeometry` with various `distance_cm` values, ensuring smooth transitions between segments without 1cm jumps.
2.  **Logic Simulation**: Verify `WaterTankLogic` correctly identifies `FILLING` state when a delta > threshold is observed.
3.  **Integration**: Run the updated `app_water_tank` and monitor the `fill_state` in logs during simulated filling/draining.
