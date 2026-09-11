# Vendor libraries (MicroOCPP + ArduinoJson)

These directories are **not** committed as full source trees by default.

When `ocpp_client.enabled: true`, ESPHome registers them as local ESP-IDF
components (sibling paths required by MicroOCPP’s `CMakeLists.txt`).

## Fetch

```bash
./components/ocpp_client/scripts/fetch_deps.sh
```

Pins (override with env):

| Dir | Upstream | Default ref |
|-----|----------|-------------|
| `MicroOcpp/` | https://github.com/matth-x/MicroOcpp | `1.2.0` |
| `ArduinoJson/` | https://github.com/bblanchon/ArduinoJson | `6.21.5` |

Optional: convert to git submodules after first fetch if you prefer submodule
workflow (`git submodule add … vendor/MicroOcpp`).

`enabled: false` builds do **not** need this folder populated.
