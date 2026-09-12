#include "esp_idf_ws_connection.h"

#include <esp_websocket_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include <esp_crt_bundle.h>
#endif

namespace twc_ocpp {
namespace {
const char *TAG = "ocpp_ws";

unsigned long now_ms() { return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL); }

bool parse_host_port_path(const std::string &url, std::string *host, unsigned *port, std::string *path) {
  constexpr const char *kPref = "wss://";
  if (url.rfind(kPref, 0) != 0) {
    return false;
  }
  std::string rest = url.substr(6);
  size_t slash = rest.find('/');
  size_t at = rest.find('@');
  if (at != std::string::npos && (slash == std::string::npos || at < slash)) {
    rest = rest.substr(at + 1);
  }
  slash = rest.find('/');
  std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  *path = (slash == std::string::npos) ? "/" : rest.substr(slash);
  *host = hostport;
  *port = 443;
  size_t colon = hostport.rfind(':');
  if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
    *host = hostport.substr(0, colon);
    int p = atoi(hostport.c_str() + colon + 1);
    if (p > 0) {
      *port = static_cast<unsigned>(p);
    }
  }
  return !host->empty();
}

// Log host/port/path only — strip userinfo; never log password/auth key.
void log_wss_target(const char *phase, const std::string &url) {
  std::string host, path;
  unsigned port = 443;
  if (!parse_host_port_path(url, &host, &port, &path)) {
    ESP_LOGE(TAG, "%s: refusing non-wss (url_len=%u)", phase, static_cast<unsigned>(url.size()));
    return;
  }
  ESP_LOGI(TAG, "%s: url_len=%u host=%s port=%u path=%s (auth not logged)", phase,
           static_cast<unsigned>(url.size()), host.c_str(), port, path.c_str());
}

bool is_ipv4_literal(const std::string &host, unsigned *a, unsigned *b, unsigned *c, unsigned *d) {
  unsigned aa = 0, bb = 0, cc = 0, dd = 0;
  char trail = 0;
  if (sscanf(host.c_str(), "%u.%u.%u.%u%c", &aa, &bb, &cc, &dd, &trail) != 4) {
    return false;
  }
  if (aa > 255 || bb > 255 || cc > 255 || dd > 255) {
    return false;
  }
  // Reject leading-zero weirdness / non-decimal by round-trip compare
  char canon[32];
  snprintf(canon, sizeof(canon), "%u.%u.%u.%u", aa, bb, cc, dd);
  if (host != canon) {
    return false;
  }
  *a = aa;
  *b = bb;
  *c = cc;
  *d = dd;
  return true;
}

bool is_rfc1918_octets(unsigned a, unsigned b, unsigned /*c*/, unsigned /*d*/) {
  if (a == 10) {
    return true;
  }
  if (a == 172 && b >= 16 && b <= 31) {
    return true;
  }
  if (a == 192 && b == 168) {
    return true;
  }
  return false;
}

bool is_rfc1918_ipv4_literal(const std::string &host) {
  unsigned a, b, c, d;
  if (!is_ipv4_literal(host, &a, &b, &c, &d)) {
    return false;
  }
  return is_rfc1918_octets(a, b, c, d);
}

