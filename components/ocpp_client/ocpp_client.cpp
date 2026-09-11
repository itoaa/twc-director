#include "ocpp_client.h"

#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"

#ifdef USE_MICROOCPP
#include <MicroOcpp.h>
#include <esp_websocket_client.h>
#include <esp_event.h>
#include <cstring>
#endif

namespace esphome {
namespace ocpp_client {

static const char *const TAG = "ocpp_client";

#ifdef USE_MICROOCPP

static void esp_ws_event_handler(void *handler_args, esp_event_base_t /*base*/, int32_t event_id,
                                 void *event_data) {
  auto *self = static_cast<EspIdfWsConnection *>(handler_args);
  if (self != nullptr) {
    self->on_event_(event_id, event_data);
  }
}

EspIdfWsConnection::~EspIdfWsConnection() { this->end(); }

bool EspIdfWsConnection::begin(const std::string &wss_url, const std::string &username,
                                 const std::string &auth_key) {
  if (!wss_url.empty() && wss_url.rfind("wss://", 0) != 0) {
    ESP_LOGE(TAG, "Refusing non-wss URL at runtime");
    return false;
  }

  this->end();

  esp_websocket_client_config_t cfg = {};
  cfg.uri = wss_url.c_str();
  cfg.transport = WEBSOCKET_TRANSPORT_OVER_SSL;
  cfg.disable_auto_reconnect = false;
  // OCPP subprotocol
  cfg.subprotocol = "ocpp1.6";
  if (!username.empty()) {
    cfg.username = username.c_str();
  }
  if (!auth_key.empty()) {
    // HTTP Basic auth over the WSS handshake (Authorization key / password).
    cfg.password = auth_key.c_str();
  }

  this->client_ = esp_websocket_client_init(&cfg);
  if (this->client_ == nullptr) {
    ESP_LOGE(TAG, "esp_websocket_client_init failed");
    return false;
  }

  esp_websocket_register_events(static_cast<esp_websocket_client_handle_t>(this->client_),
                                WEBSOCKET_EVENT_ANY, esp_ws_event_handler, this);
  esp_err_t err = esp_websocket_client_start(static_cast<esp_websocket_client_handle_t>(this->client_));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_websocket_client_start failed: %s", esp_err_to_name(err));
    this->end();
    return false;
  }
  ESP_LOGI(TAG, "WSS client started toward CSMS");
  return true;
}

void EspIdfWsConnection::end() {
  if (this->client_ != nullptr) {
    esp_websocket_client_stop(static_cast<esp_websocket_client_handle_t>(this->client_));
    esp_websocket_client_destroy(static_cast<esp_websocket_client_handle_t>(this->client_));
    this->client_ = nullptr;
  }
  this->connected_ = false;
}

void EspIdfWsConnection::loop() {
  // esp_websocket_client runs its own task; nothing required here.
}

bool EspIdfWsConnection::sendTXT(const char *msg, size_t length) {
  if (this->client_ == nullptr || !this->connected_) {
    return false;
  }
  int sent = esp_websocket_client_send_text(static_cast<esp_websocket_client_handle_t>(this->client_),
                                            msg, static_cast<int>(length), pdMS_TO_TICKS(1000));
  return sent >= 0;
}

void EspIdfWsConnection::setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback &receiveTXT) {
  this->receive_txt_ = receiveTXT;
}

void EspIdfWsConnection::on_event_(int32_t event_id, void *event_data) {
  auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      this->connected_ = true;
      this->last_connected_ms_ = millis();
      ESP_LOGI(TAG, "WSS connected");
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      this->connected_ = false;
      ESP_LOGW(TAG, "WSS disconnected");
      break;
    case WEBSOCKET_EVENT_DATA:
      if (data != nullptr && data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->data_ptr != nullptr &&
          data->data_len > 0 && this->receive_txt_) {
        this->last_recv_ms_ = millis();
        this->receive_txt_(static_cast<const char *>(data->data_ptr), static_cast<size_t>(data->data_len));
      }
      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGW(TAG, "WSS error");
      break;
    default:
      break;
  }
}

#endif  // USE_MICROOCPP

float OcppClientComponent::apply_csms_global_max_(float amps) {
  if (this->director_ == nullptr) {
    return 0.0f;
  }
  float applied = this->director_->apply_external_global_max_a(amps);
  this->last_applied_amps_ = applied;
  if (this->last_applied_amps_sensor_ != nullptr) {
    this->last_applied_amps_sensor_->publish_state(applied);
  }
  return applied;
}

void OcppClientComponent::publish_state_(bool connected, const char *state) {
  if (this->connected_sensor_ != nullptr) {
    this->connected_sensor_->publish_state(connected);
  }
  if (this->connection_state_sensor_ != nullptr && state != nullptr) {
    this->connection_state_sensor_->publish_state(state);
  }
}

void OcppClientComponent::apply_fail_safe_(const char *reason) {
  if (this->director_ == nullptr) {
    return;
  }
  float cap = this->director_->hard_cap_global_max_a();
  float safe = this->fail_safe_amps_;
  if (cap > 0.0f && safe > cap) {
    safe = cap;
  }
  ESP_LOGW(TAG, "Fail-safe amp apply (%.1fA) — %s", safe, reason != nullptr ? reason : "unknown");
  this->apply_csms_global_max_(safe);
}

