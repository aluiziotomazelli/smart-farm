# Component Management Strategy: Managed vs. Local Submodules

## 1. Introduction
The Smart Farm project consists of multiple applications (`app_water_tank`, `app_central_hub`, etc.) that share a set of core components and drivers. Managing these dependencies efficiently is crucial for build stability, code navigation, and architectural clarity.

## 2. Current Scenario: ESP-IDF Component Manager
Currently, the project uses the ESP-IDF Component Manager (`idf_component.yml`) to pull external drivers and managers.

### 2.1 Characteristics
- Dependencies are declared in `app_water_tank/main/idf_component.yml`.
- Components are downloaded into the `managed_components/` directory during the build process.
- Each application manages its own set of dependencies.

### 2.2 Pain Points
- **Opaque Codebase**: Components in `managed_components/` are often excluded from searches or navigation, making debugging and understanding the system harder.
- **Namespace and Include Collisions**: Multiple components adding generic paths like `include/interfaces` to the global include path cause collisions (e.g., `ITimerHAL` conflicting between components).
- **Type Casting Overhead**: Because components are treated as "external black boxes," internal types from `components/common` (like `FarmNodeId`) often require manual casting to generic types used by the components (like `uint8_t` or component-specific `NodeId`).
- **Isolation Overhead**: While isolation is good for distinct products, it creates friction in a co-dependent ecosystem like Smart Farm, where an update to the protocol should ideally be reflected easily across all parts.

## 3. Proposed Scenario: Local Git Submodules
The proposed change involves moving all project-specific external components into the root `components/` directory using **Git Submodules**.

### 3.1 Characteristics
- External repositories are linked as submodules in `components/<name>`.
- Components are treated as local members of the ESP-IDF project.
- The `managed_components/` directory and `idf_component.yml` files are removed.

### 3.2 Benefits
- **Full Workspace Visibility**: All source code is present in the tree, allowing for global searches, easy navigation, and better IDE support.
- **Namespace Clarity**: By having components locally, it is easier to apply namespaces (e.g., `espnow::`) and unique include paths, resolving collision issues.
- **Unified Integration**: Components can more naturally integrate with `components/common`, reducing the need for explicit type casting in `main.cpp`.
- **Read-Only Integrity**: By using submodules pointing to specific tags/commits, we maintain the integrity of the original repositories. Development still happens in the component's own repo, and updates are pulled in as controlled version bumps.

## 4. Trade-off Analysis

| Feature | Managed Components (Current) | Local Submodules (Proposed) |
| :--- | :--- | :--- |
| **Automation** | High (automatic download) | Low (manual submodule init/update) |
| **Code Navigation** | Poor (shadowed code) | Excellent (native source tree) |
| **Namespace Safety** | Low (path collisions) | High (easier to wrap/namespace) |
| **Versioning** | Automatic (semantic versioning) | Manual (commit/tag specific) |
| **Developer Workflow** | Easy for consumers | Better for architect/core devs |
| **Dependency sharing** | Isolated per app | Centralized for the whole project |

## 5. Migration Plan

### Phase 1: Preparation
1. Identify all current dependencies in `idf_component.yml`.
2. Ensure all external components have the desired versions tagged in their respective repositories.

### Phase 2: Cleanup
1. Delete `managed_components/` directories.
2. Delete `idf_component.yml` files from all `app_*/main/` directories.
3. Clean the build environment: `idf.py fullclean`.

### Phase 3: Submodule Addition
1. Add submodules to the root `components/` folder:
   ```bash
   git submodule add https://github.com/aluiziotomazelli/wifi_manager.git components/wifi_manager
   git submodule add https://github.com/aluiziotomazelli/floatswitch.git components/floatswitch
   git submodule add https://github.com/aluiziotomazelli/power_control.git components/power_control
   git submodule add https://github.com/aluiziotomazelli/ultrasonic_sensor.git components/ultrasonic_sensor
   git submodule add https://github.com/aluiziotomazelli/espnow_manager.git components/espnow_manager
   ```
2. Pin to specific versions:
   ```bash
   cd components/espnow_manager && git checkout v1.1.2 && cd ../..
   # Repeat for other components...
   ```

### Phase 4: Build System Update
1. Update `app_*/main/CMakeLists.txt` to include the local components in the `REQUIRES` list.
2. Verify that the build system correctly identifies the components in the `components/` directory.

### Phase 5: Verification
1. Run a full build of the primary application (`app_water_tank`).
2. Verify communication and sensor logic on the target hardware.
