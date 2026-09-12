idf_component_register(
  SRCS "ocpp_mocpp_bridge.cpp" "esp_idf_ws_connection.cpp"
  INCLUDE_DIRS "include"
  PRIV_INCLUDE_DIRS "." "../ArduinoJson/src"
  REQUIRES MicroOcpp
  PRIV_REQUIRES espressif__esp_websocket_client mbedtls
)
