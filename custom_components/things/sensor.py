"""Battery sensor platform - two entities off one coordinator (mV + %).

Note the % reading is sampled *during* radio activity in the firmware (the
CoAP handler reads the ADC while responding to this exact request) rather
than at rest - which turns out to be the technically correct way to catch
load-dependent voltage sag on a coin cell, not a bug. See PROGRESS.md.
"""

from __future__ import annotations

import logging

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorStateClass,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import PERCENTAGE, UnitOfElectricPotential
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from .coordinator import ThingsCoordinator
from .device import device_info_for

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    """Set up the battery sensors (only called if battery is in caps)."""
    coordinator: ThingsCoordinator = entry.runtime_data
    async_add_entities(
        [
            ThingsBatteryVoltageSensor(coordinator, entry),
            ThingsBatteryPercentSensor(coordinator, entry),
        ]
    )


class ThingsBatteryVoltageSensor(CoordinatorEntity[ThingsCoordinator], SensorEntity):
    """Raw millivolt reading - diagnostic, off by default; percent is the headline."""

    _attr_has_entity_name = True
    _attr_name = "Battery voltage"
    _attr_device_class = SensorDeviceClass.VOLTAGE
    _attr_native_unit_of_measurement = UnitOfElectricPotential.MILLIVOLT
    _attr_state_class = SensorStateClass.MEASUREMENT
    _attr_entity_registry_enabled_default = False

    def __init__(self, coordinator: ThingsCoordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.entry_id}_battery_mv"
        self._attr_device_info = device_info_for(entry)

    @property
    def native_value(self) -> int | None:
        return self.coordinator.data.get("mv") if self.coordinator.data else None


class ThingsBatteryPercentSensor(CoordinatorEntity[ThingsCoordinator], SensorEntity):
    """Rough CR2032-curve percentage - see node/lib/battery/battery.c."""

    _attr_has_entity_name = True
    _attr_name = "Battery"
    _attr_device_class = SensorDeviceClass.BATTERY
    _attr_native_unit_of_measurement = PERCENTAGE
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, coordinator: ThingsCoordinator, entry: ConfigEntry) -> None:
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.entry_id}_battery_percent"
        self._attr_device_info = device_info_for(entry)

    @property
    def native_value(self) -> int | None:
        return self.coordinator.data.get("percent") if self.coordinator.data else None
