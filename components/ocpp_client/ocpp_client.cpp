#include "ocpp_client.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>

#ifdef USE_MICROOCPP
#include "ocpp_mocpp_bridge.h"
#endif

namespace esphome {
namespace ocpp_client {

static const char *const TAG = "ocpp_client";

namespace {
struct FlagBlob {
  uint8_t remote_start;
  uint8_t remote_stop;
  uint8_t reset;
  uint8_t unlock;
};
struct PrefString {
  uint8_t len;
  char data[192];  // URL can exceed 160 (Ola live ~161); id/key use same blob
};
}  // namespace

std::string OcppClientComponent::redact_wss_url_(const std::string &url) {
  // Strip userinfo (never log auth). Keep scheme + host[:port] + path.
  constexpr const char *kPref = "wss://";
  if (url.rfind(kPref, 0) != 0) {
    return "<non-wss>";
  }
  std::string rest = url.substr(strlen(kPref));
  size_t slash = rest.find('/');
  size_t at = rest.find('@');
  if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
    rest = rest.substr(at + 1);
  }
  return std::string(kPref) + rest;
}

void OcppClientComponent::log_wss_target_(const char *phase, const std::string &url) {
  const std::string redacted = redact_wss_url_(url);
  constexpr const char *kPref = "wss://";
  std::string host = "?";
  std::string path = "/";
  unsigned port = 443;
  if (redacted.rfind(kPref, 0) == 0) {
    std::string rest = redacted.substr(strlen(kPref));
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    // host:port (skip naive IPv6 for log; still safe — no secrets)
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
      host = hostport.substr(0, colon);
      port = static_cast<unsigned>(atoi(hostport.c_str() + colon + 1));
      if (port == 0) {
        port = 443;
      }
    } else {
      host = hostport;
    }
  }
  ESP_LOGI(TAG, "%s: url_len=%u redacted=%s host=%s port=%u path=%s", phase,
           static_cast<unsigned>(url.size()), redacted.c_str(), host.c_str(), port, path.c_str());
}


#ifdef USE_MICROOCPP
static void smart_current_thunk(float amps, void *user) {
  auto *self = static_cast<OcppClientComponent *>(user);
  if (self != nullptr) {
    self->on_smart_current_(amps);
  }
}
static void connector_current_thunk(unsigned connector_id, float amps, void *user) {
  auto *self = static_cast<OcppClientComponent *>(user);
  if (self != nullptr) {
    self->on_connector_current_(connector_id, amps);
  }
}
static void change_config_thunk(const char *key, const char *value, void *user) {
  auto *self = static_cast<OcppClientComponent *>(user);
  if (self != nullptr) {
    self->on_change_config_(key, value);
  }
}
static void remote_event_thunk(const char *action, unsigned connector_id, bool accepted, const char *detail,
                               void *user) {
  auto *self = static_cast<OcppClientComponent *>(user);
  if (self != nullptr) {
    self->on_remote_event_(action, connector_id, accepted, detail);
  }
}
#endif

void OcppEnableSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->handle_enable_write_(state);
  }
  this->publish_state(state);
}

void OcppFeatureSwitch::write_state(bool state) {
  if (this->parent_ != nullptr) {
    this->parent_->handle_feature_write_(this->kind_, state);
  }
  this->publish_state(state);
}

void OcppParamText::control(const std::string &value) {
  if (this->parent_ != nullptr) {
    this->parent_->handle_param_write_(this->kind_, value);
  }
  // Never publish the raw authorization key into HA state.
  if (this->kind_ == AUTH_KEY) {
    this->publish_state(value.empty() ? "" : "********");
  } else {
    this->publish_state(value);
  }
}

void OcppFailSafeButton::press_action() {
  if (this->parent_ != nullptr) {
    this->parent_->handle_fail_safe_press_();
  }
}

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

