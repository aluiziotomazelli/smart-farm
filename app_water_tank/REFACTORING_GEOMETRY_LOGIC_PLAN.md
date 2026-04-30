# Implementation Plan: Geometry and Logic Precision Refactoring

## Background

The tank is not a cylinder. Its geometry is a stack of 5 cylindrical segments with
increasing diameter from base to top. Each 30 cm segment holds a different volume:

| Segment | Height Range | Volume (L) | Permille Range |
| :--- | :--- | :--- | :--- |
| 5 — Top      | 120–150 cm | 747 L | 751 → 1000 (249 ppm) |
| 4            | 90–120 cm  | 669 L | 528 → 751  (223 ppm) |
| 3            | 60–90 cm   | 597 L | 329 → 528  (199 ppm) |
| 2            | 30–60 cm   | 528 L | 153 → 329  (176 ppm) |
| 1 — Base     | 0–30 cm    | 459 L | 0   → 153  (153 ppm) |

Total capacity: **3000 L** · Pump flow: **~1400 L/h (≈ 23.3 L/min ≈ 7.8 ppm/min)**.

---

## Root Cause Analysis

There are **three separate stages** where precision is currently lost:

### Stage 1 — `calculate_permille()`: Early float truncation

```cpp
// tank_geometry.cpp (current)
uint16_t dist_cm_round = static_cast<uint16_t>(distance_cm + 0.5f); // rounds to 1 cm integer
uint16_t water_height  = dist_cm_round - offset_cm_;
return height_to_permille(static_cast<uint8_t>(water_height));       // passes uint8_t
```

The sensor returns a `float` with sub-centimetre precision. It is discarded on the **very
first line**, introducing up to **1 cm of error** before the LUT is ever consulted.

### Stage 2 — `height_to_permille()`: Integer division truncation

```cpp
// tank_geometry.cpp (current)
uint16_t height_to_permille(uint8_t height_cm) const
{
    uint8_t  segment = height_cm / 30;
    uint8_t  offset  = height_cm % 30;
    uint16_t p0 = VOLUME_LUT[segment];
    uint16_t p1 = VOLUME_LUT[segment + 1];
    return p0 - ((p0 - p1) * offset) / 30; // integer division truncates
}
```

For the base segment (`p0=153, p1=0`): the slope is `153/30 = 5.1 ppm/cm`.
Integer division yields `5 ppm/cm`, accumulating up to **~3 ppm of error** per segment
by the 29th centimetre.

### Stage 3 — `update_fill_state()`: Linear (cm) detection on a non-linear tank

```cpp
// water_tank_logic.cpp (current) — called BEFORE calculate_permille
update_fill_state(reading.cm, stats);
stats.level_permille = geometry_.calculate_permille(reading.cm);

// threshold is uniform regardless of segment
if (abs_delta < 0.5f) → STABLE
```

The 0.5 cm threshold applied uniformly across all segments is **not volumetrically
consistent**:

| Segment | ppm per cm | 0.5 cm change (ppm) |
| :--- | :--- | :--- |
| Base (Seg 1)  | 153/30 ≈ 5.1 | ~2.5 ppm |
| Top  (Seg 5)  | 249/30 ≈ 8.3 | ~4.2 ppm |

With `TIMER_FILLING_US = 30 s` and the pump running, the water rises approximately:
`1400 L/h ÷ 120 (intervals/h) ÷ 3 L/ppm ≈ 3.9 ppm per reading` at the top segment,
which corresponds to only **~0.47 cm/reading**. The 0.5 cm threshold **masks the pump
activity**, causing `STABLE` to be reported while the tank is actually `FILLING`.

---

## Proposed Changes

### 1. `TankGeometry` — Float-first precision

#### [MODIFY] [tank_geometry.hpp](file:///home/german/dev/workspaces/smart-farm/app_water_tank/main/include/tank_geometry.hpp)

- Change `height_to_permille` private signature from `uint16_t(uint8_t)` → `float(float)`.
- Keep `calculate_permille` return type as `uint16_t` (protocol compatibility), but
  derive it from the precise float result with **a single rounding at the output**.

#### [MODIFY] [tank_geometry.cpp](file:///home/german/dev/workspaces/smart-farm/app_water_tank/main/src/tank_geometry.cpp)

```cpp
// calculate_permille — keep float throughout, round only at the end
uint16_t TankGeometry::calculate_permille(float distance_cm) const
{
    float water_height_f = distance_cm - static_cast<float>(offset_cm_);

    if (water_height_f <= 0.0f) {
        return 1000;
    }
    if (water_height_f >= static_cast<float>(height_cm_)) {
        return 0;
    }

    float permille_f = height_to_permille(water_height_f);
    return static_cast<uint16_t>(permille_f + 0.5f); // single rounding point
}

// height_to_permille — full float interpolation, no integer division
float TankGeometry::height_to_permille(float height_cm) const
{
    static constexpr float SEGMENT_HEIGHT = 30.0f;

    uint8_t segment       = static_cast<uint8_t>(height_cm / SEGMENT_HEIGHT);
    float   offset_in_seg = height_cm - (segment * SEGMENT_HEIGHT);

    float p0 = static_cast<float>(VOLUME_LUT[segment]);
    float p1 = static_cast<float>(VOLUME_LUT[segment + 1]);

    return p0 - ((p0 - p1) * offset_in_seg) / SEGMENT_HEIGHT;
}
```

---

### 2. `WaterTankLogic` — Permille-based fill state detection

