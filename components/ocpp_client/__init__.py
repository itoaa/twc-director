# ESPHome external component: OCPP 1.6J client (MicroOCPP) → twc_director.
#
# Default OFF. Runtime HA switch can start/stop CSMS without rebuild.
# When compile-time enabled path is used (USE_MICROOCPP), vendor deps required.

from pathlib import Path
import logging
import subprocess

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_CONFIG,
)
from esphome.components import binary_sensor, text_sensor, sensor, switch, text, button
from esphome.core import CORE

CODEOWNERS = ["@itoaa"]
DEPENDENCIES = ["wifi", "twc_director"]
AUTO_LOAD = ["binary_sensor", "text_sensor", "sensor", "switch", "text", "button"]

CONF_ENABLED = "enabled"
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
CONF_ENABLE_SWITCH = "enable_switch"
CONF_CSMS_URL_TEXT = "csms_url_text"
CONF_CHARGE_POINT_ID_TEXT = "charge_point_id_text"
CONF_AUTHORIZATION_KEY_TEXT = "authorization_key_text"
CONF_CA_CERT_TEXT = "ca_cert_text"
CONF_FAIL_SAFE_BUTTON = "fail_safe_button"
CONF_ALLOW_REMOTE_START = "allow_remote_start"
CONF_ALLOW_REMOTE_STOP = "allow_remote_stop"
CONF_ALLOW_RESET = "allow_reset"
CONF_ALLOW_UNLOCK = "allow_unlock"
CONF_ALLOW_INSECURE_TLS = "allow_insecure_tls"
CONF_ALLOW_CLEARTEXT_WS = "allow_cleartext_ws"
CONF_CA_CERT = "ca_cert"
CONF_CRT_BUNDLE_ATTACH = "crt_bundle_attach"

ocpp_ns = cg.esphome_ns.namespace("ocpp_client")
OcppClientComponent = ocpp_ns.class_("OcppClientComponent", cg.Component)
OcppEnableSwitch = ocpp_ns.class_("OcppEnableSwitch", switch.Switch)
OcppFeatureSwitch = ocpp_ns.class_("OcppFeatureSwitch", switch.Switch)
OcppParamText = ocpp_ns.class_("OcppParamText", text.Text)
OcppFailSafeButton = ocpp_ns.class_("OcppFailSafeButton", button.Button)

twc_director_ns = cg.esphome_ns.namespace("twc_director")
TWCDirectorComponent = twc_director_ns.class_("TWCDirectorComponent", cg.Component)



_LOGGER = logging.getLogger(__name__)


def _vendor_cmake_paths():
    vendor_dir = Path(__file__).parent / "vendor"
    return (
        vendor_dir / "MicroOcpp" / "CMakeLists.txt",
        vendor_dir / "ArduinoJson" / "CMakeLists.txt",
    )


def _patch_mocpp_priv_includes(mocpp_cmake: Path) -> None:
    """Make ArduinoJson a PRIV include so it does not override ESPHome JSON."""
    if not mocpp_cmake.is_file():
        return
    import re

    text = mocpp_cmake.read_text(encoding="utf-8")
    if "PRIV_INCLUDE_DIRS" in text and "ArduinoJson" in text:
        return
    pat = re.compile(
        r'idf_component_register\(SRCS \$\{MO_SRC\}\s+'
        r'INCLUDE_DIRS "\./src" "\.\./ArduinoJson/src"\s+'
        r'PRIV_REQUIRES spiffs\s*\)',
        re.M,
    )
    new = (
        "idf_component_register(SRCS ${MO_SRC}\n"
        '            INCLUDE_DIRS "./src"\n'
        '            PRIV_INCLUDE_DIRS "../ArduinoJson/src"\n'
        "            PRIV_REQUIRES spiffs\n"
        "            )"
    )
    text2, n = pat.subn(new, text, count=1)
    if n:
        mocpp_cmake.write_text(text2, encoding="utf-8")
        _LOGGER.info(
            "Patched MicroOcpp CMakeLists: ArduinoJson → PRIV_INCLUDE_DIRS"
        )
    else:
        _LOGGER.warning(
            "MicroOcpp CMakeLists format unexpected; ArduinoJson may leak includes"
        )


