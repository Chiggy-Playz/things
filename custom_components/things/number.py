"""Number platform for the AC node's two IR timers.

HA's climate entity has no native concept of a "turn on/off in N hours"
timer, so these live here instead - setting the value fires the
corresponding set_timer_on/set_timer_off command immediately (see
node/lib/ir/ir.c), 0 clears that timer.

Exposed in whole hours, not minutes: both the real Voltas remote's own UI
and the Teco protocol's timer field only ever support whole-hour steps -
see the timer investigation in memory/PROGRESS.md - so a raw 0-1440 minute
number box let you enter values the hardware can't actually represent. A
slider (capped at MAX_TIMER_HOURS, well under the protocol's real ceiling -
nobody needs the full range on a slider) is a nicer control than a bare
number box.
"""

from __future__ import annotations

from datetime import timedelta
import logging

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import UnitOfTime
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.util import dt as dt_util

from .device import device_info_for

_LOGGER = logging.getLogger(__name__)

# Teco's timer field clamps to 24h (node/lib/ir/teco.c) and Voltas' encoding
# tops out in the same ballpark, but in practice nobody needs the full
# range on a slider - capped lower for a usable control.
MAX_TIMER_HOURS = 4


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
    """One of the two AC timers, in whole hours. 0 = clear.

    Snaps back to 0 immediately after sending, rather than holding the set
    value and counting down on the number entity itself - a slider that's
    also quietly ticking down on its own fights the next drag. The actual
    "how long left" readout lives on the matching timestamp sensor instead
    (sensor.py's ThingsTimerDeadlineSensor) - HA renders a timestamp as
    live relative time on its own, so nothing here needs to tick. Treat
    this slider as a momentary "arm a timer for N hours now" trigger, not
    a persisted duration display.

    Does still schedule a matching hvac_mode flip on the climate entity
    (see ThingsClimate.schedule_timer_switch) for when the timer *should*
    fire, going through entry.runtime_data.climate_entity rather than a
    direct reference since these are two separate platforms - same
    unverified-guess caveat applies there (see climate.py's docstring), as
    it does for the deadline this reports to the coordinator."""

    _attr_has_entity_name = True
    _attr_native_min_value = 0
    _attr_native_max_value = MAX_TIMER_HOURS
    _attr_native_step = 1
    _attr_mode = NumberMode.SLIDER
    _attr_native_unit_of_measurement = UnitOfTime.HOURS
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
            f"set_{self._kind}", mins=int(value) * 60
        )
        deadline = dt_util.utcnow() + timedelta(hours=value) if value > 0 else None
        self._entry.runtime_data.set_timer_deadline(self._kind, deadline)
        if climate := self._entry.runtime_data.climate_entity:
            climate.schedule_timer_switch(self._kind, value)
        self._attr_native_value = 0
        self.async_write_ha_state()
