#pragma once

#include <cstdint>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/twc_director/twc_director_component.h"

namespace esphome {
namespace ocpp_client {

class OcppClientComponent;

class OcppEnableSwitch : public switch_::Switch {
 public:
  void set_parent(OcppClientComponent *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  OcppClientComponent *parent_{nullptr};
};

class OcppFeatureSwitch : public switch_::Switch {
 public:
  enum Kind { REMOTE_START, REMOTE_STOP, RESET, UNLOCK };
  void set_parent(OcppClientComponent *parent, Kind kind) {
    this->parent_ = parent;
    this->kind_ = kind;
  }

 protected:
  void write_state(bool state) override;
  OcppClientComponent *parent_{nullptr};
  Kind kind_{REMOTE_START};
};

class OcppParamText : public text::Text {
 public:
  enum Kind { CSMS_URL, CHARGE_POINT_ID, AUTH_KEY };
  void set_parent(OcppClientComponent *parent, Kind kind) {
    this->parent_ = parent;
    this->kind_ = kind;
  }

 protected:
  void control(const std::string &value) override;
  OcppClientComponent *parent_{nullptr};
  Kind kind_{CSMS_URL};
};

class OcppFailSafeButton : public button::Button {
 public:
  void set_parent(OcppClientComponent *parent) { this->parent_ = parent; }

 protected:
  void press_action() override;
  OcppClientComponent *parent_{nullptr};
};

class OcppClientComponent : public Component {
 public:
  void set_director(twc_director::TWCDirectorComponent *director) { this->director_ = director; }
  void set_enabled_default(bool enabled) { this->enabled_default_ = enabled; }
  void set_csms_url(const std::string &url) { this->csms_url_ = url; }
  void set_charge_point_id(const std::string &id) { this->charge_point_id_ = id; }
  void set_authorization_key(const std::string &key) { this->authorization_key_ = key; }
  void set_fail_safe_amps(float amps) { this->fail_safe_amps_ = amps; }
  void set_charge_point_vendor(const std::string &v) { this->vendor_ = v; }
  void set_charge_point_model(const std::string &m) { this->model_ = m; }
  void set_allow_insecure_tls(bool v) { this->allow_insecure_tls_ = v; }
  void set_allow_cleartext_ws(bool v) { this->allow_cleartext_ws_ = v; }
  void set_crt_bundle_attach(bool v) { this->crt_bundle_attach_ = v; }
  void set_ca_cert(const std::string &pem) { this->ca_cert_ = pem; }

  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_connection_state_sensor(text_sensor::TextSensor *s) { this->connection_state_sensor_ = s; }
  void set_last_applied_amps_sensor(sensor::Sensor *s) { this->last_applied_amps_sensor_ = s; }

  void set_enable_switch(OcppEnableSwitch *s) {
    this->enable_switch_ = s;
    if (s != nullptr) {
      s->set_parent(this);
    }
  }
  void set_csms_url_text(OcppParamText *t) {
    this->csms_url_text_ = t;
    if (t != nullptr) {
      t->set_parent(this, OcppParamText::CSMS_URL);
    }
  }
  void set_charge_point_id_text(OcppParamText *t) {
    this->charge_point_id_text_ = t;
    if (t != nullptr) {
      t->set_parent(this, OcppParamText::CHARGE_POINT_ID);
    }
  }
  void set_authorization_key_text(OcppParamText *t) {
    this->authorization_key_text_ = t;
    if (t != nullptr) {
      t->set_parent(this, OcppParamText::AUTH_KEY);
    }
  }
  void set_fail_safe_button(OcppFailSafeButton *b) {
    this->fail_safe_button_ = b;
    if (b != nullptr) {
      b->set_parent(this);
    }
  }
  void set_remote_start_switch(OcppFeatureSwitch *s) {
    this->remote_start_switch_ = s;
    if (s != nullptr) {
      s->set_parent(this, OcppFeatureSwitch::REMOTE_START);
    }
  }
  void set_remote_stop_switch(OcppFeatureSwitch *s) {
    this->remote_stop_switch_ = s;
    if (s != nullptr) {
      s->set_parent(this, OcppFeatureSwitch::REMOTE_STOP);
    }
  }
  void set_reset_switch(OcppFeatureSwitch *s) {
    this->reset_switch_ = s;
    if (s != nullptr) {
      s->set_parent(this, OcppFeatureSwitch::RESET);
    }
  }
  void set_unlock_switch(OcppFeatureSwitch *s) {
    this->unlock_switch_ = s;
    if (s != nullptr) {
      s->set_parent(this, OcppFeatureSwitch::UNLOCK);
    }
  }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void on_smart_current_(float amps);
  void on_connector_current_(unsigned connector_id, float amps);
  void on_change_config_(const char *key, const char *value);
  void on_remote_event_(const char *action, unsigned connector_id, bool accepted, const char *detail);