void OcppClientComponent::on_connector_current_(unsigned connector_id, float amps) {
  if (this->director_ == nullptr) {
    return;
  }
  if (amps < 0.0f) {
    return;
  }
  float applied = this->director_->apply_external_connector_max_a(connector_id, amps);
  ESP_LOGI(TAG, "Connector %u smart current applied %.1fA", connector_id, applied);
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
    return;
  }
  // TwcConnectorNMaxCurrent → per-slot (N = 1..4)
  if (!strncmp(key, "TwcConnector", 12) && strstr(key, "MaxCurrent") != nullptr) {
    unsigned n = static_cast<unsigned>(atoi(key + 12));
    float amps = atof(value);
    if (n >= 1 && amps >= 0.0f) {
      this->on_connector_current_(n, amps);
    }
  }
}

void OcppClientComponent::on_remote_event_(const char *action, unsigned connector_id, bool accepted,
                                          const char *detail) {
  ESP_LOGW(TAG, "Remote event action=%s connector=%u accepted=%d detail=%s",
           action != nullptr ? action : "?", connector_id, accepted ? 1 : 0,
           detail != nullptr ? detail : "");
  if (!accepted || action == nullptr) {
    return;
  }
  if (!strcmp(action, "RemoteStop") || !strcmp(action, "Reset")) {
    this->apply_fail_safe_(action);
  }
}

bool OcppClientComponent::runtime_enabled_() const {
  if (this->enable_switch_ != nullptr) {
    return this->enable_switch_->state;
  }
  return this->runtime_enabled_pref_;
}

std::string OcppClientComponent::effective_url_() const { return this->csms_url_; }

void OcppClientComponent::load_runtime_prefs_() {
  this->pref_enabled_ = global_preferences->make_preference<bool>(fnv1_hash("ocpp_en"));
  this->pref_flags_ = global_preferences->make_preference<FlagBlob>(fnv1_hash("ocpp_fl"));
  this->pref_url_ = global_preferences->make_preference<PrefString>(fnv1_hash("ocpp_url"));
  this->pref_id_ = global_preferences->make_preference<PrefString>(fnv1_hash("ocpp_id"));
  this->pref_key_ = global_preferences->make_preference<PrefString>(fnv1_hash("ocpp_key"));

  bool en = this->enabled_default_;
  if (this->pref_enabled_.load(&en)) {
    this->runtime_enabled_pref_ = en;
  } else {
    this->runtime_enabled_pref_ = this->enabled_default_;
  }

  FlagBlob flags{};
  if (this->pref_flags_.load(&flags)) {
    this->allow_remote_start_ = flags.remote_start != 0;
    this->allow_remote_stop_ = flags.remote_stop != 0;
    this->allow_reset_ = flags.reset != 0;
    this->allow_unlock_ = flags.unlock != 0;
  }

  PrefString ps{};
  if (this->pref_url_.load(&ps) && ps.len > 0 && ps.len < sizeof(ps.data)) {
    this->csms_url_.assign(ps.data, ps.len);
    this->has_url_override_ = true;
  }
  if (this->pref_id_.load(&ps) && ps.len > 0 && ps.len < sizeof(ps.data)) {
    this->charge_point_id_.assign(ps.data, ps.len);
    this->has_id_override_ = true;
  }
  if (this->pref_key_.load(&ps) && ps.len > 0 && ps.len < sizeof(ps.data)) {
    this->authorization_key_.assign(ps.data, ps.len);
    this->has_key_override_ = true;
  }
}

