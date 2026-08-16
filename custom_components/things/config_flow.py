"""Config flow for the Things integration.

Primary path is zeroconf discovery (the node already SRP-registers
_coap._udp with a "caps" TXT entry - see node/lib/thread/thread.c). Manual
entry is a fallback for when discovery doesn't fire, where the capability
set has to be told to us rather than read from a TXT record.
"""

from __future__ import annotations

import logging
from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.const import CONF_HOST, CONF_NAME
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers.selector import SelectSelector, SelectSelectorConfig

from .api import ThingsApiClient, ThingsApiError
from .const import CONF_CAPS, DOMAIN, KNOWN_CAPS

_LOGGER = logging.getLogger(__name__)


class ThingsConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config flow for a single Things node."""

    VERSION = 1

    def __init__(self) -> None:
        self._discovered_host: str | None = None
        self._discovered_name: str | None = None
        self._discovered_caps: list[str] = []

    async def async_step_zeroconf(self, discovery_info) -> FlowResult:
        """Handle discovery via the node's SRP-registered _coap._udp service."""
        caps_raw = discovery_info.properties.get("caps", "")
        caps = [c for c in caps_raw.split(",") if c]
        # mDNS instance names look like "<hostname>._coap._udp.local."
        name = discovery_info.name.removesuffix("._coap._udp.local.")

        await self.async_set_unique_id(name)
        self._abort_if_unique_id_configured(updates={CONF_HOST: discovery_info.host})

        self._discovered_host = discovery_info.host
        self._discovered_name = name
        self._discovered_caps = caps
        self.context["title_placeholders"] = {"name": name}

        return await self.async_step_discovery_confirm()

    async def async_step_discovery_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Confirm a discovered node before creating its config entry."""
        if user_input is not None:
            return self.async_create_entry(
                title=self._discovered_name,
                data={
                    CONF_HOST: self._discovered_host,
                    CONF_CAPS: self._discovered_caps,
                },
            )

        return self.async_show_form(
            step_id="discovery_confirm",
            description_placeholders={
                "name": self._discovered_name,
                "caps": ", ".join(self._discovered_caps) or "none declared",
            },
        )

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Manual fallback - capabilities have to be told to us, not read
        from a TXT record we never saw.

        Unique ID is the node's SRP hostname (same identity zeroconf uses),
        not the host/address - otherwise a manually-added entry and its
        zeroconf discovery never recognize each other as the same node, and
        the discovered card keeps coming back even after being added.
        """
        errors: dict[str, str] = {}

        if user_input is not None:
            name = user_input[CONF_NAME]
            host = user_input[CONF_HOST]

            client = await ThingsApiClient.create(host)
            try:
                # No dedicated /info endpoint yet - if "battery" was picked,
                # a live read is at least some confirmation the node's
                # actually there and reachable.
                if CONF_CAPS in user_input and "battery" in user_input[CONF_CAPS]:
                    await client.get_battery()
            except ThingsApiError:
                errors["base"] = "cannot_connect"
            finally:
                await client.close()

            if not errors:
                await self.async_set_unique_id(name)
                self._abort_if_unique_id_configured(updates={CONF_HOST: host})
                return self.async_create_entry(
                    title=name,
                    data={CONF_HOST: host, CONF_CAPS: user_input[CONF_CAPS]},
                )

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_NAME): str,
                    vol.Required(CONF_HOST): str,
                    vol.Required(CONF_CAPS, default=[]): SelectSelector(
                        SelectSelectorConfig(options=KNOWN_CAPS, multiple=True)
                    ),
                }
            ),
            errors=errors,
        )
