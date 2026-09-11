# Vendor libraries (MicroOCPP + ArduinoJson)

| Path | Tracked in git? | Role |
|------|-----------------|------|
| `ocpp_mocpp_bridge/` | **yes** | C bridge + WSS adapter (isolates ArduinoJson from ESPHome) |
| `MicroOcpp/` | no (fetch) | [MicroOCPP](https://github.com/matth-x/MicroOcpp) ESP-IDF component |
| `ArduinoJson/` | no (fetch) | ArduinoJson v6 — **private** include for MicroOCPP only |

When `ocpp_client.enabled: true`, ESPHome registers `MicroOcpp` + `ocpp_mocpp_bridge`
as local ESP-IDF components (sibling layout required by MicroOCPP’s CMake).

## Fetch (lab / enabled:true)

```bash
./components/ocpp_client/scripts/fetch_deps.sh
```

Pins (override with env): `MICROOCPP_REF` (default `1.2.0`), `ARDUINOJSON_REF` (default `6.21.5`).

The fetch script patches MicroOCPP’s `CMakeLists.txt` so ArduinoJson is
`PRIV_INCLUDE_DIRS` (avoids breaking ESPHome’s JSON component).

`enabled: false` builds do **not** need MicroOcpp/ArduinoJson populated.
