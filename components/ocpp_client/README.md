# ocpp_client (ESPHome external component)

Native **OCPP 1.6J** Charge Point client for the TWC Director. Embeds
[MicroOCPP](https://github.com/matth-x/MicroOcpp) and maps Smart Charging onto
`twc_director` **global + per-connector** amp wishes (hard caps win).

## Safety (CISO)

| Rule | Behaviour |
|------|-----------|
| Transport | `wss://` default; `ws://` only with `allow_cleartext_ws` + RFC1918/`.local` (never public) |
| Secrets | `!secret` and/or HA password text → NVS; never commit. `ca_cert_text` state is redacted (`set (N bytes)`), never raw PEM |
| Default | `enabled: false` / omit block; HA enable can stop cleanly |
| Hard caps | CSMS wishes clamped by director hard cap / per-EVSE max |
| CSMS down | `fail_safe_amps` (≤ hard cap) |
| Remotes | RemoteStart/Stop/Reset/Unlock **DEFAULT OFF**; **forced OFF on cleartext `ws://`**; UpdateFirmware **always rejected** |
| Cloud | Lab = LAN/VPN CSMS; public cloud remotes need new risk accept |
| TLS / WS | Default `crt_bundle_attach: true`. Prefer `ca_cert` for lab CA. `allow_insecure_tls` / `allow_cleartext_ws` DEFAULT false; RFC1918/`.local` gated (CISO) |

## TLS options

```yaml
ocpp_client:
  # … enabled / csms_url / secrets …
  crt_bundle_attach: true          # default — public CA verify
  # ca_cert: !secret ocpp_lab_ca_pem
  allow_insecure_tls: false        # lab PoC only; WARN + RFC1918/.local gate
  allow_cleartext_ws: false        # lab LAN only; ws:// + RFC1918/.local; remove after wss
```

Self-signed / bare IP on LAN: use `ca_cert` **or** temporary `allow_insecure_tls: true`.
CitrineOS identification may use `ws://…:8081/<stationId>` with `allow_cleartext_ws: true` (Remote* stay OFF).
CitrineOS 1.6 WSS is often port **8092** (custom labs may use 8090). After wss handoff, do not leave `allow_cleartext_ws` in prod-config.

## HA entities

See [`examples/ocpp-fragment.yaml`](../../examples/ocpp-fragment.yaml): enable switch,
CSMS URL / CP id / auth key / **CA cert** texts, status sensors, fail-safe button, lab remote flags.

**Runtime CA:** paste a PEM into `ca_cert_text`. It is stored in NVS and used ahead of YAML
`ca_cert` and the Mozilla bundle (and ahead of `allow_insecure_tls` if both are set). HA state
shows `set (N bytes)` — never the PEM. Clear the field (empty) to **delete** the NVS override
and fall back to YAML `ca_cert`, or the bundle if YAML is unset.

## Metering / status

Bridge registers MicroOCPP inputs from `twc_director` telemetry (A/V/Wh/power,
plugged/occupied/ev_ready/evse_ready). Gen2→OCPP status is approximate.

## Vendoring

MicroOCPP (`v1.2.0`) and ArduinoJson (`v6.21.5`) live as **git submodules** under
`vendor/`. HA Device Builder does not need a manual fetch: if CMakeLists are
missing, `__init__.py` runs `scripts/fetch_deps.sh` automatically.

```bash
# Local clone / pin change:
git submodule update --init --recursive
# or:
./components/ocpp_client/scripts/fetch_deps.sh
```

Details: [`vendor/README.md`](vendor/README.md), [`docs/OCPP.md`](../../docs/OCPP.md).

## Enable ON — what to watch

`connection_state` goes to **`connecting`** immediately (init runs on the next loop tick).
Serial tags `ocpp_client` / `ocpp_bridge` / `ocpp_ws` print host/port/path **without** secrets.
If the socket never connects, after ~45s you get `error:connect-timeout` + fail-safe.
TLS handshake failures publish a specific `connection_state` (`error:tls-verify`,
`error:tls-cn`, `error:tls-alert`, `error:tls-timeout`, `error:tls-no-verify-option`,
`error:tls-insecure-needs-rebuild`) instead of a generic `error:ws-init` when the cause is known.
Logs include mbedtls/esp-tls codes only — never peer-cert PEM.
Full sequence: [`docs/OCPP.md`](../../docs/OCPP.md#connection-log-sequence-enable-on).