def _ensure_vendor_deps() -> None:
    """Populate vendor/MicroOcpp + ArduinoJson when missing (HA / non-recursive clone).

    ESPHome external_components git clone does not always init submodules.
    Prefer git submodules when present; otherwise scripts/fetch_deps.sh clones
    the pinned tags and applies the CMake PRIV_INCLUDE_DIRS patch.
    """
    mocpp, ajson = _vendor_cmake_paths()
    if mocpp.is_file() and ajson.is_file():
        _patch_mocpp_priv_includes(mocpp)
        return

    script = Path(__file__).parent / "scripts" / "fetch_deps.sh"
    _LOGGER.warning(
        "ocpp_client.enabled: true but vendor MicroOCPP/ArduinoJson CMakeLists "
        "missing (submodules not initialized?). Running %s …",
        script,
    )
    if not script.is_file():
        raise cv.Invalid(
            "enabled: true requires MicroOCPP + ArduinoJson under "
            "components/ocpp_client/vendor/, and fetch_deps.sh is missing. "
            "Use external_components git source with submodules, or run: "
            "components/ocpp_client/scripts/fetch_deps.sh"
        )
    try:
        subprocess.run(
            ["bash", str(script)],
            check=True,
            cwd=str(script.parent),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=300,
        )
    except subprocess.CalledProcessError as err:
        out = (err.stdout or "").strip()
        _LOGGER.error("fetch_deps.sh failed:\n%s", out)
        raise cv.Invalid(
            "enabled: true could not fetch MicroOCPP + ArduinoJson. "
            "Check network / git, or run: components/ocpp_client/scripts/fetch_deps.sh"
        ) from err
    except (OSError, subprocess.TimeoutExpired) as err:
        raise cv.Invalid(
            f"enabled: true failed to run fetch_deps.sh ({err}). "
            "Run: components/ocpp_client/scripts/fetch_deps.sh"
        ) from err

    mocpp, ajson = _vendor_cmake_paths()
    if not mocpp.is_file() or not ajson.is_file():
        raise cv.Invalid(
            "enabled: true requires MicroOCPP + ArduinoJson under "
            "components/ocpp_client/vendor/ after fetch_deps. "
            "Run: components/ocpp_client/scripts/fetch_deps.sh"
        )
    _LOGGER.info("OCPP vendor deps ready (MicroOCPP + ArduinoJson).")
    _patch_mocpp_priv_includes(mocpp)


def _validate_csms_url(value):
    value = cv.url(value)
    lower = value.lower()
    if not (lower.startswith("wss://") or lower.startswith("ws://")):
        raise cv.Invalid(
            "csms_url must use wss:// (TLS) or ws:// with allow_cleartext_ws (lab only)."
        )
    return value


