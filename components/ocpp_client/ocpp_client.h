#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/twc_director/twc_director_component.h"

#ifdef USE_MICROOCPP
#include <MicroOcpp/Core/Connection.h>
#endif

namespace esphome {
namespace ocpp_client {

#ifdef USE_MICROOCPP
// Thin WebSocket adapter for MicroOCPP on ESP-IDF (esp_websocket_client + wss).
class EspIdfWsConnection : public MicroOcpp::Connection {
 public:
  EspIdfWsConnection() = default;
  ~EspIdfWsConnection() override;

  bool begin(const std::string &wss_url, const std::string &username, const std::string &auth_key);
  void end();

  void loop() override;
  bool sendTXT(const char *msg, size_t length) override;
  void setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback &receiveTXT) override;
  unsigned long getLastRecv() override { return this->last_recv_ms_; }
  unsigned long getLastConnected() override { return this->last_connected_ms_; }
  bool isConnected() override { return this->connected_; }

  // Called from the C event handler.
  void on_event_(int32_t event_id, void *event_data);

 private:
  void *client_{nullptr};  // esp_websocket_client_handle_t
  MicroOcpp::ReceiveTXTcallback receive_txt_;
  bool connected_{false};
  unsigned long last_recv_ms_{0};
  unsigned long last_connected_ms_{0};
};
#endif

class OcppClientComponent : public Component {
 public:
  void set_director(twc_director::TWCDirectorComponent *director) { this->director_ = director; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }
  void set_csms_url(const std::string &url) { this->csms_url_ = url; }
  void set_charge_point_id(const std::string &id) { this->charge_point_id_ = id; }
  void set_authorization_key(const std::string &key) { this->authorization_key_ = key; }
  void set_fail_safe_amps(float amps) { this->fail_safe_amps_ = amps; }
  void set_charge_point_vendor(const std::string &v) { this->vendor_ = v; }
  void set_charge_point_model(const std::string &m) { this->model_ = m; }

  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_connection_state_sensor(text_sensor::TextSensor *s) { this->connection_state_sensor_ = s; }
  void set_last_applied_amps_sensor(sensor::Sensor *s) { this->last_applied_amps_sensor_ = s; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

 protected:
  float apply_csms_global_max_(float amps);
  void publish_state_(bool connected, const char *state);
  void apply_fail_safe_(const char *reason);
  void maybe_init_microocpp_();
  void poll_connection_();

  twc_director::TWCDirectorComponent *director_{nullptr};
  bool enabled_{false};
  bool mocpp_started_{false};
  bool was_connected_{false};
  float fail_safe_amps_{6.0f};
  float last_applied_amps_{0.0f};

  std::string csms_url_;
  std::string charge_point_id_;
  std::string authorization_key_;
  std::string vendor_{"itoaa"};
  std::string model_{"TWC-Director"};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  text_sensor::TextSensor *connection_state_sensor_{nullptr};
  sensor::Sensor *last_applied_amps_sensor_{nullptr};

#ifdef USE_MICROOCPP
  EspIdfWsConnection ws_;
#endif
};

}  // namespace ocpp_client
}  // namespace esphome
