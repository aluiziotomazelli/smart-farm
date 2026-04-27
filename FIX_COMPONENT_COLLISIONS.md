# Fix: Component Header Name Collisions

## The Problem
Although the components use C++ namespaces, the **filenames** of the interface headers are identical across different components:
- `floatswitch/include/interfaces/i_hal_timer.hpp`
- `espnow_manager/include/interfaces/i_hal_timer.hpp`

When the ESP-IDF build system adds these components, it flattens the include search path. When a file does `#include "i_hal_timer.hpp"`, the compiler picks the first one it finds. If `espnow_manager` picks up the `floatswitch` header, it won't find the `espnow::ITimerHAL` class, causing a build failure.

## The Solution
You must rename the header files in their **original repositories** (not in `managed_components`) to ensure they are unique across the entire project.

### Recommended Changes for `floatswitch` Repository:
1. **Rename the file:**
   - From: `include/interfaces/i_hal_timer.hpp`
   - To: `include/interfaces/i_float_switch_hal_timer.hpp`
2. **Update all includes** within the `floatswitch` repository to point to the new filename.
3. **Commit and Tag** a new version (e.g., `v1.0.2`).

### Recommended Changes for `espnow_manager` Repository:
1. **Rename the file:**
   - From: `include/interfaces/i_hal_timer.hpp`
   - To: `include/interfaces/i_espnow_hal_timer.hpp`
2. **Update all includes** within the `espnow_manager` repository.
3. **Update `water_tank_app.cpp`** in the main project to use the full namespace `espnow::IEspNowManager` in the constructor.
4. **Commit and Tag** a new version (e.g., `v1.1.1`).

## Final Step
After updating the remote repositories, update your `idf_component.yml` versions in the main project and run:
```bash
idf.py fullclean
rm -rf managed_components/
idf.py build
```
