# OCPP 1.6J PoC (native on ESP)

**Branch:** `feature/ocpp-1.6`  
**Status:** scaffolding / lab experiment — not production-ready.

## Model

| Layer | Role |
|-------|------|
| OCPP 1.6J (WSS/TLS) | One Charge Point identity for the whole site |
| Director (`twc_director`) | Local Tesla Gen2 RS-485 load sharing (up to 4 TWC) |
| Hard caps | `global_max_current` / per-TWC max / contactor gates — **last line of defense** |

OCPP only steers **global max amp**. Profiles may lower or redistribute **within** hard caps; they must never raise above breaker/cable limits. Load sharing among Wall Connectors stays local.

Optional view: ConnectorId `1` aggregates the bus. Multiple connectors for telemetry are out of scope for v1 amp control.

## Non-negotiable (CISO)

- Transport: **wss** only (no cleartext `ws` to the internet)
- Unique Charge Point credentials; secrets **never** in git
- OCPP **default OFF** (YAML / compile flag)
- Fail-safe if CSMS is down: safe local default, not full open load
- v1 allowlist: BootNotification, Heartbeat, StatusNotification, MeterValues, minimal Authorize/Start/Stop if needed, **SetChargingProfile / ChangeConfiguration → global max amp**
- Explicitly **out** of v1: RemoteStart/Stop, Reset, Unlock, firmware update, arbitrary DataTransfer

## Hardware

| Board | Role |
|-------|------|
| ESP32-WROOM (`esp32dev`) | Early compile spike only |
| **ESP32-S3 (8 MB+ flash)** | Target for a livable build (RAM/flash headroom for ESPHome + TLS + MicroOCPP + UART) |

## Library

Intended client: [MicroOCPP](https://github.com/matth-x/MicroOCPP) (OCPP 1.6J). Integration path under ESPHome is TBD in this PoC (custom component / IDF component / vendored tree) — see `components/ocpp_client/`.

## Related

- Production residual risk is lower with a **gateway** (HA add-on / host) until OTA + cert lifecycle on-device is proven. This branch explores **native** anyway for lab learning.
- Existing safety notes: [SECURITY.md](SECURITY.md)
