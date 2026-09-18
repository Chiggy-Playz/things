"""Sensor platform - battery readings and IR-timer deadlines.

Battery: two entities off one coordinator (mV + %). Note the % reading is
sampled *during* radio activity in the firmware (the CoAP handler reads the
ADC while responding to this exact request) rather than at rest - which
turns out to be the technically correct way to catch load-dependent
voltage sag on a coin cell, not a bug. See PROGRESS.md.

Timer deadlines: this platform is forwarded for CAP_BATTERY *or*
CAP_AC_CLIMATE now (see const.py's PLATFORMS_BY_CAP), since a node can have
either, both, or (in practice, today) always both - so entity creation
below is keyed off which caps are actually present, not "any sensor
entities exist at all".
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

from .const import CAP_AC_CLIMATE, CAP_BATTERY, CONF_CAPS
from .coordinator import ThingsCoordinator
from .device import device_info_for

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(
    hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback
) -> None:
    """Set up whichever sensors this node's declared caps call for."""
    coordinator: ThingsCoordinator = entry.runtime_data
    caps = entry.data.get(CONF_CAPS, [])
    entities: list[SensorEntity] = []

    if CAP_BATTERY in caps:
        entities += [
            ThingsBatteryVoltageSensor(coordinator, entry),
            ThingsBatteryPercentSensor(coordinator, entry),
        ]

    if CAP_AC_CLIMATE in caps:
        entities += [
            ThingsTimerDeadlineSensor(entry, "timer_on", "Timer on at"),
            ThingsTimerDeadlineSensor(entry, "timer_off", "Timer off at"),
        ]

    async_add_entities(entities)


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


class ThingsTimerDeadlineSensor(SensorEntity):
    """Absolute time a pending IR timer should fire - see number.py's
    async_set_native_value() and climate.py's schedule_timer_switch().

    Not a CoordinatorEntity: this has nothing to do with the battery poll
    cycle, and updates the instant a timer's armed/cancelled/fires rather
    than on a schedule. coordinator.py writes straight into it (via
    coordinator.timer_sensors) the same way climate.py's timer_cancel
    callbacks do, once it's registered here."""

    _attr_has_entity_name = True
    _attr_device_class = SensorDeviceClass.TIMESTAMP

    def __init__(self, entry: ConfigEntry, kind: str, name: str) -> None:
        self._entry = entry
        self._kind = kind
        self._attr_name = name
        self._attr_unique_id = f"{entry.entry_id}_{kind}_at"
        self._attr_device_info = device_info_for(entry)

    @property
    def native_value(self):
        return self._entry.runtime_data.timer_deadline.get(self._kind)

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self._entry.runtime_data.timer_sensors[self._kind] = self

    async def async_will_remove_from_hass(self) -> None:
        if self._entry.runtime_data.timer_sensors.get(self._kind) is self:
            self._entry.runtime_data.timer_sensors[self._kind] = None
