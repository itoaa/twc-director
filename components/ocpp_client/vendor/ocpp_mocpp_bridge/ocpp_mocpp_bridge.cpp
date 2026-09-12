#include "ocpp_mocpp_bridge.h"

#include "esp_idf_ws_connection.h"

#include <MicroOcpp.h>
#include <MicroOcpp/Model/ConnectorBase/Notification.h>
#include <esp_log.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
const char *TAG = "ocpp_bridge";
twc_ocpp::EspIdfWsConnection *g_ws = nullptr;
bool g_started = false;
bool g_cleartext_ws = false; /* set when active transport is ws:// */
char g_last_error[48] = {};

void remember_error_(const char *code) {
  if (code == nullptr || code[0] == '\0') {
    g_last_error[0] = '\0';
    return;
  }
  std::snprintf(g_last_error, sizeof(g_last_error), "%s", code);
}
twc_ocpp_mocpp_config_t g_cfg{};
twc_ocpp_feature_flags_t g_flags{};
twc_ocpp_telemetry_t g_telem{};

void force_remotes_off_if_cleartext_(const char *why) {
  if (!g_cleartext_ws) {
    return;
  }
  if (g_flags.allow_remote_start || g_flags.allow_remote_stop || g_flags.allow_reset ||
      g_flags.allow_unlock) {
    ESP_LOGW(TAG,
             "cleartext ws: forcing Remote* flags OFF (%s) — CISO: remotes disabled on ws://",
             why != nullptr ? why : "?");
  }
  g_flags.allow_remote_start = false;
  g_flags.allow_remote_stop = false;
  g_flags.allow_reset = false;
  g_flags.allow_unlock = false;
}

void audit_(const char *action, unsigned connector_id, bool accepted, const char *detail) {
  ESP_LOGW(TAG, "AUDIT %s connector=%u accepted=%d detail=%s", action, connector_id, accepted ? 1 : 0,
           detail != nullptr ? detail : "");
  if (g_cfg.on_remote_event) {
    g_cfg.on_remote_event(action, connector_id, accepted, detail, g_cfg.user);
  }
}

bool telem_plugged_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return g_telem.conn_plugged[connector_id - 1];
  }
  return g_telem.plugged;
}
bool telem_occupied_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return g_telem.conn_occupied[connector_id - 1];
  }
  return g_telem.occupied;
}
bool telem_ev_ready_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return g_telem.conn_ev_ready[connector_id - 1];
  }
  return g_telem.ev_ready;
}
bool telem_evse_ready_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return g_telem.conn_evse_ready[connector_id - 1];
  }
  return g_telem.evse_ready;
}
int telem_energy_wh_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return static_cast<int>(g_telem.conn_energy_wh[connector_id - 1]);
  }
  return static_cast<int>(g_telem.energy_wh);
}
float telem_power_w_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return g_telem.conn_power_w[connector_id - 1];
  }
  return g_telem.power_w;
}
float telem_current_a_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return g_telem.conn_current_a[connector_id - 1];
  }
  return g_telem.current_a;
}
float telem_voltage_v_(unsigned connector_id) {
  if (connector_id >= 1 && connector_id <= TWC_OCPP_MAX_CONNECTORS &&
      g_telem.conn_valid[connector_id - 1]) {
    return g_telem.conn_voltage_v[connector_id - 1];
  }
  return g_telem.voltage_v;
}

void register_connector_inputs_(unsigned connector_id) {
  setConnectorPluggedInput([connector_id]() { return telem_plugged_(connector_id); }, connector_id);
  setOccupiedInput([connector_id]() { return telem_occupied_(connector_id); }, connector_id);
  setEvReadyInput([connector_id]() { return telem_ev_ready_(connector_id); }, connector_id);
  setEvseReadyInput([connector_id]() { return telem_evse_ready_(connector_id); }, connector_id);
  setEnergyMeterInput([connector_id]() { return telem_energy_wh_(connector_id); }, connector_id);
  setPowerMeterInput([connector_id]() { return telem_power_w_(connector_id); }, connector_id);
  addMeterValueInput([connector_id]() { return telem_current_a_(connector_id); }, "Current.Import", "A",
                     "Outlet", nullptr, connector_id);
  addMeterValueInput([connector_id]() { return telem_voltage_v_(connector_id); }, "Voltage", "V", "Outlet",
                     nullptr, connector_id);
  addMeterValueInput([connector_id]() { return telem_energy_wh_(connector_id) / 1000.0f; }, "Energy.Active.Import.Register",
                     "kWh", "Outlet", nullptr, connector_id);

  setSmartChargingCurrentOutput(
      [connector_id](float amps) {
        if (amps < 0.0f) {
          return;
        }
        if (g_cfg.on_connector_current) {
          g_cfg.on_connector_current(connector_id, amps, g_cfg.user);
        }
      },
      connector_id);

  setTxNotificationOutput(
      [connector_id](MicroOcpp::Transaction *tx, MicroOcpp::TxNotification note) {
        (void) tx;
        if (note == MicroOcpp::TxNotification::RemoteStart) {
          if (!g_flags.allow_remote_start) {
            endTransaction(nullptr, "Other", connector_id);
            audit_("RemoteStart", connector_id, false, "flag_off_undone");
          } else {
            audit_("RemoteStart", connector_id, true, "lab_flag_on");
          }
        } else if (note == MicroOcpp::TxNotification::RemoteStop) {
          if (!g_flags.allow_remote_stop) {
            // MO already ended OCPP tx; do not touch amp path when flag off.
            audit_("RemoteStop", connector_id, false, "flag_off_ocpp_tx_only");
          } else {
            audit_("RemoteStop", connector_id, true, "fail_safe_requested");
          }
        }
      },
      connector_id);
}

}  // namespace

