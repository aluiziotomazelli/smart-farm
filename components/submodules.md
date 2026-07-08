# Components Submodules Inventory

This file tracks the versions of components currently linked as Git submodules in the `components/` directory.

| Component Name | Repository | Version (Tag/Commit) |
| :--- | :--- | :--- |
| `wifi_manager` | [wifi_manager](https://github.com/aluiziotomazelli/wifi_manager.git) | `v1.1.0` |
| `floatswitch` | [floatswitch](https://github.com/aluiziotomazelli/floatswitch.git) | `v1.0.2` |
| `power_control` | [power_control](https://github.com/aluiziotomazelli/power_control.git) | `v1.0.1` |
| `ultrasonic_sensor` | [ultrasonic_sensor](https://github.com/aluiziotomazelli/ultrasonic_sensor.git) | `v1.0.1` |
| `espnow_manager` | [espnow_manager](https://github.com/aluiziotomazelli/espnow_manager.git) | `v1.1.2` |
| `ota_manager` | [ota_manager](https://github.com/aluiziotomazelli/ota_manager.git) | `v1.0.0` |


## Maintenance Note
To update a component version, follow the read-only policy:
1. Navigate to the component directory: `cd components/<component_name>`
2. Fetch new tags: `git fetch --tags`
3. Checkout the desired tag: `git checkout <new_tag>`
4. Go back to root: `cd ../..`
5. Commit the submodule state: `git add components/<component_name> && git commit -m "update <component_name> to <new_tag>"`

## Working with Branches
Whenever you switch branches, the submodules may appear empty because Git does not automatically update the working tree of submodules. To populate them after switching branches, always run:

```bash
git submodule update --init --recursive
```