void OcppClientComponent::save_runtime_prefs_() {
  bool en = this->runtime_enabled_();
  this->pref_enabled_.save(&en);
  FlagBlob flags{};
  flags.remote_start = this->allow_remote_start_ ? 1 : 0;
  flags.remote_stop = this->allow_remote_stop_ ? 1 : 0;
  flags.reset = this->allow_reset_ ? 1 : 0;
  flags.unlock = this->allow_unlock_ ? 1 : 0;
  this->pref_flags_.save(&flags);

  auto save_str = [](ESPPreferenceObject &pref, const std::string &s) {
    PrefString ps{};
    size_t n = std::min(s.size(), sizeof(ps.data) - 1);
    ps.len = static_cast<uint8_t>(n);
    if (n > 0) {
      memcpy(ps.data, s.c_str(), n);
    }
    ps.data[n] = '\0';
    pref.save(&ps);
  };
  if (this->has_url_override_) {
    save_str(this->pref_url_, this->csms_url_);
  }
  if (this->has_id_override_) {
    save_str(this->pref_id_, this->charge_point_id_);
  }
  if (this->has_key_override_) {
    save_str(this->pref_key_, this->authorization_key_);
  }
  global_preferences->sync();
}

void OcppClientComponent::push_feature_flags_() {
#ifdef USE_MICROOCPP
  if (!this->mocpp_started_) {
    return;
  }
  twc_ocpp_feature_flags_t flags{};
  flags.allow_remote_start = this->allow_remote_start_;
  flags.allow_remote_stop = this->allow_remote_stop_;
  flags.allow_reset = this->allow_reset_;
  flags.allow_unlock = this->allow_unlock_;
  twc_ocpp_mocpp_set_feature_flags(&flags);
#endif
}

void OcppClientComponent::push_telemetry_() {
#ifdef USE_MICROOCPP
  if (!this->mocpp_started_ || this->director_ == nullptr) {
    return;
  }
  twc_ocpp_telemetry_t t{};
  auto site = this->director_->get_site_telemetry();
  t.current_a = site.max_phase_current_a;
  t.voltage_v = site.voltage_v;
  t.energy_wh = site.total_energy_kwh * 1000.0f;
  t.power_w = site.approx_power_w;
  t.plugged = site.any_vehicle_connected;
  t.occupied = site.any_vehicle_connected || site.any_charging;
  t.ev_ready = site.any_charging;
  t.evse_ready = site.any_online;
  t.online_count = site.online_count;

  const size_t slots = std::min(this->director_->slot_count(), static_cast<size_t>(TWC_OCPP_MAX_CONNECTORS));
  t.num_connectors = static_cast<unsigned>(slots > 0 ? slots : 1);
  for (size_t i = 0; i < slots; i++) {
    auto slot = this->director_->get_slot_telemetry(i);
    t.conn_valid[i] = slot.valid;
    if (!slot.valid) {
      continue;
    }
    float imax = std::max(slot.current_a[0], std::max(slot.current_a[1], slot.current_a[2]));
    float v = slot.voltage_v[0] > 0 ? slot.voltage_v[0]
                                    : (slot.voltage_v[1] > 0 ? slot.voltage_v[1] : slot.voltage_v[2]);
    t.conn_current_a[i] = imax;
    t.conn_voltage_v[i] = v;
    t.conn_energy_wh[i] = slot.total_energy_kwh * 1000.0f;
    t.conn_power_w[i] = (v > 0.0f) ? v * imax : 0.0f;
    t.conn_plugged[i] = slot.vehicle_connected;
    t.conn_occupied[i] = slot.vehicle_connected || slot.charging;
    t.conn_ev_ready[i] = slot.charging;
    t.conn_evse_ready[i] = slot.online;
  }
  // Aggregate fallback when no bound slots yet.
  if (slots == 0) {
    t.conn_valid[0] = true;
    t.conn_current_a[0] = t.current_a;
    t.conn_voltage_v[0] = t.voltage_v;
    t.conn_energy_wh[0] = t.energy_wh;
    t.conn_power_w[0] = t.power_w;
    t.conn_plugged[0] = t.plugged;
    t.conn_occupied[0] = t.occupied;
    t.conn_ev_ready[0] = t.ev_ready;
    t.conn_evse_ready[0] = t.evse_ready;
  }
  twc_ocpp_mocpp_set_telemetry(&t);
#endif
}

