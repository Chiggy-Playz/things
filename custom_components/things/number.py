"""Number platform for the AC node's two IR timers.

HA's climate entity has no native concept of a "turn on/off in N minutes"
timer, so these live here instead - setting the value fires the
corresponding set_timer_on/set_timer_off command immediately (see
node/lib/ir/ir.c), 0 clears that timer.
"""

from __future__ import annotations

import logging

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .device import device_info_for

_LOGGER = logging.getLogger(__name__)

# Teco's timer field clamps to 24h (node/lib/ir/teco.c); Voltas' encoding
# tops out in the same ballpark - use one shared ceiling for both.
MAX_TIMER_MINUTES = 24 * 60


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    """Set up the two timer number entities (only called if ac_climate is in caps)."""
    async_add_entities(
        [
            ThingsTimerNumber(entry, "timer_on", "Timer on in", "mdi:timer-play-outline"),
            ThingsTimerNumber(entry, "timer_off", "Timer off in", "mdi:timer-stop-outline"),
        ]
    )


class ThingsTimerNumber(NumberEntity):
    """One of the two AC timers, in minutes. 0 = clear."""

    _attr_has_entity_name = True
    _attr_native_min_value = 0
    _attr_native_max_value = MAX_TIMER_MINUTES
    _attr_native_step = 1
    _attr_mode = NumberMode.BOX
    _attr_native_unit_of_measurement = UnitOfTime.MINUTES
    _attr_assumed_state = True

    def __init__(self, entry: ConfigEntry, kind: str, name: str, icon: str) -> None:
        self._entry = entry
        self._kind = kind
        self._attr_name = name
        self._attr_icon = icon
        self._attr_unique_id = f"{entry.entry_id}_{kind}"
        self._attr_device_info = device_info_for(entry)
        self._attr_native_value = 0

    async def async_set_native_value(self, value: float) -> None:
        await self._entry.runtime_data.client.send_ir_command(
            f"set_{self._kind}", mins=int(value)
        )
        self._attr_native_value = value
        self.async_write_ha_state()
