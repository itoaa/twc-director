#pragma once

/* C bridge so ESPHome firmware never #includes MicroOcpp/ArduinoJson headers
 * (ArduinoJson v6 would otherwise override ESPHome's JSON stack). */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*twc_ocpp_smart_current_cb_t)(float amps, void *user);
typedef void (*twc_ocpp_change_config_cb_t)(const char *key, const char *value, void *user);

typedef struct {
  const char *wss_url;           /* full wss://…/ChargePointId */
  const char *charge_point_id;   /* basic-auth username */
  const char *authorization_key; /* basic-auth password */
  const char *vendor;
  const char *model;
  twc_ocpp_smart_current_cb_t on_smart_current;
  twc_ocpp_change_config_cb_t on_change_config;
  void *user;
} twc_ocpp_mocpp_config_t;

bool twc_ocpp_mocpp_start(const twc_ocpp_mocpp_config_t *cfg);
void twc_ocpp_mocpp_loop(void);
bool twc_ocpp_mocpp_is_connected(void);
void twc_ocpp_mocpp_stop(void);

#ifdef __cplusplus
}
#endif