#### [MODIFY] [water_tank_logic.hpp](file:///home/german/dev/workspaces/smart-farm/app_water_tank/main/include/water_tank_logic.hpp)

- Change `update_fill_state` private signature from `(float distance_cm, ...)` →
  `(uint16_t current_permille, ...)`.

#### [MODIFY] [water_tank_logic.cpp](file:///home/german/dev/workspaces/smart-farm/app_water_tank/main/src/water_tank_logic.cpp)

- In `process_reading`: compute `level_permille` **before** calling `update_fill_state`
  and pass the permille value instead of the raw distance.
- In `update_fill_state`: compare `int32_t` delta in permille against `LEVEL_DELTA_MIN`.

```cpp
void WaterTankLogic::process_reading(const ultrasonic::Reading& reading, WaterTankStats& stats)
{
    stats.last_result = reading.result;
    stats.measure_count++;
    update_results_counters(reading.result, stats);

    if (ultrasonic::is_success(reading.result)) {
        stats.last_distance_cm = reading.cm;
        stats.level_permille   = geometry_.calculate_permille(reading.cm); // first
        update_fill_state(stats.level_permille, stats);                     // then
    }
}

void WaterTankLogic::update_fill_state(uint16_t current_permille, WaterTankStats& stats) const
{
    if (stats.last_level_permille == 0) {     // first reading — no reference yet
        stats.fill_state = FillState::STABLE;
        return;
    }

    int32_t delta = static_cast<int32_t>(current_permille)
                  - static_cast<int32_t>(stats.last_level_permille);

    if (std::abs(delta) < static_cast<int32_t>(LEVEL_DELTA_MIN)) {
        stats.fill_state = FillState::STABLE;
    } else if (delta > 0) {
        stats.fill_state = FillState::FILLING;
    } else {
        stats.fill_state = FillState::DRAINING;
    }
}
```

> [!IMPORTANT]
> `WaterTankStats` must gain a `last_level_permille` (uint16_t) field used exclusively
> by `update_fill_state` as the previous-reading reference. The existing `last_distance_cm`
> field is kept for telemetry/logging only.

---

### 3. `water_tank_types.hpp` — Constant adjustments

#### [MODIFY] [water_tank_types.hpp](file:///home/german/dev/workspaces/smart-farm/app_water_tank/main/include/water_tank_types.hpp)

| Constant | Current | Proposed | Rationale |
| :--- | :--- | :--- | :--- |
| `LEVEL_DELTA_MIN` | `10` ppm | **`5` ppm** | Pump moves ~3.9 ppm/30 s. A threshold of 5 ppm detects filling reliably while rejecting sensor noise (~1–2 ppm). |
| `TIMER_FILLING_US` | `30 s` | **`60 s`** | At 30 s the pump delta (~3.9 ppm) is too close to threshold. At 60 s it yields ~7.8 ppm/reading — clearly above 5 ppm. Also reduces deep-sleep wake/boot overhead by 50%. |
| `TIMER_STABLE_US` | `5 min` | Keep | Adequate for monitoring a stable tank. |
| `TIMER_DRAIN_US`  | `2 min` | Keep | Gravity drain is slower than pump; 2 min resolution is sufficient. |
| `TIMER_UNKNOWN_US`| `60 s`  | Keep | Fast retry on sensor failure is appropriate. |
| `BACKUP_MODE_SLEEP_US` | `15 s` | Keep | Aggressive retry in backup mode is intentional. |

---

## Open Questions

> [!IMPORTANT]
> **`WaterTankStats.last_distance_cm` after refactoring:** Once fill-state detection
> moves to permille space, `last_distance_cm` is only used for telemetry logging. Should
> it be kept (useful for debugging sensor health) or removed to keep the struct lean?

> [!NOTE]
> **`TIMER_FILLING_US = 60 s` vs 30 s:** If faster telemetry updates during filling
> are preferred, keeping 30 s is valid — but the threshold must then be lowered to
> **`3 ppm`** instead of 5 ppm to reliably detect the pump at ~3.9 ppm/reading. The
> trade-off is slightly higher sensitivity to sensor noise.

---

## Impact Analysis

- **Accuracy**: A single `float` path with one rounding at the output eliminates the
  two-stage truncation. Permille error per reading drops from ~3–4 ppm to < 0.5 ppm.
- **Fill-state correctness**: Detection in permille space is volumetrically consistent
  across all five segments — 5 ppm means the same volume change regardless of segment
  diameter.
- **Protocol compatibility**: `calculate_permille` still returns `uint16_t`; no changes
  to the communication layer are required.
- **Deep-sleep overhead**: Doubling `TIMER_FILLING_US` to 60 s reduces wake cycles by
  50% during filling, saving power and reducing ESP32 boot overhead.

---

## Verification Plan

### Unit Tests (`host_test`)
- Verify `TankGeometry::calculate_permille` at segment boundaries (e.g., 29 cm, 30 cm,
  31 cm) and at float sub-centimetre values — confirm no 1-ppm "staircase" jumps.
- Verify `WaterTankLogic::update_fill_state` with synthetic permille sequences:
  - Delta of 4 ppm → `STABLE`
  - Delta of +6 ppm → `FILLING`
  - Delta of -6 ppm → `DRAINING`
  - First reading (no reference) → `STABLE`

### Integration
- Flash `app_water_tank`, enable verbose logging, and monitor `fill_state` transitions
  during a real pump cycle to confirm `FILLING` is reported consistently.
