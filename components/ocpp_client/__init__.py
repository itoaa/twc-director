# PoC stub: ocpp_client is not yet a loadable ESPHome component.
# Enabling it in YAML will fail until MicroOCPP integration lands.
# See README.md in this folder and docs/OCPP.md.
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@itoaa"]

# Keep schema strict so accidental enable fails closed with a clear message.
CONFIG_SCHEMA = cv.All(
    cv.invalid(
        "ocpp_client is scaffolding only on feature/ocpp-1.6 — "
        "MicroOCPP integration not wired yet. See docs/OCPP.md."
    )
)