void OcppClientComponent::stop_microocpp_(const char *reason) {
  this->init_pending_ = false;
  this->connect_started_ms_ = 0;
#ifdef USE_MICROOCPP
  if (!this->mocpp_started_) {
    return;
  }
  ESP_LOGW(TAG, "Stopping MicroOCPP — %s", reason != nullptr ? reason : "");
  twc_ocpp_mocpp_stop();
  this->mocpp_started_ = false;
  this->was_connected_ = false;
  this->publish_state_(false, "stopped");
#else
  (void) reason;
#endif
}

void OcppClientComponent::request_microocpp_start_(const char *reason) {
  ESP_LOGI(TAG, "OCPP enable path start (%s) url_len=%u cp_id_len=%u auth_key_set=%d",
           reason != nullptr ? reason : "?", static_cast<unsigned>(this->csms_url_.size()),
           static_cast<unsigned>(this->charge_point_id_.size()),
           this->authorization_key_.empty() ? 0 : 1);
  this->log_wss_target_("enable/resolve-input", this->csms_url_);
  // Publish connecting *before* heavy WS/MO work so HA is not stuck on "starting".
  // Actual mocpp_start runs on the next loop() tick (deferred) so this state can flush.
  this->init_pending_ = true;
  this->connect_started_ms_ = 0;
  this->publish_state_(false, "connecting");
}