bool ends_with_ci(const std::string &s, const char *suffix) {
  const size_t n = std::strlen(suffix);
  if (s.size() < n) {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

bool is_lab_local_hostname(const std::string &host) { return ends_with_ci(host, ".local"); }

bool hostname_resolves_rfc1918(const std::string &host) {
  struct addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = nullptr;
  const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
  if (rc != 0 || res == nullptr) {
    ESP_LOGW(TAG, "insecure-gate: getaddrinfo(%s) failed rc=%d", host.c_str(), rc);
    return false;
  }
  bool ok = false;
  for (struct addrinfo *p = res; p != nullptr; p = p->ai_next) {
    if (p->ai_family != AF_INET || p->ai_addr == nullptr) {
      continue;
    }
    auto *sa = reinterpret_cast<struct sockaddr_in *>(p->ai_addr);
    const uint32_t addr = ntohl(sa->sin_addr.s_addr);
    const unsigned a = (addr >> 24) & 0xffu;
    const unsigned b = (addr >> 16) & 0xffu;
    const unsigned c = (addr >> 8) & 0xffu;
    const unsigned d = addr & 0xffu;
    if (is_rfc1918_octets(a, b, c, d)) {
      ESP_LOGI(TAG, "insecure-gate: %s resolved to %u.%u.%u.%u (RFC1918)", host.c_str(), a, b, c, d);
      ok = true;
      break;
    }
  }
  freeaddrinfo(res);
  return ok;
}

/* CISO: allow_insecure_tls only for RFC1918 literal, .local, or hostname→RFC1918. */
bool insecure_host_allowed(const std::string &host) {
  if (is_rfc1918_ipv4_literal(host)) {
    return true;
  }
  if (is_lab_local_hostname(host)) {
    return true;
  }
  // Reject obvious public IPv4 literals early (no resolve needed).
  unsigned a, b, c, d;
  if (is_ipv4_literal(host, &a, &b, &c, &d)) {
    return false;
  }
  return hostname_resolves_rfc1918(host);
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
                               const std::string &auth_key, const EspIdfWsTlsOptions &tls) {
  if (wss_url.rfind("wss://", 0) != 0) {
    ESP_LOGE(TAG, "Refusing non-wss URL");
    return false;
  }
  log_wss_target("ws-begin-enter", wss_url);
  ESP_LOGI(TAG, "ws-begin: username_set=%d auth_key_set=%d (values not logged)",
           username.empty() ? 0 : 1, auth_key.empty() ? 0 : 1);

  std::string host, path;
  unsigned port = 443;
  if (!parse_host_port_path(wss_url, &host, &port, &path)) {
    ESP_LOGE(TAG, "ws-begin: could not parse host from wss URL");
    return false;
  }

  bool use_insecure = false;
  if (tls.allow_insecure_tls) {
    if (!insecure_host_allowed(host)) {
      ESP_LOGE(TAG,
               "allow_insecure_tls REJECTED for host=%s — not RFC1918 / .local / lab-resolved "
               "(CISO: never skip verify against public IP/DNS/cloud). Keep CA or crt_bundle.",
               host.c_str());
      return false;
    }
    use_insecure = true;
    ESP_LOGW(TAG, "allow_insecure_tls ON for host=%s — TLS server verify skipped (lab PoC only)",
             host.c_str());
  }

  end();
  ca_cert_owned_.clear();

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

  if (use_insecure) {
#if !defined(CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY) || !CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY
    ESP_LOGE(TAG,
             "allow_insecure_tls accepted for host=%s but firmware built without "
             "CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY — rebuild with allow_insecure_tls: true",
             host.c_str());
    return false;
#else
    // Leave cert_pem / crt_bundle unset so esp_tls takes the skip-verify path.
    cfg.cert_pem = nullptr;
    cfg.crt_bundle_attach = nullptr;
    cfg.use_global_ca_store = false;
    cfg.skip_cert_common_name_check = true;
#endif
  } else if (tls.ca_cert_pem != nullptr && tls.ca_cert_pem[0] != '\0') {
    ca_cert_owned_ = tls.ca_cert_pem;
    cfg.cert_pem = ca_cert_owned_.c_str();
    cfg.crt_bundle_attach = nullptr;
    // IP + lab CA often has CN/SAN mismatch; skip CN only (chain still verified).
    unsigned a, b, c, d;
    if (is_ipv4_literal(host, &a, &b, &c, &d)) {
      cfg.skip_cert_common_name_check = true;
      ESP_LOGI(TAG, "ws-begin: ca_cert PEM set; skip_cert_common_name_check for IP host=%s",
               host.c_str());
    } else {
      ESP_LOGI(TAG, "ws-begin: ca_cert PEM set (server verify via custom CA)");
    }
  } else if (tls.crt_bundle_attach) {
#if defined(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) && CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    ESP_LOGI(TAG, "ws-begin: crt_bundle_attach enabled (server verify via CA bundle)");
#else
    ESP_LOGE(TAG, "ws-begin: crt_bundle_attach requested but CONFIG_MBEDTLS_CERTIFICATE_BUNDLE off");
    return false;
#endif
  } else {
    ESP_LOGE(TAG,
             "ws-begin: no TLS verify option (set crt_bundle_attach, ca_cert, or lab-only "
             "allow_insecure_tls) — refusing SSL_SETUP_FAILED");
    return false;
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
  ca_cert_owned_.clear();
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
