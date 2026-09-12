# OCPP 1.6J PoC (native on ESP)

**Branch:** `feature/ocpp-1.6`  
**Status:** lab experiment — default `tesla-director.yaml` has no OCPP block; `tesla-director-ocpp.yaml` is `enabled: true` and CI-verified (git submodules + fetch_deps + compile). Live CSMS still needs real secrets + network.

## Model

| Layer | Role |
|-------|------|
| OCPP 1.6J (WSS/TLS) | One Charge Point identity; connectors 1..N ↔ TWC slots |
| Director (`twc_director`) | Local Tesla Gen2 RS-485 load sharing (up to 4 TWC) |
| Hard caps | `global_max_current` / per-TWC max / contactor gates — **last line of defense** |

- **Global** Smart Charging / `ChargePointMaxCurrent` → `apply_external_global_max_a()` (hard cap ceiling).
- **Per-connector** Smart Charging / `TwcConnectorNMaxCurrent` → slot N-1 session/max wish, still ≤ hard cap + per-EVSE limit.
- Local load sharing among Wall Connectors stays in `twc_director`.

## Non-negotiable (CISO)

- Transport: **wss** only (no cleartext `ws`) — YAML + runtime HA URL reject `ws://`
- Unique Charge Point credentials; secrets **never** in git (`!secret` / NVS overrides)
- OCPP **default OFF** (`enabled: false` or absent); HA enable switch can start/stop without rebuild when firmware linked
- Fail-safe if CSMS is down: `fail_safe_amps` (≤ hard cap), not full open
- Lab remotes (**RemoteStart/Stop, Reset, Unlock**) are **feature-flagged DEFAULT OFF**
- **UpdateFirmware / remote FW: always rejected** until signed image + rollback exist
- Enabling remotes against a **public cloud CSMS** requires a **new explicit risk accept** (not covered by lab LAN/VPN accept)

## Runtime HA surface (Max #3)

| Entity | Purpose |
|--------|---------|
| `enable_switch` | Start/stop CSMS client without reflash; OFF → fail-safe + clean mocpp stop |
| `csms_url_text` / `charge_point_id_text` / `authorization_key_text` | Runtime overrides (wss only; key shown as `********`) |
| `connected` / `connection_state` / `last_applied_amps` | CSMS observability |
| `fail_safe_button` («återställ till fail-safe») | Calls `apply_fail_safe_` |
| `allow_remote_*` switches | Lab gates for RemoteStart/Stop/Reset/Unlock (DEFAULT OFF) |

YAML `!secret` values seed the device; HA edits persist in NVS (not git).

## MeterValues + StatusNotification (Max #6)

Fed from `twc_director` site/slot telemetry (not empty MicroOCPP defaults):

| OCPP input | TWC source (best-effort) |
|------------|---------------------------|
| ConnectorPlugged / Occupied | `vehicle_connected` / vehicle \|\| charging |
| EvReady / EvseReady | charging/contactor / online |
| Energy (Wh) / Power (W) | total energy kWh×1000 / V×I approx |
| Current.Import (A), Voltage (V) | max phase current / first phase V |
| StatusNotification | Available / Preparing / Charging / Finishing via MO inputs |

**Mapping limits:** Gen2 proprietary RS-485 state → OCPP ChargePointStatus is **approximate**. No J1772 State C wire; “charging” inferred from session amps / contactor.

## Lab remotes (Max #9) — flags DEFAULT OFF

| Action | Flag off | Flag on (lab LAN/VPN CSMS only) |
|--------|----------|----------------------------------|
| RemoteStart | Undo tx + AUDIT reject | AUDIT accept; OCPP tx may run |
| RemoteStop | AUDIT; **no** amp fail-safe | AUDIT + **fail-safe amps** |
| Reset | `setOnResetNotify` → false | Fail-safe then allow reset notify |
| Unlock | AUDIT reject (no Gen2 actuator) | AUDIT; still no physical unlock |
| UpdateFirmware | **Always rejected** | — |

Every remote receive/accept/reject is **AUDIT**-logged.

## Per-connector amp (Max #10) / OCPP 2.0.1

- **1.6J per-connector:** `MO_NUMCONNECTORS=5` (CP + 4 slots). Connector `N` ↔ EVSE slot `N-1`. Global hard cap remains ceiling.
- **OCPP 2.0.1:** **not enabled** (`MO_ENABLE_V201=0`). Partial/flag-off until a separate ask; ship 1.6J per-connector first.

## Out of scope (enforced)

- Remote firmware / signed OTA path (stub reject)
- OCPP 2.0.1 protocol selection
- Cleartext `ws://`
- Raising amps above YAML/compile hard caps
- Committing secrets

## Component

| Piece | Path |
|-------|------|
| ESPHome component | [`components/ocpp_client/`](../components/ocpp_client/) |
| Example fragment | [`examples/ocpp-fragment.yaml`](../examples/ocpp-fragment.yaml) |
| Optional full config | [`tesla-director-ocpp.yaml`](../tesla-director-ocpp.yaml) |
| Secrets placeholders | [`secrets.yaml.example`](../secrets.yaml.example) |

