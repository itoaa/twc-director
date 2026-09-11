# OCPP 1.6J PoC (native on ESP)

**Branch:** `feature/ocpp-1.6`  
**Status:** lab experiment — default `tesla-director.yaml` has no OCPP block; `tesla-director-ocpp.yaml` is `enabled: true` and CI-verified (fetch_deps + compile). Live CSMS still needs real secrets + network.

## Model

| Layer | Role |
|-------|------|
| OCPP 1.6J (WSS/TLS) | One Charge Point identity for the whole site |
| Director (`twc_director`) | Local Tesla Gen2 RS-485 load sharing (up to 4 TWC) |
| Hard caps | `global_max_current` / per-TWC max / contactor gates — **last line of defense** |

OCPP only steers **global max amp** via `TWCDirectorComponent::apply_external_global_max_a()`.
Profiles may lower within hard caps; they must never raise above breaker/cable limits.
Load sharing among Wall Connectors stays local.

## Non-negotiable (CISO)

- Transport: **wss** only (no cleartext `ws`)
- Unique Charge Point credentials; secrets **never** in git
- OCPP **default OFF** (`enabled: false` or absent from YAML)
- Fail-safe if CSMS is down: `fail_safe_amps` (≤ hard cap), not full open
- v1 allowlist: BootNotification, Heartbeat, StatusNotification, MeterValues,
  **SetChargingProfile / ChangeConfiguration → global max amp**
- Explicitly **out** of v1: RemoteStart/Stop, Reset, Unlock, firmware update, arbitrary DataTransfer

## Component

| Piece | Path |
|-------|------|
| ESPHome component | [`components/ocpp_client/`](../components/ocpp_client/) |
| Example fragment | [`examples/ocpp-fragment.yaml`](../examples/ocpp-fragment.yaml) |
| Optional full config | [`tesla-director-ocpp.yaml`](../tesla-director-ocpp.yaml) (`enabled: true`; CI runs `fetch_deps.sh` then compiles) |
| Secrets placeholders | [`secrets.yaml.example`](../secrets.yaml.example) |

### Director API used

```cpp
float hard_cap_global_max_a() const;
float apply_external_global_max_a(float amps);  // clamps to hard cap
```

## Library / vendoring

Client: [MicroOCPP](https://github.com/matth-x/MicroOcpp) (OCPP 1.6J) + [ArduinoJson](https://github.com/bblanchon/ArduinoJson) v6.

```bash
./components/ocpp_client/scripts/fetch_deps.sh
```

MicroOCPP + ArduinoJson live under `components/ocpp_client/vendor/` (fetch_deps.sh); runtime glue is the committed `ocpp_mocpp_bridge` C API (keeps ArduinoJson off ESPHome’s include path)
(sibling layout required by MicroOCPP’s CMake). WSS uses managed component
`espressif/esp_websocket_client`.

`enabled: false` / omitted block builds do **not** fetch or link MicroOCPP. CI’s OCPP job fetches deps and compiles `enabled: true`.

## Hardware

| Board | Role |
|-------|------|
| ESP32-WROOM (`esp32dev`) | Compile / early lab |
| **ESP32-S3 (8 MB+ flash)** | Preferred for ESPHome + TLS + MicroOCPP + UART |

## Lab checklist (live CSMS)

1. `cp secrets.yaml.example secrets.yaml` and set `ocpp_csms_url` (`wss://…`), CP id, auth key
2. Run `fetch_deps.sh`
3. Use `tesla-director-ocpp.yaml` (`enabled: true`) or set the flag in a non-default YAML (keep `tesla-director.yaml` clean)
4. Flash, watch logs for BootNotification / Heartbeat
5. From CSMS, send SetChargingProfile — confirm HA/global max moves but never above hard cap
6. Disconnect CSMS — confirm fail-safe amps applied

## Related

- Safety notes: [SECURITY.md](SECURITY.md)
- Production residual risk is often lower with a **gateway** until OTA + cert lifecycle on-device is proven.