  void handle_enable_write_(bool state);
  /* Returns effective state to publish (may force false on cleartext ws). */
  bool handle_feature_write_(OcppFeatureSwitch::Kind kind, bool state);
  void handle_param_write_(OcppParamText::Kind kind, const std::string &value);
  void handle_fail_safe_press_();

 protected:
  float apply_csms_global_max_(float amps);
  void publish_state_(bool connected, const char *state);
  void apply_fail_safe_(const char *reason);
  void request_microocpp_start_(const char *reason);
  void maybe_init_microocpp_();
  void stop_microocpp_(const char *reason);
  void poll_connection_();
  void check_connect_timeout_();
  void push_telemetry_();
  void push_feature_flags_();
  void load_runtime_prefs_();
  void save_runtime_prefs_();
  bool runtime_enabled_() const;
  std::string effective_url_() const;
  static std::string redact_wss_url_(const std::string &url);
  static void log_wss_target_(const char *phase, const std::string &url);
  bool is_cleartext_ws_url_() const;
  bool url_scheme_allowed_(const std::string &url) const;

  twc_director::TWCDirectorComponent *director_{nullptr};
  bool enabled_default_{false};
  bool runtime_enabled_pref_{false};
  bool mocpp_started_{false};
  bool init_pending_{false};
  bool was_connected_{false};
  uint32_t connect_started_ms_{0};
  // Fixed connect timeout: if WS never becomes connected, fail-safe + stop.
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 45000;
  float fail_safe_amps_{6.0f};
  float last_applied_amps_{0.0f};

  bool allow_remote_start_{false};
  bool allow_remote_stop_{false};
  bool allow_reset_{false};
  bool allow_unlock_{false};

  std::string csms_url_;
  std::string charge_point_id_;
  std::string authorization_key_;
  std::string vendor_{"itoaa"};
  std::string model_{"TWC-Director"};
  std::string resolved_url_;
  bool allow_insecure_tls_{false};
  bool allow_cleartext_ws_{false};
  bool crt_bundle_attach_{true};
  std::string ca_cert_;

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  text_sensor::TextSensor *connection_state_sensor_{nullptr};
  sensor::Sensor *last_applied_amps_sensor_{nullptr};

  OcppEnableSwitch *enable_switch_{nullptr};
  OcppParamText *csms_url_text_{nullptr};
  OcppParamText *charge_point_id_text_{nullptr};
  OcppParamText *authorization_key_text_{nullptr};
  OcppFailSafeButton *fail_safe_button_{nullptr};
  OcppFeatureSwitch *remote_start_switch_{nullptr};
  OcppFeatureSwitch *remote_stop_switch_{nullptr};
  OcppFeatureSwitch *reset_switch_{nullptr};
  OcppFeatureSwitch *unlock_switch_{nullptr};

  ESPPreferenceObject pref_enabled_{};
  ESPPreferenceObject pref_flags_{};
  // Fixed-size NVS blobs for runtime URL/id/key overrides (never logged for key).
  static constexpr size_t PREF_URL_LEN = 191;
  static constexpr size_t PREF_ID_LEN = 48;
  static constexpr size_t PREF_KEY_LEN = 64;
  ESPPreferenceObject pref_url_{};
  ESPPreferenceObject pref_id_{};
  ESPPreferenceObject pref_key_{};
  bool has_url_override_{false};
  bool has_id_override_{false};
  bool has_key_override_{false};
};

}  // namespace ocpp_client
}  // namespace esphome
