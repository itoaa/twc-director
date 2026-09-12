#pragma once

/* C bridge so ESPHome firmware never #includes MicroOcpp/ArduinoJson headers
 * (ArduinoJson v6 would otherwise override ESPHome's JSON stack). */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TWC_OCPP_MAX_CONNECTORS
#define TWC_OCPP_MAX_CONNECTORS 4
#endif

typedef void (*twc_ocpp_smart_current_cb_t)(float amps, void *user);
typedef void (*twc_ocpp_connector_current_cb_t)(unsigned connector_id, float amps, void *user);
typedef void (*twc_ocpp_change_config_cb_t)(const char *key, const char *value, void *user);
typedef void (*twc_ocpp_remote_event_cb_t)(const char *action, unsigned connector_id, bool accepted,
                                          const char *detail, void *user);

typedef struct {
  bool allow_remote_start; /* DEFAULT false — lab gate */
  bool allow_remote_stop;  /* DEFAULT false */
  bool allow_reset;        /* DEFAULT false */
  bool allow_unlock;       /* DEFAULT false — log only; Gen2 has no OCPP unlock actuator */
  /* UpdateFirmware: always rejected (no flag). */
} twc_ocpp_feature_flags_t;

typedef struct {
  /* Aggregate / connector-1 fallback (one-CP model). */
  float current_a;
  float voltage_v;
  float energy_wh; /* MO energy meter expects Wh */
  float power_w;
  bool plugged;     /* vehicle connected */
  bool occupied;    /* preparing / finishing vs available */
  bool ev_ready;    /* charging / contactor closed */
  bool evse_ready;  /* online + operative path */
  int online_count;

  /* Per OCPP connector 1..N ↔ TWC slot 0..N-1 */
  unsigned num_connectors; /* 1..TWC_OCPP_MAX_CONNECTORS */
  float conn_current_a[TWC_OCPP_MAX_CONNECTORS];
  float conn_voltage_v[TWC_OCPP_MAX_CONNECTORS];
  float conn_energy_wh[TWC_OCPP_MAX_CONNECTORS];
  float conn_power_w[TWC_OCPP_MAX_CONNECTORS];
  bool conn_plugged[TWC_OCPP_MAX_CONNECTORS];
  bool conn_occupied[TWC_OCPP_MAX_CONNECTORS];
  bool conn_ev_ready[TWC_OCPP_MAX_CONNECTORS];
  bool conn_evse_ready[TWC_OCPP_MAX_CONNECTORS];
  bool conn_valid[TWC_OCPP_MAX_CONNECTORS];
} twc_ocpp_telemetry_t;

typedef struct {
  const char *wss_url;           /* full wss://… or (lab) ws://…/ChargePointId */
  const char *charge_point_id;   /* basic-auth username */
  const char *authorization_key; /* basic-auth password */
  const char *vendor;
  const char *model;
  unsigned num_connectors; /* 1..TWC_OCPP_MAX_CONNECTORS (MO_NUMCONNECTORS = n+1) */
  /* TLS: default verify via crt_bundle. allow_insecure_tls is lab-only (RFC1918/.local). */
  bool allow_insecure_tls;       /* DEFAULT false — runtime gated to private lab hosts */
  bool allow_cleartext_ws;       /* DEFAULT false — ws:// only if ON + RFC1918/.local */
  bool crt_bundle_attach;        /* DEFAULT true when not insecure / no ca_cert */
  const char *ca_cert_pem;       /* optional PEM CA string; nullptr = unset */
  twc_ocpp_feature_flags_t flags;
  twc_ocpp_smart_current_cb_t on_smart_current;           /* connector 0 / global */
  twc_ocpp_connector_current_cb_t on_connector_current; /* connector 1..N */
  twc_ocpp_change_config_cb_t on_change_config;
  twc_ocpp_remote_event_cb_t on_remote_event; /* audit + fail-safe hooks */
  void *user;
} twc_ocpp_mocpp_config_t;

bool twc_ocpp_mocpp_start(const twc_ocpp_mocpp_config_t *cfg);
void twc_ocpp_mocpp_loop(void);
bool twc_ocpp_mocpp_is_connected(void);
void twc_ocpp_mocpp_stop(void);

/* Push latest director telemetry (called from ocpp_client loop). */
void twc_ocpp_mocpp_set_telemetry(const twc_ocpp_telemetry_t *telemetry);

/* Update lab feature flags at runtime (DEFAULT all false). */
void twc_ocpp_mocpp_set_feature_flags(const twc_ocpp_feature_flags_t *flags);

#ifdef __cplusplus
}
#endif
