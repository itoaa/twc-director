#pragma once

#include <string>
#include <MicroOcpp/Core/Connection.h>

namespace twc_ocpp {

class EspIdfWsConnection : public MicroOcpp::Connection {
 public:
  EspIdfWsConnection() = default;
  ~EspIdfWsConnection() override;

  bool begin(const std::string &wss_url, const std::string &username, const std::string &auth_key);
  void end();

  void loop() override;
  bool sendTXT(const char *msg, size_t length) override;
  void setReceiveTXTcallback(MicroOcpp::ReceiveTXTcallback &receiveTXT) override;
  unsigned long getLastRecv() override { return last_recv_ms_; }
  unsigned long getLastConnected() override { return last_connected_ms_; }
  bool isConnected() override { return connected_; }

  void on_event_(int32_t event_id, void *event_data);

 private:
  void *client_{nullptr};
  MicroOcpp::ReceiveTXTcallback receive_txt_;
  bool connected_{false};
  unsigned long last_recv_ms_{0};
  unsigned long last_connected_ms_{0};
};

}  // namespace twc_ocpp