### Director API used

```cpp
float hard_cap_global_max_a() const;
float apply_external_global_max_a(float amps);
float apply_external_connector_max_a(unsigned connector_id, float amps);
SiteTelemetry get_site_telemetry() const;
SlotTelemetry get_slot_telemetry(size_t slot_index) const;
```

## Library / vendoring

MicroOCPP **v1.2.0** and ArduinoJson **v6.21.5** are **git submodules** under
`components/ocpp_client/vendor/`. Runtime glue: committed `ocpp_mocpp_bridge` C API.
WSS: `espressif/esp_websocket_client`.

### Home Assistant Device Builder

No manual `fetch_deps.sh` needed. Point `external_components` at this branch:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/itoaa/twc-director.git
      ref: feature/ocpp-1.6
    components: [twc_director, ocpp_client]
    refresh: 0s   # tip: force re-clone after pushes to the branch
```

If ESPHome’s git clone did not initialize submodules, `ocpp_client` detects missing
`vendor/*/CMakeLists.txt` when `enabled: true` and **automatically runs**
`scripts/fetch_deps.sh` (clear log warning), then patches MicroOCPP so ArduinoJson
is `PRIV_INCLUDE_DIRS`.

### Local / CI

```bash
git submodule update --init --recursive
# or (submodule OR clone + CMake patch):
./components/ocpp_client/scripts/fetch_deps.sh
```

Pins (clone fallback env): `MICROOCPP_REF` (default `1.2.0`), `ARDUINOJSON_REF` (default `6.21.5`).

`enabled: false` / omitted block builds do **not** need or link MicroOCPP. CI checks out
with `submodules: recursive` and still runs `fetch_deps` as safety (CMake patch).

## Hardware / flash·RAM

| Board | Role |
|-------|------|
| ESP32-WROOM (`esp32dev`) | Compile / early lab — watch flash/RAM |
| **ESP32-S3 (8 MB+ flash)** | Preferred for ESPHome + TLS + MicroOCPP + UART |

### Measured sizes (esp32dev / WROOM, `tesla-director-ocpp.yaml`, ESPHome 2026.7.3)

Local compile 2026-09-11 (also re-checked in CI «Firmware size» step):

| Artifact | Value | Notes |
|----------|-------|-------|
| App image / `.bin` | **1 205 776 bytes** (~1.15 MiB) | `tesla-director-ocpp.bin` / OTA |
| Flash (idf size) | **1 205 659 / 1 835 008** (**65.7%**) | app partition usable |
| DRAM | **52 488 / 180 736** (**29.0%**) | `.data`+`.bss` |
| IRAM | **77 575 / 131 072** (**59.2%**) | watch if adding features |
| Factory `.bin` | 1 271 312 bytes | includes bootloader padding |

**Judgement:** WROOM still compiles with DRAM headroom; IRAM ~59% and flash ~66% mean **S3 (8 MB+) remains preferred** before stacking more lab features. Re-check CI logs after each OCPP change.

## Lab checklist (live CSMS)

1. `cp secrets.yaml.example secrets.yaml` — `wss://…`, CP id, auth key
2. `git submodule update --init --recursive` (or rely on HA auto-fetch_deps)
3. Flash `tesla-director-ocpp.yaml` (keep `tesla-director.yaml` clean)
4. Prefer **lab CSMS on LAN/VPN**; leave remote-* switches OFF unless risk-accepted
5. Confirm BootNotification / Heartbeat; MeterValues move with TWC telemetry
6. From CSMS, SetChargingProfile — global/connector max never above hard cap
7. Disconnect CSMS — fail-safe amps; or press «återställ till fail-safe»


## Connection log sequence (Enable ON)

When Ola flips **OCPP Enable** ON (or cold boot with enable already on), look for this order in the
device log (`ocpp_client` / `ocpp_bridge` / `ocpp_ws`). Auth key and URL userinfo are **never** logged.

1. `OCPP enable path start (HA enable ON|cold setup|…)` — `url_len`, `cp_id_len`, `auth_key_set`
2. `enable/resolve-input:` / `resolved-wss:` — redacted `host` / `port` / `path`
3. HA `connection_state` → **`connecting`** (not stuck on `starting`; init is deferred to `loop`)
4. `Calling twc_ocpp_mocpp_start` → bridge `before g_ws->begin` → `ws-begin-enter` / `esp_websocket_client_start`
5. `after g_ws->begin: OK` → `before mocpp_initialize` → `after mocpp_initialize`
6. `MicroOCPP bridge initialized` / `MicroOCPP started`
7. Either `WEBSOCKET_EVENT_CONNECTED` + `CSMS WebSocket connected` (`connection_state=connected`),
   or after **45s** `error:connect-timeout` + fail-safe + stop (toggle Enable OFF/ON to retry)

If steps 4–6 never appear while UI sat on `starting`, you were on a build before deferred init;
reflash this branch tip.

## Related

- Safety notes: [SECURITY.md](SECURITY.md)
- Production residual risk is often lower with a **gateway** until OTA + cert lifecycle on-device is proven.
