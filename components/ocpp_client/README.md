# ocpp_client (ESPHome external component)

Native **OCPP 1.6J** Charge Point client for the TWC Director. Embeds
[MicroOCPP](https://github.com/matth-x/MicroOcpp) and maps Smart Charging /
max-amp configuration onto `twc_director` **global max only**.

## Safety (CISO)

| Rule | Behaviour |
|------|-----------|
| Transport | `csms_url` must be `wss://` (schema + runtime reject `ws://`) |
| Secrets | `authorization_key` / URL / CP id from `secrets.yaml` — never commit |
| Default | `enabled: false` (or omit the block entirely) |
| Hard caps | CSMS wishes go through `apply_external_global_max_a()` → clamp to YAML/compile hard cap |
| CSMS down | Applies `fail_safe_amps` (≤ hard cap), **not** full open |
| Scope | One Charge Point; no per-connector amp; load sharing stays in `twc_director` |

### v1 implemented / handled

- BootNotification, Heartbeat, StatusNotification, MeterValues — via MicroOCPP when `enabled: true` and vendor deps present
- `SetChargingProfile` → Smart Charging current output → global max (clamped)
- `ChangeConfiguration` keys `ChargePointMaxCurrent` / `MaxCurrent` / `TwcGlobalMaxCurrent` → global max
- Explicitly **ignored/rejected**: RemoteStart/Stop, Reset, UnlockConnector, UpdateFirmware

## YAML

See [`examples/ocpp-fragment.yaml`](../../examples/ocpp-fragment.yaml).

```yaml
ocpp_client:
  id: twc_ocpp
  enabled: false          # keep false until lab CSMS is ready
  twc_director_id: twc_component
  csms_url: !secret ocpp_csms_url
  charge_point_id: !secret ocpp_charge_point_id
  authorization_key: !secret ocpp_authorization_key
  fail_safe_amps: 6
```

Default `tesla-director.yaml` does **not** include this block (CI stays green).

## MicroOCPP vendoring

```bash
./components/ocpp_client/scripts/fetch_deps.sh
```

Places `vendor/MicroOcpp` + `vendor/ArduinoJson` as ESP-IDF sibling components.
Runtime uses committed `vendor/ocpp_mocpp_bridge` (C API) so ArduinoJson does not
collide with ESPHome’s JSON stack.
Also pulls managed component `espressif/esp_websocket_client` at compile time for
the WSS adapter. Details: [`vendor/README.md`](vendor/README.md).

## Lab vs CI

| Mode | What works |
|------|------------|
| `enabled: false` | Component loads, sensors report `disabled`, no MicroOCPP link — CI-safe |
| `enabled: true` + deps | WSS to CSMS, BootNotification/Heartbeat/SmartCharging path — **lab** |
| Live CSMS | Needs valid secrets, S3-class flash/RAM recommended, network to CSMS |

Remaining lab hardening: CA pin / custom client certs, MO flash store vs ESPHome FS,
Mongoose alternative if `esp_websocket_client` + ESPHome WiFi needs tuning on a board.
