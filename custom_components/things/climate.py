"""Climate platform for a Things AC node.

IR is fire-and-forget with zero feedback from the physical unit (see
node/lib/ir/ir.c) - this entity runs entirely in assumed-state/optimistic
mode. HA just remembers what it last told the node to do; it has no way
to know the AC's real state.

Bounds (16-30C, no mode/fan selection) come straight from
node/lib/ir/voltas.c and teco.c - both dialects agree on temp range, and
neither exposes mode or fan speed as a controllable ir_cmd_t.
"""

from __future__ import annotations

import logging
from typing import Any

from homeassistant.components.climate import (
    ClimateEntity,
    ClimateEntityFeature,
    HVACMode,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import ATTR_TEMPERATURE, UnitOfTemperature
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .device import device_info_for

_LOGGER = logging.getLogger(__name__)

MIN_TEMP = 16
MAX_TEMP = 30


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    """Set up the AC climate entity (only called if ac_climate is in caps)."""
    async_add_entities([ThingsClimate(entry)])


class ThingsClimate(ClimateEntity):
    """Assumed-state AC control over the node's POST /ir CoAP endpoint."""

    _attr_has_entity_name = True
    _attr_name = None
    _attr_assumed_state = True
    _attr_hvac_modes = [HVACMode.OFF, HVACMode.COOL]
    _attr_temperature_unit = UnitOfTemperature.CELSIUS
    _attr_min_temp = MIN_TEMP
    _attr_max_temp = MAX_TEMP
    _attr_target_temperature_step = 1
    _attr_swing_modes = ["off", "on"]
    _attr_supported_features = (
        ClimateEntityFeature.TARGET_TEMPERATURE
        | ClimateEntityFeature.SWING_MODE
        | ClimateEntityFeature.TURN_ON
        | ClimateEntityFeature.TURN_OFF
    )

    def __init__(self, entry: ConfigEntry) -> None:
        self._entry = entry
        self._attr_unique_id = f"{entry.entry_id}_climate"
        self._attr_device_info = device_info_for(entry)
        self._attr_hvac_mode = HVACMode.OFF
        self._attr_target_temperature: float = 24
        self._attr_swing_mode = "off"

    @property
    def _client(self):
        return self._entry.runtime_data.client

    async def async_set_hvac_mode(self, hvac_mode: HVACMode) -> None:
        """Turn the AC on/off.

        power_on resets the unit to a hardcoded default state on the
        firmware side (voltas_default_on in voltas.c) - re-assert whatever
        HA last had set right after, so the two don't silently drift apart.
        """
        if hvac_mode == HVACMode.OFF:
            await self._client.send_ir_command("power_off")
        else:
            await self._client.send_ir_command("power_on")
            await self._client.send_ir_command(
                "set_temp", temp=int(self._attr_target_temperature)
            )
            await self._client.send_ir_command(
                "set_swing", swing=self._attr_swing_mode == "on"
            )
        self._attr_hvac_mode = hvac_mode
        self.async_write_ha_state()

    async def async_set_temperature(self, **kwargs: Any) -> None:
        temp = kwargs.get(ATTR_TEMPERATURE)
        if temp is None:
            return
        await self._client.send_ir_command("set_temp", temp=int(temp))
        self._attr_target_temperature = temp
        self.async_write_ha_state()

    async def async_set_swing_mode(self, swing_mode: str) -> None:
        await self._client.send_ir_command("set_swing", swing=swing_mode == "on")
        self._attr_swing_mode = swing_mode
        self.async_write_ha_state()