void OcppClientComponent::setup() {
  if (!this->enabled_) {
    ESP_LOGI(TAG, "OCPP client disabled (default). No CSMS connection.");
    this->publish_state_(false, "disabled");
    return;
  }

  if (this->director_ == nullptr) {
    ESP_LOGE(TAG, "twc_director_id missing — refusing to start OCPP");
    this->publish_state_(false, "error:no-director");
    this->enabled_ = false;
    return;
  }

  // Defense in depth: reject ws:// even if schema was bypassed.
  if (this->csms_url_.rfind("wss://", 0) != 0) {
    ESP_LOGE(TAG, "csms_url must be wss:// — OCPP left disabled");
    this->publish_state_(false, "error:not-wss");
    this->enabled_ = false;
    return;
  }

  // Clamp configured fail-safe to hard cap once at boot.
  float cap = this->director_->hard_cap_global_max_a();
  if (cap > 0.0f && this->fail_safe_amps_ > cap) {
    ESP_LOGW(TAG, "fail_safe_amps %.1fA > hard cap %.1fA — clamping", this->fail_safe_amps_, cap);
    this->fail_safe_amps_ = cap;
  }

  this->publish_state_(false, "starting");
  this->maybe_init_microocpp_();
}

void OcppClientComponent::maybe_init_microocpp_() {
#ifdef USE_MICROOCPP
  if (this->mocpp_started_) {
    return;
  }

  // Build backend URL without trailing charge box id if the user already
  // included the full path; MicroOCPP Connection API takes the full WS URL.
  // Prefer full wss URL = backend + "/" + charge_point_id when URL has no CP id.
  std::string url = this->csms_url_;
  if (!this->charge_point_id_.empty()) {
    // If URL does not already end with the charge point id, append it.
    if (url.size() < this->charge_point_id_.size() ||
        url.compare(url.size() - this->charge_point_id_.size(), this->charge_point_id_.size(),
                    this->charge_point_id_) != 0) {
      if (url.back() != '/') {
        url.push_back('/');
      }
      url += this->charge_point_id_;
    }
  }

  // Username for basic auth is typically the charge point id.
  // EspIdfWsConnection currently maps authorization_key → WS password.
  if (!this->ws_.begin(url, this->charge_point_id_, this->authorization_key_)) {
    this->publish_state_(false, "error:ws-init");
    this->apply_fail_safe_("ws init failed");
    return;
  }

  ChargerCredentials creds(this->model_.c_str(), this->vendor_.c_str());
  // Deactivate on-flash MO store for PoC (avoids fighting ESPHome FS layout).
  auto fs = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Deactivate);
  mocpp_initialize(this->ws_, creds, fs, false, MicroOcpp::ProtocolVersion(1, 6));

  // Map Smart Charging current limit → director global max (hard-capped).
  setSmartChargingCurrentOutput([this](float amps) {
    if (amps < 0.0f) {
      // MicroOCPP uses -1 for "undefined" — hold last / do not open full.
      ESP_LOGI(TAG, "SmartCharging undefined (-1) — holding last applied / fail-safe");
      return;
    }
    this->apply_csms_global_max_(amps);
  });

  // Reject Reset (v1 out of scope).
  setOnResetNotify([](bool /*is_hard*/) -> bool {
    ESP_LOGW(TAG, "Rejecting OCPP Reset (not implemented in v1)");
    return false;
  });

  // Defense: log (and leave MicroOCPP default) for remote start/stop — we do not
  // expose local RemoteStart/Stop hooks; transactions are not driven from CSMS in v1.
  setOnReceiveRequest("RemoteStartTransaction", [](JsonObject) {
    ESP_LOGW(TAG, "RemoteStartTransaction received — ignored (v1 out of scope)");
  });
  setOnReceiveRequest("RemoteStopTransaction", [](JsonObject) {
    ESP_LOGW(TAG, "RemoteStopTransaction received — ignored (v1 out of scope)");
  });
  setOnReceiveRequest("UnlockConnector", [](JsonObject) {
    ESP_LOGW(TAG, "UnlockConnector received — ignored (v1 out of scope)");
  });
  setOnReceiveRequest("UpdateFirmware", [](JsonObject) {
    ESP_LOGW(TAG, "UpdateFirmware received — ignored (v1 out of scope)");
  });

  // Also watch ChangeConfiguration for ChargePointMaxCurrent-style keys.
  setOnReceiveRequest("ChangeConfiguration", [this](JsonObject req) {
    const char *key = req["key"] | "";
    const char *value = req["value"] | "";
    if (key == nullptr || value == nullptr) {
      return;
    }
    // Common CSMS keys that mean "site max amp".
    if (!strcmp(key, "ChargePointMaxCurrent") || !strcmp(key, "MaxCurrent") ||
        !strcmp(key, "TwcGlobalMaxCurrent")) {
      float amps = atof(value);
      if (amps > 0.0f) {
        this->apply_csms_global_max_(amps);
      }
    }
  });

  this->mocpp_started_ = true;
  this->publish_state_(false, "connecting");
  ESP_LOGI(TAG, "MicroOCPP initialized (BootNotification/Heartbeat via library)");
#else
  ESP_LOGE(TAG, "enabled:true but firmware built without USE_MICROOCPP — "
                "fetch vendor deps and rebuild");
  this->publish_state_(false, "error:no-microocpp");
  this->apply_fail_safe_("MicroOCPP not linked");
#endif
}

void OcppClientComponent::poll_connection_() {
#ifdef USE_MICROOCPP
  bool connected = this->ws_.isConnected();
  if (connected != this->was_connected_) {
    if (connected) {
      this->publish_state_(true, "connected");
    } else {
      this->publish_state_(false, "disconnected");
      this->apply_fail_safe_("CSMS disconnected");
    }
    this->was_connected_ = connected;
  }
#endif
}

void OcppClientComponent::loop() {
  if (!this->enabled_) {
    return;
  }

#ifdef USE_MICROOCPP
  if (this->mocpp_started_) {
    mocpp_loop();
    this->poll_connection_();
  }
#endif
}

}  // namespace ocpp_client
}  // namespace esphome
