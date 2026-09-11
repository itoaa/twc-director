#include "ocpp_client.h"

#include "esphome/core/log.h"

#include <cstring>
#include <cstdlib>

#ifdef USE_MICROOCPP
#include "ocpp_mocpp_bridge.h"
#endif

namespace esphome {
namespace ocpp_client {

static const char *const TAG = "ocpp_client";

#ifdef USE_MICROOCPP
static void smart_current_thunk(float amps, void *user) {
  auto *self = static_cast<OcppClientComponent *>(user);
  if (self != nullptr) {
    self->on_smart_current_(amps);
  }
}
static void change_config_thunk(const char *key, const char *value, void *user) {
  auto *self = static_cast<OcppClientComponent *>(user);
  if (self != nullptr) {
    self->on_change_config_(key, value);
  }
}
#endif

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

void OcppClientComponent::on_smart_current_(float amps) {
  if (amps < 0.0f) {
    ESP_LOGI(TAG, "SmartCharging undefined (-1) — holding last applied");
    return;
  }
  this->apply_csms_global_max_(amps);
}

void OcppClientComponent::on_change_config_(const char *key, const char *value) {
  if (key == nullptr || value == nullptr) {
    return;
  }
  if (!strcmp(key, "ChargePointMaxCurrent") || !strcmp(key, "MaxCurrent") ||
      !strcmp(key, "TwcGlobalMaxCurrent")) {
    float amps = atof(value);
    if (amps > 0.0f) {
      this->apply_csms_global_max_(amps);
    }
  }
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

  if (this->csms_url_.rfind("wss://", 0) != 0) {
    ESP_LOGE(TAG, "csms_url must be wss:// — OCPP left disabled");
    this->publish_state_(false, "error:not-wss");
    this->enabled_ = false;
    return;
  }

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

  this->resolved_url_ = this->csms_url_;
  if (!this->charge_point_id_.empty()) {
    if (this->resolved_url_.size() < this->charge_point_id_.size() ||
        this->resolved_url_.compare(this->resolved_url_.size() - this->charge_point_id_.size(),
                                    this->charge_point_id_.size(), this->charge_point_id_) != 0) {
      if (this->resolved_url_.back() != '/') {
        this->resolved_url_.push_back('/');
      }
      this->resolved_url_ += this->charge_point_id_;
    }
  }

  twc_ocpp_mocpp_config_t cfg = {};
  cfg.wss_url = this->resolved_url_.c_str();
  cfg.charge_point_id = this->charge_point_id_.c_str();
  cfg.authorization_key = this->authorization_key_.c_str();
  cfg.vendor = this->vendor_.c_str();
  cfg.model = this->model_.c_str();
  cfg.on_smart_current = smart_current_thunk;
  cfg.on_change_config = change_config_thunk;
  cfg.user = this;

  if (!twc_ocpp_mocpp_start(&cfg)) {
    this->publish_state_(false, "error:ws-init");
    this->apply_fail_safe_("mocpp/ws init failed");
    return;
  }

  this->mocpp_started_ = true;
  this->publish_state_(false, "connecting");
  ESP_LOGI(TAG, "MicroOCPP bridge initialized");
#else
  ESP_LOGE(TAG, "enabled:true but firmware built without USE_MICROOCPP");
  this->publish_state_(false, "error:no-microocpp");
  this->apply_fail_safe_("MicroOCPP not linked");
#endif
}

void OcppClientComponent::poll_connection_() {
#ifdef USE_MICROOCPP
  bool connected = twc_ocpp_mocpp_is_connected();
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
    twc_ocpp_mocpp_loop();
    this->poll_connection_();
  }
#endif
}

}  // namespace ocpp_client
}  // namespace esphome