def _validate_fail_safe(config):
    if CONF_FAIL_SAFE_AMPS in config and config[CONF_FAIL_SAFE_AMPS] <= 0:
        raise cv.Invalid("fail_safe_amps must be > 0")
    # Compile-time MicroOCPP link when YAML enabled:true OR when runtime entities
    # imply the OCPP firmware path is desired. CI uses enabled:true.
    if config.get(CONF_ENABLED, False):
        for key in (CONF_CSMS_URL, CONF_CHARGE_POINT_ID, CONF_AUTHORIZATION_KEY):
            if key not in config:
                raise cv.Invalid(f"'{key}' is required when ocpp_client.enabled is true")
        _ensure_vendor_deps()
    url = config.get(CONF_CSMS_URL, "") or ""
    if url.lower().startswith("ws://") and not config.get(CONF_ALLOW_CLEARTEXT_WS, False):
        raise cv.Invalid(
            "csms_url ws:// requires allow_cleartext_ws: true "
            "(CISO lab gate; RFC1918/.local enforced at runtime)."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OcppClientComponent),
            cv.Optional(CONF_ENABLED, default=False): cv.boolean,
            cv.Optional(CONF_CSMS_URL): _validate_csms_url,
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
            cv.Optional(CONF_ENABLE_SWITCH): switch.switch_schema(
                OcppEnableSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="RESTORE_DEFAULT_OFF",
            ),
            cv.Optional(CONF_CSMS_URL_TEXT): text.text_schema(
                OcppParamText,
                mode="TEXT",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_CHARGE_POINT_ID_TEXT): text.text_schema(
                OcppParamText,
                mode="TEXT",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_AUTHORIZATION_KEY_TEXT): text.text_schema(
                OcppParamText,
                mode="PASSWORD",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_CA_CERT_TEXT): text.text_schema(
                OcppParamText,
                mode="PASSWORD",
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_FAIL_SAFE_BUTTON): button.button_schema(
                OcppFailSafeButton,
                entity_category=ENTITY_CATEGORY_CONFIG,
            ),
            cv.Optional(CONF_ALLOW_REMOTE_START): switch.switch_schema(
                OcppFeatureSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="ALWAYS_OFF",
            ),
            cv.Optional(CONF_ALLOW_REMOTE_STOP): switch.switch_schema(
                OcppFeatureSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="ALWAYS_OFF",
            ),
            cv.Optional(CONF_ALLOW_RESET): switch.switch_schema(
                OcppFeatureSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="ALWAYS_OFF",
            ),
            cv.Optional(CONF_ALLOW_UNLOCK): switch.switch_schema(
                OcppFeatureSwitch,
                entity_category=ENTITY_CATEGORY_CONFIG,
                default_restore_mode="ALWAYS_OFF",
            ),
            # TLS: default verify via crt_bundle. allow_insecure_tls = lab-only (RFC1918/.local).
            cv.Optional(CONF_ALLOW_INSECURE_TLS, default=False): cv.boolean,
            # Cleartext ws:// lab-only (same RFC1918/.local gate); DEFAULT false.
            cv.Optional(CONF_ALLOW_CLEARTEXT_WS, default=False): cv.boolean,
            cv.Optional(CONF_CRT_BUNDLE_ATTACH, default=True): cv.boolean,
            cv.Optional(CONF_CA_CERT): cv.string,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_fail_safe,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    director = await cg.get_variable(config[CONF_TWC_DIRECTOR_ID])
    cg.add(var.set_director(director))
    cg.add(var.set_enabled_default(config[CONF_ENABLED]))
    cg.add(var.set_fail_safe_amps(config[CONF_FAIL_SAFE_AMPS]))
    cg.add(var.set_charge_point_vendor(config[CONF_VENDOR]))
    cg.add(var.set_charge_point_model(config[CONF_MODEL]))

    if CONF_CSMS_URL in config:
        cg.add(var.set_csms_url(config[CONF_CSMS_URL]))
    if CONF_CHARGE_POINT_ID in config:
        cg.add(var.set_charge_point_id(config[CONF_CHARGE_POINT_ID]))
    if CONF_AUTHORIZATION_KEY in config:
        cg.add(var.set_authorization_key(config[CONF_AUTHORIZATION_KEY]))

    cg.add(var.set_allow_insecure_tls(config[CONF_ALLOW_INSECURE_TLS]))
    cg.add(var.set_allow_cleartext_ws(config[CONF_ALLOW_CLEARTEXT_WS]))
    cg.add(var.set_crt_bundle_attach(config[CONF_CRT_BUNDLE_ATTACH]))
    if CONF_CA_CERT in config:
        cg.add(var.set_ca_cert(config[CONF_CA_CERT]))

    if CONF_CONNECTED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(var.set_connected_sensor(sens))
    if CONF_CONNECTION_STATE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CONNECTION_STATE])
        cg.add(var.set_connection_state_sensor(sens))
    if CONF_LAST_APPLIED_AMPS in config:
        sens = await sensor.new_sensor(config[CONF_LAST_APPLIED_AMPS])
        cg.add(var.set_last_applied_amps_sensor(sens))

    if CONF_ENABLE_SWITCH in config:
        sw = await switch.new_switch(config[CONF_ENABLE_SWITCH])
        cg.add(var.set_enable_switch(sw))
    if CONF_CSMS_URL_TEXT in config:
        t = await text.new_text(config[CONF_CSMS_URL_TEXT])
        cg.add(var.set_csms_url_text(t))
    if CONF_CHARGE_POINT_ID_TEXT in config:
        t = await text.new_text(config[CONF_CHARGE_POINT_ID_TEXT])
        cg.add(var.set_charge_point_id_text(t))
    if CONF_AUTHORIZATION_KEY_TEXT in config:
        t = await text.new_text(config[CONF_AUTHORIZATION_KEY_TEXT])
        cg.add(var.set_authorization_key_text(t))
    if CONF_CA_CERT_TEXT in config:
        # PEM can exceed the default 255; HA state is redacted (never raw PEM).
        t = await text.new_text(config[CONF_CA_CERT_TEXT], max_length=4094)
        cg.add(var.set_ca_cert_text(t))
    if CONF_FAIL_SAFE_BUTTON in config:
        btn = await button.new_button(config[CONF_FAIL_SAFE_BUTTON])
        cg.add(var.set_fail_safe_button(btn))
    if CONF_ALLOW_REMOTE_START in config:
        sw = await switch.new_switch(config[CONF_ALLOW_REMOTE_START])
        cg.add(var.set_remote_start_switch(sw))
    if CONF_ALLOW_REMOTE_STOP in config:
        sw = await switch.new_switch(config[CONF_ALLOW_REMOTE_STOP])
        cg.add(var.set_remote_stop_switch(sw))
    if CONF_ALLOW_RESET in config:
        sw = await switch.new_switch(config[CONF_ALLOW_RESET])
        cg.add(var.set_reset_switch(sw))
    if CONF_ALLOW_UNLOCK in config:
        sw = await switch.new_switch(config[CONF_ALLOW_UNLOCK])
        cg.add(var.set_unlock_switch(sw))

    # Link MicroOCPP whenever YAML has enabled:true (CI / lab firmware path).
    # Runtime HA enable can still start/stop without rebuild once linked.
    if config[CONF_ENABLED]:
        if not CORE.is_esp32:
            raise cv.Invalid("ocpp_client enabled:true requires ESP32 with ESP-IDF framework")
        from esphome.components.esp32 import (
            add_idf_component,
            add_idf_sdkconfig_option,
            include_builtin_idf_component,
        )

        vendor = Path(__file__).parent / "vendor"
        add_idf_component(name="MicroOcpp", path=str(vendor / "MicroOcpp"))
        add_idf_component(name="ocpp_mocpp_bridge", path=str(vendor / "ocpp_mocpp_bridge"))
        add_idf_component(name="espressif/esp_websocket_client", ref="1.4.0")
        include_builtin_idf_component("spiffs")
        # Default verify path needs CA bundle (already typical on ESPHome; force on).
        add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True)
        # Skip-verify only compiles in when YAML asks; runtime still RFC1918/.local-gated.
        if config.get(CONF_ALLOW_INSECURE_TLS):
            add_idf_sdkconfig_option("CONFIG_ESP_TLS_INSECURE", True)
            add_idf_sdkconfig_option("CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY", True)
            _LOGGER.warning(
                "ocpp_client.allow_insecure_tls: true — firmware will allow TLS verify "
                "skip only for RFC1918 / .local lab CSMS hosts (CISO)."
            )
        if config.get(CONF_ALLOW_CLEARTEXT_WS):
            _LOGGER.warning(
                "ocpp_client.allow_cleartext_ws: true — firmware will allow ws:// only for "
                "RFC1918 / .local lab CSMS hosts; Remote* stay OFF on cleartext (CISO). "
                "Remove after wss handoff; never leave in prod-config."
            )
        cg.add_define("USE_MICROOCPP")
        cg.add_build_flag("-DMO_PLATFORM=MO_PLATFORM_ESPIDF")
        cg.add_build_flag("-DMO_ENABLE_CONNECTOR_LOCK=0")
        # CP + up to 4 TWC slots (connectorId 1..4 ↔ slots). OCPP 2.0.1 not enabled.
        cg.add_build_flag("-DMO_NUMCONNECTORS=5")
        cg.add_build_flag("-DMO_ENABLE_V201=0")
        cg.add_build_flag("-DTWC_OCPP_MAX_CONNECTORS=4")
