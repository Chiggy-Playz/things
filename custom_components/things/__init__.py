"""The Things integration - discovers and controls Zephyr/Thread nodes
from this repo's node/ firmware, over CoAP.

A config entry = one physical node. Which entity platforms get set up is
the union of PLATFORMS_BY_CAP over whatever capabilities that node
declared at discovery time (see const.py) - nothing here assumes a fixed
node "type".
"""

from __future__ import annotations

import logging

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import CONF_HOST
from homeassistant.core import HomeAssistant

from .api import ThingsApiClient
from .const import CAP_BATTERY, CONF_CAPS, PLATFORMS_BY_CAP
from .coordinator import ThingsCoordinator

_LOGGER = logging.getLogger(__name__)

type ThingsConfigEntry = ConfigEntry[ThingsCoordinator]


def _platforms_for(entry: ConfigEntry) -> list[str]:
    caps = entry.data.get(CONF_CAPS, [])
    return sorted({platform for cap in caps for platform in PLATFORMS_BY_CAP.get(cap, [])})


async def async_setup_entry(hass: HomeAssistant, entry: ThingsConfigEntry) -> bool:
    """Set up a Things node from a config entry."""
    client = await ThingsApiClient.create(entry.data[CONF_HOST])
    coordinator = ThingsCoordinator(hass, entry, client, entry.title)

    if CAP_BATTERY in entry.data.get(CONF_CAPS, []):
        await coordinator.async_config_entry_first_refresh()

    entry.runtime_data = coordinator

    await hass.config_entries.async_forward_entry_setups(entry, _platforms_for(entry))
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ThingsConfigEntry) -> bool:
    """Unload a config entry."""
    unloaded = await hass.config_entries.async_unload_platforms(
        entry, _platforms_for(entry)
    )
    if unloaded:
        await entry.runtime_data.client.close()
    return unloaded
