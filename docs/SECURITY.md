# Security notes — TWC Director

This is a community ESPHome stack for Tesla Gen2 Wall Connector load-sharing.
It is **not** a certified EVSE safety controller. Electrical safety depends on
correct installation (breaker, cable, hardware limits) plus the policies below.

## What we harden in software

| Control | Behaviour |
|---------|-----------|
| `global_max_current` | **Required** and must be `> 0`. Total budget for all EVSEs. |
| Runtime global max number | Updates C core reconciliation (not only the UI entity). |
| Per-EVSE max clamp | Uses `global_twc_max_current`, not a hard-coded 80 A. |
| Contactor switch | Requires **master mode** and EVSE **enabled**; no optimistic UI. |
| Contactor UI state | Follows bus-inferred current (`session > 0.1 A`), not the last click. |
| Master OFF | Fail-safe: set session 0 A, best-effort open contactors, then stop TX. |
| Boot | Does **not** auto-enable master mode. |
| Web UI | Prefer **no** `web_server` in production (`tesla-director-safe.yaml`). |

## Recommended deployment

1. Use **`tesla-director-safe.yaml`** (or copy its choices into your config).
2. Set `global_max_current` to **breaker rating minus margin**, never higher.
3. Set `global_twc_max_current` / per-slot max to **cable + TWC rating**.
4. Control only via **Home Assistant API** with `api.encryption`.
5. Protect **OTA** with a unique password.
6. Put the ESP on a **trusted IoT LAN/VLAN**; do not expose port 80 to the internet.
7. Enable **Master Mode** only when you intend to control the bus.

## Threat model (honest)

| Threat | Mitigation |
|--------|------------|
| Anyone on LAN uses web UI | Omit `web_server` / use reverse proxy HTTPS |
| Wrong global max in HA | Compile-time max clamps runtime number; core updated |
| Contactor click without master | Rejected + UI restored |
| Master left on after maintenance | Operator responsibility; default boot is master off |
| RS-485 cable cut mid-charge | TWC may keep last limit; director cannot force 0 A offline |
| Flash-write protocol cmds | Blocked for known dangerous 0x19/0x1A + 0xFC |

## Profiles

- **Lab / debug:** `tesla-director.yaml` — web server allowed (no HTTP password auth).
- **Production:** `tesla-director-safe.yaml` — no web server, conservative currents.

## Reporting

Prefer private disclosure for security issues that could cause unsafe charge
limits or remote contactor abuse on shared networks.

## OCPP experiment (`feature/ocpp-1.6`)

Native OCPP on the ESP moves TLS, WebSocket, and CSMS trust onto the same MCU
that owns RS-485. Treat CSMS as remote admin.

| Control | Requirement |
|---------|-------------|
| Transport | `wss://` only (schema + runtime reject `ws://`) |
| Amp profiles | Wish from CSMS via `apply_external_global_max_a()`; **hard caps win** |
| CSMS down | `fail_safe_amps` (≤ hard cap), never full open |
| Feature flag | Default **off** (`enabled: false` / omit block) |
| Secrets | `ocpp_*` in `secrets.yaml` only; rotate; physical theft of ESP = credential risk |
| v1 scope | Global max amp + Boot/Heartbeat/Status/MeterValues; **no** RemoteStart/Stop, Reset, Unlock, FW update |

See [OCPP.md](OCPP.md) and [`components/ocpp_client/README.md`](../components/ocpp_client/README.md).
Lab PoC only until OTA signing and cert lifecycle are proven.
