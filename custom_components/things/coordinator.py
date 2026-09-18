"""Data update coordinator for a Things node's polled state.

Only battery is polled right now (see const.py's BATTERY_SCAN_INTERVAL_MINUTES
for why it's deliberately slow). AC control is separate, instant,
fire-and-forget POSTs via ThingsApiClient directly - not routed through here.
"""

from __future__ import annotations

from datetime import datetime, timedelta
import logging
from typing import TYPE_CHECKING

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import ThingsApiClient, ThingsApiError
from .const import BATTERY_SCAN_INTERVAL_MINUTES

if TYPE_CHECKING:
    from .climate import ThingsClimate
    from .sensor import ThingsTimerDeadlineSensor

_LOGGER = logging.getLogger(__name__)


class ThingsCoordinator(DataUpdateCoordinator[dict]):
    """Polls a Things node's /battery endpoint on a slow interval.

    Also doubles as the one shared object both the climate and number
    platforms for the same node already reach via entry.runtime_data - the
    climate entity registers itself here (climate.py's async_added_to_hass)
    so the timer number entities (number.py) can reach it to schedule an
    hvac_mode flip when a timer they just armed elapses, without the two
    platforms needing a direct reference to each other."""

    def __init__(
        self,
        hass: HomeAssistant,
        config_entry: ConfigEntry,
        client: ThingsApiClient,
        name: str,
    ) -> None:
        super().__init__(
            hass,
            _LOGGER,
            config_entry=config_entry,
            name=f"things-{name}",
            update_interval=timedelta(minutes=BATTERY_SCAN_INTERVAL_MINUTES),
        )
        self.client = client
        self.climate_entity: "ThingsClimate | None" = None
        # Absolute time each armed IR timer should fire, for the "Timer ...
        # at" sensors (sensor.py) - written by number.py when a timer's
        # armed/cleared and by climate.py once a pending one fires or gets
        # superseded by a manual mode change. None means nothing pending.
        # Not polled data, so it lives here rather than in self.data.
        self.timer_deadline: dict[str, datetime | None] = {
            "timer_on": None,
            "timer_off": None,
        }
        self.timer_sensors: dict[str, "ThingsTimerDeadlineSensor | None"] = {
            "timer_on": None,
            "timer_off": None,
        }

    async def _async_update_data(self) -> dict:
        try:
            return await self.client.get_battery()
        except ThingsApiError as err:
            raise UpdateFailed(str(err)) from err

    def set_timer_deadline(self, kind: str, deadline: datetime | None) -> None:
        """Record when (or whether) a timer is due to fire and push it to
        its sensor immediately, if that sensor's been added yet."""
        self.timer_deadline[kind] = deadline
        if sensor := self.timer_sensors.get(kind):
            sensor.async_write_ha_state()
