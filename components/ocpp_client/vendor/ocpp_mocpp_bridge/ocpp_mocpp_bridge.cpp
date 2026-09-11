#include "ocpp_mocpp_bridge.h"

#include "esp_idf_ws_connection.h"

#include <MicroOcpp.h>
#include <esp_log.h>
#include <cstring>
#include <cstdlib>
#include <string>

namespace {
const char *TAG = "ocpp_bridge";
twc_ocpp::EspIdfWsConnection *g_ws = nullptr;
bool g_started = false;
twc_ocpp_mocpp_config_t g_cfg{};
}  // namespace

extern "C" bool twc_ocpp_mocpp_start(const twc_ocpp_mocpp_config_t *cfg) {
  if (cfg == nullptr || cfg->wss_url == nullptr) {
    return false;
  }
  if (g_started) {
    return true;
  }
  g_cfg = *cfg;

  g_ws = new twc_ocpp::EspIdfWsConnection();
  const char *user = cfg->charge_point_id ? cfg->charge_point_id : "";
  const char *pass = cfg->authorization_key ? cfg->authorization_key : "";
  if (!g_ws->begin(cfg->wss_url, user, pass)) {
    delete g_ws;
    g_ws = nullptr;
    return false;
  }

  const char *model = cfg->model ? cfg->model : "TWC-Director";
  const char *vendor = cfg->vendor ? cfg->vendor : "itoaa";
  ChargerCredentials creds(model, vendor);
  auto fs = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Deactivate);
  mocpp_initialize(*g_ws, creds, fs, false, MicroOcpp::ProtocolVersion(1, 6));

  setSmartChargingCurrentOutput([](float amps) {
    if (g_cfg.on_smart_current) {
      g_cfg.on_smart_current(amps, g_cfg.user);
    }
  });

  setOnResetNotify([](bool) -> bool {
    ESP_LOGW(TAG, "Rejecting OCPP Reset (v1 out of scope)");
    return false;
  });

  setOnReceiveRequest("RemoteStartTransaction", [](JsonObject) {
    ESP_LOGW(TAG, "RemoteStartTransaction ignored (v1)");
  });
  setOnReceiveRequest("RemoteStopTransaction", [](JsonObject) {
    ESP_LOGW(TAG, "RemoteStopTransaction ignored (v1)");
  });
  setOnReceiveRequest("UnlockConnector", [](JsonObject) {
    ESP_LOGW(TAG, "UnlockConnector ignored (v1)");
  });
  setOnReceiveRequest("UpdateFirmware", [](JsonObject) {
    ESP_LOGW(TAG, "UpdateFirmware ignored (v1)");
  });

  setOnReceiveRequest("ChangeConfiguration", [](JsonObject req) {
    const char *key = req["key"] | "";
    const char *value = req["value"] | "";
    if (g_cfg.on_change_config) {
      g_cfg.on_change_config(key, value, g_cfg.user);
    }
  });

  g_started = true;
  ESP_LOGI(TAG, "MicroOCPP started");
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
}