void OcppClientComponent::maybe_init_microocpp_() {
#ifdef USE_MICROOCPP
  if (this->mocpp_started_) {
    this->init_pending_ = false;
    return;
  }
  if (!this->runtime_enabled_()) {
    this->init_pending_ = false;
    return;
  }

  if (this->csms_url_.rfind("wss://", 0) != 0) {
    ESP_LOGE(TAG, "csms_url must be wss:// — not starting");
    this->init_pending_ = false;
    this->publish_state_(false, "error:not-wss");
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

  this->log_wss_target_("resolved-wss", this->resolved_url_);

  unsigned num_connectors = 1;
  if (this->director_ != nullptr) {
    size_t slots = this->director_->slot_count();
    if (slots == 0) {
      slots = 1;
    }
    if (slots > TWC_OCPP_MAX_CONNECTORS) {
      slots = TWC_OCPP_MAX_CONNECTORS;
    }
    num_connectors = static_cast<unsigned>(slots);
  }

  twc_ocpp_mocpp_config_t cfg = {};
  cfg.wss_url = this->resolved_url_.c_str();
  cfg.charge_point_id = this->charge_point_id_.c_str();
  cfg.authorization_key = this->authorization_key_.c_str();
  cfg.vendor = this->vendor_.c_str();
  cfg.model = this->model_.c_str();
  cfg.num_connectors = num_connectors;
  cfg.allow_insecure_tls = this->allow_insecure_tls_;
  cfg.crt_bundle_attach = this->crt_bundle_attach_;
  cfg.ca_cert_pem = this->ca_cert_.empty() ? nullptr : this->ca_cert_.c_str();
  if (this->allow_insecure_tls_) {
    ESP_LOGW(TAG, "allow_insecure_tls enabled in config (CISO: only RFC1918/.local lab hosts)");
  }
  cfg.flags.allow_remote_start = this->allow_remote_start_;
  cfg.flags.allow_remote_stop = this->allow_remote_stop_;
  cfg.flags.allow_reset = this->allow_reset_;
  cfg.flags.allow_unlock = this->allow_unlock_;
  cfg.on_smart_current = smart_current_thunk;
  cfg.on_connector_current = connector_current_thunk;
  cfg.on_change_config = change_config_thunk;
  cfg.on_remote_event = remote_event_thunk;
  cfg.user = this;

  ESP_LOGI(TAG, "Calling twc_ocpp_mocpp_start (connectors=%u) — may block on WS/TLS begin",
           num_connectors);
  const uint32_t t0 = millis();
  if (!twc_ocpp_mocpp_start(&cfg)) {
    ESP_LOGE(TAG, "twc_ocpp_mocpp_start failed after %ums", static_cast<unsigned>(millis() - t0));
    this->init_pending_ = false;
    if (this->allow_insecure_tls_) {
      this->publish_state_(false, "error:tls-insecure-rejected-or-ws");
    } else {
      this->publish_state_(false, "error:ws-init");
    }
    this->apply_fail_safe_("mocpp/ws init failed");
    return;
  }
  ESP_LOGI(TAG, "twc_ocpp_mocpp_start returned ok in %ums", static_cast<unsigned>(millis() - t0));

  this->mocpp_started_ = true;
  this->init_pending_ = false;
  this->connect_started_ms_ = millis();
  // Stay on "connecting" until poll sees WS connected (or timeout).
  this->publish_state_(false, "connecting");
  this->push_telemetry_();
  ESP_LOGI(TAG, "MicroOCPP bridge initialized (connectors=%u); waiting for WS connect (timeout=%ums)",
           num_connectors, static_cast<unsigned>(CONNECT_TIMEOUT_MS));
#else
  ESP_LOGE(TAG, "enabled but firmware built without USE_MICROOCPP");
  this->init_pending_ = false;
  this->publish_state_(false, "error:no-microocpp");
  this->apply_fail_safe_("MicroOCPP not linked");
#endif
}

void OcppClientComponent::poll_connection_() {
#ifdef USE_MICROOCPP
  bool connected = twc_ocpp_mocpp_is_connected();
  if (connected != this->was_connected_) {
    if (connected) {
      this->connect_started_ms_ = 0;
      this->publish_state_(true, "connected");
      ESP_LOGI(TAG, "CSMS WebSocket connected");
    } else {
      this->publish_state_(false, "disconnected");
      this->apply_fail_safe_("CSMS disconnected");
    }
    this->was_connected_ = connected;
  }
#endif
}

void OcppClientComponent::check_connect_timeout_() {
#ifdef USE_MICROOCPP
  if (!this->mocpp_started_ || this->was_connected_) {
    return;
  }
  if (this->connect_started_ms_ == 0) {
    return;
  }
  const uint32_t elapsed = millis() - this->connect_started_ms_;
  if (elapsed < CONNECT_TIMEOUT_MS) {
    return;
  }
  ESP_LOGE(TAG, "CSMS connect timeout after %ums — fail-safe and stop (no auto-retry)",
           static_cast<unsigned>(elapsed));
  this->publish_state_(false, "error:connect-timeout");
  this->apply_fail_safe_("CSMS connect timeout");
  this->stop_microocpp_("connect timeout");
  // Leave enable switch ON so Ola can see the error; toggle OFF/ON to retry.
  this->publish_state_(false, "error:connect-timeout");
#endif
}

void OcppClientComponent::handle_enable_write_(bool state) {
  this->runtime_enabled_pref_ = state;
  this->save_runtime_prefs_();
  if (!state) {
    this->apply_fail_safe_("OCPP disabled via HA");
    this->stop_microocpp_("HA enable off");
    this->publish_state_(false, "disabled");
  } else {
    // Same logging + deferred init path as cold setup (do not block HA write_state).
    this->request_microocpp_start_("HA enable ON");
  }
}

void OcppClientComponent::handle_feature_write_(OcppFeatureSwitch::Kind kind, bool state) {
  switch (kind) {
    case OcppFeatureSwitch::REMOTE_START:
      this->allow_remote_start_ = state;
      break;
    case OcppFeatureSwitch::REMOTE_STOP:
      this->allow_remote_stop_ = state;
      break;
    case OcppFeatureSwitch::RESET:
      this->allow_reset_ = state;
      break;
    case OcppFeatureSwitch::UNLOCK:
      this->allow_unlock_ = state;
      break;
  }
  ESP_LOGW(TAG, "Lab feature flag kind=%d -> %d (DEFAULT off; cloud CSMS needs new risk accept)",
           static_cast<int>(kind), state ? 1 : 0);
  this->save_runtime_prefs_();
  this->push_feature_flags_();
}

void OcppClientComponent::handle_param_write_(OcppParamText::Kind kind, const std::string &value) {
  switch (kind) {
    case OcppParamText::CSMS_URL:
      if (value.rfind("wss://", 0) != 0) {
        ESP_LOGE(TAG, "Rejecting non-wss CSMS URL from HA");
        if (this->csms_url_text_ != nullptr) {
          this->csms_url_text_->publish_state(this->csms_url_);
        }
        return;
      }
      this->csms_url_ = value;
      this->has_url_override_ = true;
      break;
    case OcppParamText::CHARGE_POINT_ID:
      this->charge_point_id_ = value;
      this->has_id_override_ = true;
      break;
    case OcppParamText::AUTH_KEY:
      if (value.empty() || value == "********") {
        ESP_LOGW(TAG, "Ignoring empty/placeholder auth key write");
        return;
      }
      this->authorization_key_ = value;
      this->has_key_override_ = true;
      ESP_LOGI(TAG, "Authorization key updated via HA (value not logged)");
      break;
  }
  this->save_runtime_prefs_();
  if (this->runtime_enabled_()) {
    this->stop_microocpp_("CSMS params changed");
    this->request_microocpp_start_("CSMS params changed");
  }
}

void OcppClientComponent::handle_fail_safe_press_() {
  this->apply_fail_safe_("manual HA button");
}

void OcppClientComponent::setup() {
  this->load_runtime_prefs_();

  if (this->director_ == nullptr) {
    ESP_LOGE(TAG, "twc_director_id missing — OCPP inactive");
    this->publish_state_(false, "error:no-director");
    return;
  }

  float cap = this->director_->hard_cap_global_max_a();
  if (cap > 0.0f && this->fail_safe_amps_ > cap) {
    ESP_LOGW(TAG, "fail_safe_amps %.1fA > hard cap %.1fA — clamping", this->fail_safe_amps_, cap);
    this->fail_safe_amps_ = cap;
  }

  // Seed HA entities from effective values (YAML secrets or NVS overrides).
  if (this->enable_switch_ != nullptr) {
    this->enable_switch_->publish_state(this->runtime_enabled_pref_);
  }
  if (this->csms_url_text_ != nullptr) {
    this->csms_url_text_->publish_state(this->csms_url_);
  }
  if (this->charge_point_id_text_ != nullptr) {
    this->charge_point_id_text_->publish_state(this->charge_point_id_);
  }
  if (this->authorization_key_text_ != nullptr) {
    // Do not publish real secret into HA state if empty; show placeholder when set.
    this->authorization_key_text_->publish_state(this->authorization_key_.empty() ? "" : "********");
  }
  if (this->remote_start_switch_ != nullptr) {
    this->remote_start_switch_->publish_state(this->allow_remote_start_);
  }
  if (this->remote_stop_switch_ != nullptr) {
    this->remote_stop_switch_->publish_state(this->allow_remote_stop_);
  }
  if (this->reset_switch_ != nullptr) {
    this->reset_switch_->publish_state(this->allow_reset_);
  }
  if (this->unlock_switch_ != nullptr) {
    this->unlock_switch_->publish_state(this->allow_unlock_);
  }

  if (!this->runtime_enabled_()) {
    ESP_LOGI(TAG, "OCPP client disabled (YAML default and/or HA switch). No CSMS connection.");
    this->publish_state_(false, "disabled");
    return;
  }

  this->request_microocpp_start_("cold setup");
}

void OcppClientComponent::loop() {
  if (!this->runtime_enabled_()) {
    return;
  }
  // Deferred init: run heavy WS/MO start outside switch/setup so UI can leave "starting".
  if (this->init_pending_ && !this->mocpp_started_) {
    this->maybe_init_microocpp_();
  }
#ifdef USE_MICROOCPP
  if (this->mocpp_started_) {
    this->push_telemetry_();
    twc_ocpp_mocpp_loop();
    this->poll_connection_();
    this->check_connect_timeout_();
  }
#endif
}

}  // namespace ocpp_client
}  // namespace esphome