extern "C" bool twc_ocpp_mocpp_start(const twc_ocpp_mocpp_config_t *cfg) {
  if (cfg == nullptr || cfg->wss_url == nullptr) {
    return false;
  }
  if (g_started) {
    return true;
  }
  g_cfg = *cfg;
  g_flags = cfg->flags;
  {
    const char *u = cfg->wss_url ? cfg->wss_url : "";
    g_cleartext_ws = (std::strncmp(u, "ws://", 5) == 0);
  }
  force_remotes_off_if_cleartext_("mocpp_start");

  g_ws = new twc_ocpp::EspIdfWsConnection();
  const char *user = cfg->charge_point_id ? cfg->charge_point_id : "";
  const char *pass = cfg->authorization_key ? cfg->authorization_key : "";
  // Redact userinfo for logs — never print auth key / password.
  {
    std::string url = cfg->wss_url ? cfg->wss_url : "";
    std::string rest = url;
    const char *scheme = "wss://";
    if (rest.rfind("wss://", 0) == 0) {
      rest = rest.substr(6);
    } else if (rest.rfind("ws://", 0) == 0) {
      scheme = "ws://";
      rest = rest.substr(5);
    } else {
      ESP_LOGE(TAG, "before g_ws->begin: non-ws/wss url_len=%u", static_cast<unsigned>(url.size()));
      delete g_ws;
      g_ws = nullptr;
      g_cleartext_ws = false;
      return false;
    }
    size_t slash = rest.find('/');
    size_t at = rest.find('@');
    if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
      rest = rest.substr(at + 1);
    }
    ESP_LOGI(TAG, "before g_ws->begin: url_len=%u redacted=%s%s user_set=%d auth_key_set=%d cleartext=%d",
             static_cast<unsigned>(url.size()), scheme, rest.c_str(), user[0] ? 1 : 0, pass[0] ? 1 : 0,
             g_cleartext_ws ? 1 : 0);
  }
  twc_ocpp::EspIdfWsTlsOptions tls{};
  tls.allow_insecure_tls = cfg->allow_insecure_tls;
  tls.allow_cleartext_ws = cfg->allow_cleartext_ws;
  tls.crt_bundle_attach = cfg->crt_bundle_attach;
  tls.ca_cert_pem = cfg->ca_cert_pem;
  if (tls.allow_cleartext_ws && g_cleartext_ws) {
    ESP_LOGW(TAG, "WS: allow_cleartext_ws requested (will apply only if host is RFC1918/.local lab)");
  } else if (tls.ca_cert_pem && tls.ca_cert_pem[0]) {
    ESP_LOGI(TAG, "TLS: custom ca_cert PEM (%u bytes, not logged)",
             (unsigned) strlen(tls.ca_cert_pem));
    if (tls.allow_insecure_tls) {
      ESP_LOGI(TAG, "TLS: allow_insecure_tls ignored because ca_cert is set (verify wins)");
    }
  } else if (tls.allow_insecure_tls) {
    ESP_LOGW(TAG, "TLS: allow_insecure_tls requested (will apply only if host is RFC1918/.local lab)");
  } else if (tls.crt_bundle_attach) {
    ESP_LOGI(TAG, "TLS: crt_bundle_attach (default verify)");
  }
  if (!g_ws->begin(cfg->wss_url, user, pass, tls)) {
    const char *err = g_ws->last_error();
    remember_error_(err);
    ESP_LOGE(TAG, "after g_ws->begin: FAILED category=%s", err ? err : "error:ws-init");
    delete g_ws;
    g_ws = nullptr;
    return false;
  }
  remember_error_(nullptr);
  ESP_LOGI(TAG, "after g_ws->begin: OK");

  const char *model = cfg->model ? cfg->model : "TWC-Director";
  const char *vendor = cfg->vendor ? cfg->vendor : "itoaa";
  ChargerCredentials creds(model, vendor);
  auto fs = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Deactivate);
  // OCPP 1.6J only — OCPP 2.0.1 not enabled (MO_ENABLE_V201 remains 0).
  ESP_LOGI(TAG, "before mocpp_initialize (1.6J)");
  mocpp_initialize(*g_ws, creds, fs, false, MicroOcpp::ProtocolVersion(1, 6));
  ESP_LOGI(TAG, "after mocpp_initialize");

  // Global / CP-level (connectorId 0) smart charging → site hard-capped global max.
  setSmartChargingCurrentOutput([](float amps) {
    if (g_cfg.on_smart_current) {
      g_cfg.on_smart_current(amps, g_cfg.user);
    }
  }, 0);

  unsigned n = cfg->num_connectors;
  if (n < 1) {
    n = 1;
  }
  if (n > TWC_OCPP_MAX_CONNECTORS) {
    n = TWC_OCPP_MAX_CONNECTORS;
  }
  g_telem.num_connectors = n;
  for (unsigned c = 1; c <= n; c++) {
    register_connector_inputs_(c);
  }

  setOnResetNotify([](bool is_hard) -> bool {
    if (!g_flags.allow_reset) {
      audit_("Reset", 0, false, is_hard ? "hard_flag_off" : "soft_flag_off");
      return false;
    }
    audit_("Reset", 0, true, is_hard ? "hard_fail_safe" : "soft_fail_safe");
    return true;  // allow MO reset path; ocpp_client applies fail-safe via on_remote_event
  });

  setOnReceiveRequest("RemoteStartTransaction", [](JsonObject req) {
    unsigned cid = static_cast<unsigned>(req["connectorId"] | 1);
    const char *id_tag = req["idTag"] | "";
    char detail[96];
    snprintf(detail, sizeof(detail), "idTag=%s flag=%d", id_tag, g_flags.allow_remote_start ? 1 : 0);
    // Acceptance/undo handled in TxNotification; this is early audit.
    ESP_LOGI(TAG, "RemoteStartTransaction received: %s", detail);
  });
  setOnReceiveRequest("RemoteStopTransaction", [](JsonObject req) {
    int txid = req["transactionId"] | -1;
    char detail[64];
    snprintf(detail, sizeof(detail), "transactionId=%d flag=%d", txid, g_flags.allow_remote_stop ? 1 : 0);
    ESP_LOGI(TAG, "RemoteStopTransaction received: %s", detail);
  });
  setOnReceiveRequest("UnlockConnector", [](JsonObject req) {
    unsigned cid = static_cast<unsigned>(req["connectorId"] | 1);
    if (!g_flags.allow_unlock) {
      audit_("UnlockConnector", cid, false, "flag_off");
    } else {
      // Gen2 has no standardized unlock actuator — acknowledge log only.
      audit_("UnlockConnector", cid, false, "no_actuator_gen2");
    }
  });
  setOnReceiveRequest("UpdateFirmware", [](JsonObject) {
    audit_("UpdateFirmware", 0, false, "always_rejected_no_signed_ota");
  });

  setOnReceiveRequest("ChangeConfiguration", [](JsonObject req) {
    const char *key = req["key"] | "";
    const char *value = req["value"] | "";
    if (g_cfg.on_change_config) {
      g_cfg.on_change_config(key, value, g_cfg.user);
    }
  });

  g_started = true;
  ESP_LOGI(TAG, "MicroOCPP started (1.6J, connectors=%u, remote flags default off)", n);
  return true;
}

