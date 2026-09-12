#include "esp_idf_ws_connection.h"

#include <esp_websocket_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cstdlib>

namespace twc_ocpp {
namespace {
const char *TAG = "ocpp_ws";

unsigned long now_ms() { return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL); }

// Log host/port/path only — strip userinfo; never log password/auth key.
void log_wss_target(const char *phase, const std::string &url) {
  constexpr const char *kPref = "wss://";
  if (url.rfind(kPref, 0) != 0) {
    ESP_LOGE(TAG, "%s: refusing non-wss (url_len=%u)", phase, static_cast<unsigned>(url.size()));
    return;
  }
  std::string rest = url.substr(6);
  size_t slash = rest.find('/');
  size_t at = rest.find('@');
  if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
    rest = rest.substr(at + 1);
  }
  slash = rest.find('/');
  std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  std::string path = (slash == std::string::npos) ? "/" : rest.substr(slash);
  std::string host = hostport;
  unsigned port = 443;
  size_t colon = hostport.rfind(':');
  if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
    host = hostport.substr(0, colon);
    int p = atoi(hostport.c_str() + colon + 1);
    if (p > 0) {
      port = static_cast<unsigned>(p);
    }
  }
  ESP_LOGI(TAG, "%s: url_len=%u host=%s port=%u path=%s (auth not logged)", phase,
           static_cast<unsigned>(url.size()), host.c_str(), port, path.c_str());
}

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
  log_wss_target("ws-begin-enter", wss_url);
  ESP_LOGI(TAG, "ws-begin: username_set=%d auth_key_set=%d (values not logged)",
           username.empty() ? 0 : 1, auth_key.empty() ? 0 : 1);
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

  ESP_LOGI(TAG, "ws-begin: esp_websocket_client_init...");
  client_ = esp_websocket_client_init(&cfg);
  if (!client_) {
    ESP_LOGE(TAG, "esp_websocket_client_init failed");
    return false;
  }
  esp_websocket_register_events(static_cast<esp_websocket_client_handle_t>(client_), WEBSOCKET_EVENT_ANY,
                                esp_ws_event_handler, this);
  ESP_LOGI(TAG, "ws-begin: esp_websocket_client_start...");
  if (esp_websocket_client_start(static_cast<esp_websocket_client_handle_t>(client_)) != ESP_OK) {
    ESP_LOGE(TAG, "esp_websocket_client_start failed");
    end();
    return false;
  }
  ESP_LOGI(TAG, "ws-begin: start returned OK (async connect; wait for WEBSOCKET_EVENT_CONNECTED)");
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
      ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      connected_ = false;
      ESP_LOGW(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
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
