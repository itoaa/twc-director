#pragma once

#include <string>
#include <MicroOcpp/Core/Connection.h>

namespace twc_ocpp {

struct EspIdfWsTlsOptions {
  bool allow_insecure_tls{false}; /* lab-only; gated to RFC1918 / .local at begin() */
  bool crt_bundle_attach{true};   /* Mozilla CA bundle (default verify path) */
  const char *ca_cert_pem{nullptr}; /* optional PEM CA; overrides bundle when set */
};

class EspIdfWsConnection : public MicroOcpp::Connection {
 public:
  EspIdfWsConnection() = default;
  ~EspIdfWsConnection() override;

  bool begin(const std::string &wss_url, const std::string &username, const std::string &auth_key,
             const EspIdfWsTlsOptions &tls = {});
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
  std::string ca_cert_owned_; /* keeps cert_pem alive for websocket client lifetime */
};

}  // namespace twc_ocpp
