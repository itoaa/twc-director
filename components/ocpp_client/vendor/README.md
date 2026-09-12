# Vendor libraries (MicroOCPP + ArduinoJson)

| Path | Tracked in git? | Role |
|------|-----------------|------|
| `ocpp_mocpp_bridge/` | **yes** (source) | C bridge + WSS adapter (isolates ArduinoJson from ESPHome) |
| `MicroOcpp/` | **git submodule** @ `v1.2.0` | [MicroOCPP](https://github.com/matth-x/MicroOcpp) ESP-IDF component |
| `ArduinoJson/` | **git submodule** @ `v6.21.5` | ArduinoJson v6 — **private** include for MicroOCPP only |

When `ocpp_client.enabled: true`, ESPHome registers `MicroOcpp` + `ocpp_mocpp_bridge`
as local ESP-IDF components (sibling layout required by MicroOCPP’s CMake).

## Home Assistant / Device Builder

Prefer git `external_components` on branch `feature/ocpp-1.6`. ESPHome may clone
without initializing submodules; `ocpp_client` then **auto-runs**
`scripts/fetch_deps.sh` during config validation when CMakeLists are missing.
No manual fetch is required for HA builds.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/itoaa/twc-director.git
      ref: feature/ocpp-1.6
    components: [twc_director, ocpp_client]
    refresh: 0s   # tip: force re-clone after branch updates
```

## Local / CI

```bash
git submodule update --init --recursive -- components/ocpp_client/vendor/MicroOcpp components/ocpp_client/vendor/ArduinoJson
# or (also applies CMake PRIV_INCLUDE_DIRS patch):
./components/ocpp_client/scripts/fetch_deps.sh
```

Pins (override with env on clone fallback): `MICROOCPP_REF` (default `1.2.0`),
`ARDUINOJSON_REF` (default `6.21.5`).

`fetch_deps.sh` patches MicroOCPP’s `CMakeLists.txt` so ArduinoJson is
`PRIV_INCLUDE_DIRS` (avoids breaking ESPHome’s JSON component). The patch is
applied at fetch/validate time and is **not** committed into the submodule.

`enabled: false` builds do **not** need MicroOcpp/ArduinoJson populated.