extern "C" void twc_ocpp_mocpp_loop(void) {
  if (g_started) {
    mocpp_loop();
  }
}

extern "C" bool twc_ocpp_mocpp_is_connected(void) {
  return g_ws != nullptr && g_ws->isConnected();
}

extern "C" const char *twc_ocpp_mocpp_last_error(void) {
  if (g_ws != nullptr) {
    const char *err = g_ws->last_error();
    if (err != nullptr && err[0] != '\0') {
      return err;
    }
  }
  return g_last_error[0] ? g_last_error : nullptr;
}

extern "C" void twc_ocpp_mocpp_stop(void) {
  if (!g_started) {
    return;
  }
  mocpp_deinitialize();
  if (g_ws) {
    g_ws->end();
    delete g_ws;
    g_ws = nullptr;
  }
  g_started = false;
  g_cleartext_ws = false;
  remember_error_(nullptr);
  std::memset(&g_telem, 0, sizeof(g_telem));
}

extern "C" void twc_ocpp_mocpp_set_telemetry(const twc_ocpp_telemetry_t *telemetry) {
  if (telemetry == nullptr) {
    return;
  }
  g_telem = *telemetry;
}

extern "C" void twc_ocpp_mocpp_set_feature_flags(const twc_ocpp_feature_flags_t *flags) {
  if (flags == nullptr) {
    return;
  }
  g_flags = *flags;
  force_remotes_off_if_cleartext_("set_feature_flags");
  ESP_LOGI(TAG, "Feature flags: remote_start=%d remote_stop=%d reset=%d unlock=%d cleartext_ws=%d",
           g_flags.allow_remote_start ? 1 : 0, g_flags.allow_remote_stop ? 1 : 0,
           g_flags.allow_reset ? 1 : 0, g_flags.allow_unlock ? 1 : 0, g_cleartext_ws ? 1 : 0);
}
