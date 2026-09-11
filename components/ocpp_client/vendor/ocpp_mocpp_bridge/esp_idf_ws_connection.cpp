#include "esp_idf_ws_connection.h"

#include <esp_websocket_client.h>
#include <esp_log.h>
#include <esp_timer.h>

namespace twc_ocpp {
namespace {
const char *TAG = "ocpp_ws";

unsigned long now_ms() { return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL); }

void esp_ws_event_handler(void *handler_args, esp_event_base_t, int32_t event_id, void *event_data) {
  auto *self = static_cast<EspIdfWsConnection *>(handler_args);
  if (self) {
    self->on_event_(event_id, event_data);
  }
}
}  // namespace

EspIdfWsConnection::~EspIdfWsConnection() { end(); }

bool EspIdfWsConnection::begin(const std::string &wss_url, const std::string &username,
                               const std::string &auth_key) {
  if (wss_url.rfind("wss://", 0) != 0) {
    ESP_LOGE(TAG, "Refusing non-wss URL");
    return false;
  }
  end();

  esp_websocket_client_config_t cfg = {};
  cfg.uri = wss_url.c_str();
  cfg.transport = WEBSOCKET_TRANSPORT_OVER_SSL;
  cfg.subprotocol = "ocpp1.6";
  if (!username.empty()) {
    cfg.username = username.c_str();
  }
  if (!auth_key.empty()) {
    cfg.password = auth_key.c_str();
  }

  client_ = esp_websocket_client_init(&cfg);
  if (!client_) {
    ESP_LOGE(TAG, "esp_websocket_client_init failed");
    return false;
  }
  esp_websocket_register_events(static_cast<esp_websocket_client_handle_t>(client_), WEBSOCKET_EVENT_ANY,
                                esp_ws_event_handler, this);
  if (esp_websocket_client_start(static_cast<esp_websocket_client_handle_t>(client_)) != ESP_OK) {
    ESP_LOGE(TAG, "esp_websocket_client_start failed");
    end();
    return false;
  }
  return true;
}

void EspIdfWsConnection::end() {
  if (client_) {
    esp_websocket_client_stop(static_cast<esp_websocket_client_handle_t>(client_));
    esp_websocket_client_destroy(static_cast<esp_websocket_client_handle_t>(client_));
    client_ = nullptr;
  }
  connected_ = false;
}

void EspIdfWsConnection::loop() {}

bool EspIdfWsConnection::sendTXT(const char *msg, size_t length) {
  if (!client_ || !connected_) {
    return false;
  }
  return esp_websocket_client_send_text(static_cast<esp_websocket_client_handle_t>(client_), msg,
                                        static_cast<int>(length), pdMS_TO_TICKS(1000)) >= 0;
}

void EspIdfWsConnection::setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback &receiveTXT) {
  receive_txt_ = receiveTXT;
}

void EspIdfWsConnection::on_event_(int32_t event_id, void *event_data) {
  auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      connected_ = true;
      last_connected_ms_ = now_ms();
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      connected_ = false;
      break;
    case WEBSOCKET_EVENT_DATA:
      if (data && data->op_code == WS_TRANSPORT_OPCODES_TEXT && data->data_ptr && data->data_len > 0 &&
          receive_txt_) {
        last_recv_ms_ = now_ms();
        receive_txt_(static_cast<const char *>(data->data_ptr), static_cast<size_t>(data->data_len));
      }
      break;
    default:
      break;
  }
}

}  // namespace twc_ocpp
