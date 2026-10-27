import logging

import esphome.codegen as cg
from esphome.components import i2c, pn532
import esphome.config_validation as cv
from esphome.const import CONF_ID, __version__ as ESPHOME_VERSION
from esphome.core import Version
from esphome.types import ConfigType

AUTO_LOAD = ["pn532"]
CODEOWNERS = ["@OttoWinter", "@jesserockz"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

_LOGGER = logging.getLogger(__name__)

# The C++ side replaces PN532::loop() and implements the virtual transport API
# of exactly this ESPHome release line. Older releases have a different PN532
# base API (use the 2026.9.1 pin f89b5ef7f21067ecf64ed1b2a77bb322b8541df4).
MIN_ESPHOME_VERSION = Version(2026, 10, 0)
TESTED_RELEASE_LINE = (2026, 10)

pn532_i2c_ns = cg.esphome_ns.namespace("pn532_i2c")
PN532I2C = pn532_i2c_ns.class_("PN532I2C", pn532.PN532, i2c.I2CDevice)


def _warn_untested_esphome(config: ConfigType) -> ConfigType:
    current = Version.parse(ESPHOME_VERSION)
    if (current.major, current.minor) != TESTED_RELEASE_LINE:
        _LOGGER.warning(
            "Adonno HCE pn532_i2c was ported and tested for ESPHome %s.%s.x only; "
            "running on %s. Review docs/UPSTREAM.md and re-test on hardware "
            "before installing this firmware on a reader.",
            *TESTED_RELEASE_LINE,
            ESPHOME_VERSION,
        )
    return config


CONFIG_SCHEMA = cv.All(
    pn532.PN532_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(PN532I2C),
        }
    ).extend(i2c.i2c_device_schema(0x24)),
    cv.require_esphome_version(MIN_ESPHOME_VERSION),
    _warn_untested_esphome,
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await pn532.setup_pn532(var, config)
    await i2c.register_i2c_device(var, config)
