# ESPHome external component: OCPP 1.6J client (MicroOCPP) → twc_director global max.
#
# Default OFF. When enabled:false (or omitted from YAML), the C++ component is a
# clean no-op. When enabled:true, MicroOCPP + ArduinoJson IDF components are
# pulled from components/ocpp_client/vendor/ (see scripts/fetch_deps.sh).

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
)

CONF_ENABLED = "enabled"
from esphome.components import binary_sensor, text_sensor, sensor
from esphome.core import CORE

CODEOWNERS = ["@itoaa"]
DEPENDENCIES = ["wifi", "twc_director"]
AUTO_LOAD = ["binary_sensor", "text_sensor", "sensor"]

CONF_CSMS_URL = "csms_url"
CONF_CHARGE_POINT_ID = "charge_point_id"
CONF_AUTHORIZATION_KEY = "authorization_key"
CONF_TWC_DIRECTOR_ID = "twc_director_id"
CONF_FAIL_SAFE_AMPS = "fail_safe_amps"
CONF_CONNECTED = "connected"
CONF_CONNECTION_STATE = "connection_state"
CONF_LAST_APPLIED_AMPS = "last_applied_amps"
CONF_VENDOR = "charge_point_vendor"
CONF_MODEL = "charge_point_model"

ocpp_ns = cg.esphome_ns.namespace("ocpp_client")
OcppClientComponent = ocpp_ns.class_("OcppClientComponent", cg.Component)

# Avoid importing the twc_director Python package (circular / load-order issues).
twc_director_ns = cg.esphome_ns.namespace("twc_director")
TWCDirectorComponent = twc_director_ns.class_("TWCDirectorComponent", cg.Component)


def _validate_wss_url(value):
    value = cv.url(value)
    if not value.lower().startswith("wss://"):
        raise cv.Invalid(
            "csms_url must use wss:// (TLS). Cleartext ws:// is rejected (CISO)."
        )
    return value


def _validate_fail_safe(config):
    # fail_safe_amps is further clamped at runtime to director hard cap.
    if CONF_FAIL_SAFE_AMPS in config and config[CONF_FAIL_SAFE_AMPS] <= 0:
        raise cv.Invalid("fail_safe_amps must be > 0")
    if config.get(CONF_ENABLED, False):
        for key in (CONF_CSMS_URL, CONF_CHARGE_POINT_ID, CONF_AUTHORIZATION_KEY):
            if key not in config:
                raise cv.Invalid(f"'{key}' is required when ocpp_client.enabled is true")
        vendor_dir = Path(__file__).parent / "vendor"
        mocpp = vendor_dir / "MicroOcpp" / "CMakeLists.txt"
        ajson = vendor_dir / "ArduinoJson" / "CMakeLists.txt"
        if not mocpp.is_file() or not ajson.is_file():
            raise cv.Invalid(
                "enabled: true requires MicroOCPP + ArduinoJson under "
                "components/ocpp_client/vendor/. Run: "
                "components/ocpp_client/scripts/fetch_deps.sh"
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppClientComponent),
            cv.Optional(CONF_ENABLED, default=False): cv.boolean,
            cv.Optional(CONF_CSMS_URL): _validate_wss_url,
            cv.Optional(CONF_CHARGE_POINT_ID): cv.string_strict,
            cv.Optional(CONF_AUTHORIZATION_KEY): cv.string_strict,
            cv.Required(CONF_TWC_DIRECTOR_ID): cv.use_id(TWCDirectorComponent),
            cv.Optional(CONF_FAIL_SAFE_AMPS, default=6.0): cv.float_range(min=1.0, max=80.0),
            cv.Optional(CONF_VENDOR, default="itoaa"): cv.string_strict,
            cv.Optional(CONF_MODEL, default="TWC-Director"): cv.string_strict,
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ),
            cv.Optional(CONF_CONNECTION_STATE): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_LAST_APPLIED_AMPS): sensor.sensor_schema(
                unit_of_measurement="A",
                accuracy_decimals=1,
                device_class="current",
                state_class="measurement",
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_fail_safe,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    director = await cg.get_variable(config[CONF_TWC_DIRECTOR_ID])
    cg.add(var.set_director(director))
    cg.add(var.set_enabled(config[CONF_ENABLED]))
    cg.add(var.set_fail_safe_amps(config[CONF_FAIL_SAFE_AMPS]))
    cg.add(var.set_charge_point_vendor(config[CONF_VENDOR]))
    cg.add(var.set_charge_point_model(config[CONF_MODEL]))

    if CONF_CSMS_URL in config:
        cg.add(var.set_csms_url(config[CONF_CSMS_URL]))
    if CONF_CHARGE_POINT_ID in config:
        cg.add(var.set_charge_point_id(config[CONF_CHARGE_POINT_ID]))
    if CONF_AUTHORIZATION_KEY in config:
        cg.add(var.set_authorization_key(config[CONF_AUTHORIZATION_KEY]))

    if CONF_CONNECTED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(var.set_connected_sensor(sens))
    if CONF_CONNECTION_STATE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CONNECTION_STATE])
        cg.add(var.set_connection_state_sensor(sens))
    if CONF_LAST_APPLIED_AMPS in config:
        sens = await sensor.new_sensor(config[CONF_LAST_APPLIED_AMPS])
        cg.add(var.set_last_applied_amps_sensor(sens))

    if config[CONF_ENABLED]:
        if not CORE.is_esp32:
            raise cv.Invalid("ocpp_client enabled:true requires ESP32 with ESP-IDF framework")
        from esphome.components.esp32 import add_idf_component, include_builtin_idf_component

        vendor = Path(__file__).parent / "vendor"
        # Sibling layout required by MicroOCPP's ESP-IDF CMakeLists
        # (INCLUDE_DIRS "../ArduinoJson/src").
        # Sibling MicroOcpp + ArduinoJson (AJ is PRIV include only — see fetch_deps.sh).
        add_idf_component(name="MicroOcpp", path=str(vendor / "MicroOcpp"))
        # C bridge isolates MicroOcpp/ArduinoJson from ESPHome's JSON stack.
        add_idf_component(name="ocpp_mocpp_bridge", path=str(vendor / "ocpp_mocpp_bridge"))
        # ESP-IDF 5.x WebSocket client (managed) for wss:// CSMS link.
        add_idf_component(name="espressif/esp_websocket_client", ref="1.4.0")
        # MicroOCPP PRIV_REQUIRES spiffs even with FilesystemOpt::Deactivate.
        include_builtin_idf_component("spiffs")
        cg.add_define("USE_MICROOCPP")
        cg.add_build_flag("-DMO_PLATFORM=MO_PLATFORM_ESPIDF")
        cg.add_build_flag("-DMO_ENABLE_CONNECTOR_LOCK=0")
