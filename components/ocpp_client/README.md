# ocpp_client (ESPHome external component)

Native **OCPP 1.6J** Charge Point client for the TWC Director. Embeds
[MicroOCPP](https://github.com/matth-x/MicroOcpp) and maps Smart Charging onto
`twc_director` **global + per-connector** amp wishes (hard caps win).

## Safety (CISO)

| Rule | Behaviour |
|------|-----------|
| Transport | `csms_url` must be `wss://` (YAML + HA text reject `ws://`) |
| Secrets | `!secret` and/or HA password text → NVS; never commit |
| Default | `enabled: false` / omit block; HA enable can stop cleanly |
| Hard caps | CSMS wishes clamped by director hard cap / per-EVSE max |
| CSMS down | `fail_safe_amps` (≤ hard cap) |
| Remotes | RemoteStart/Stop/Reset/Unlock **DEFAULT OFF**; UpdateFirmware **always rejected** |
| Cloud | Lab = LAN/VPN CSMS; public cloud remotes need new risk accept |

## HA entities

See [`examples/ocpp-fragment.yaml`](../../examples/ocpp-fragment.yaml): enable switch,
CSMS URL / CP id / auth key texts, status sensors, fail-safe button, lab remote flags.

## Metering / status

Bridge registers MicroOCPP inputs from `twc_director` telemetry (A/V/Wh/power,
plugged/occupied/ev_ready/evse_ready). Gen2→OCPP status is approximate.

## Vendoring

```bash
# First time / after pin change:
./components/ocpp_client/scripts/fetch_deps.sh
```

Details: [`vendor/README.md`](vendor/README.md), [`docs/OCPP.md`](../../docs/OCPP.md).
