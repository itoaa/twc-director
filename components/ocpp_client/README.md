# ocpp_client (ESPHome external component — PoC stub)

Placeholder for a future ESPHome component that embeds MicroOCPP and maps
`SetChargingProfile` / max-amp configuration onto `twc_director` global max.

## Planned behaviour

- Disabled unless `ocpp_client:` is present **and** `enabled: true`
- Requires `csms_url: wss://...` and auth from secrets
- On profile/config max amp: clamp then call into director global max path
- Does not own RS-485; does not bypass contactor / master gates

## Not implemented yet

No MicroOCPP sources are vendored in this commit. Next spike: link MicroOCPP
under ESP-IDF, run BootNotification + Heartbeat against a test CSMS, then wire
global amp.
