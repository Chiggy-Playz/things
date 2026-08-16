"""Shared DeviceInfo helper - every entity for a node should show up under
one device page, however many capabilities/entities that node has.
"""

from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.helpers.entity import DeviceInfo

from .const import DOMAIN


def device_info_for(entry: ConfigEntry) -> DeviceInfo:
    """Build the shared DeviceInfo all of a node's entities attach to."""
    return DeviceInfo(
        identifiers={(DOMAIN, entry.entry_id)},
        name=entry.title,
        manufacturer="Chiggy-Playz/things",
        model="Things node (nRF52840 / Zephyr)",
    )
