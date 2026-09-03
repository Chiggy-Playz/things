"""Config flow for the Things integration.

Primary path is zeroconf discovery (the node already SRP-registers
_coap._udp with "caps" and "config" TXT entries - see node/lib/thread/
thread.c). Manual entry is a multi-step fallback for when discovery
doesn't fire: name+host, then capabilities (best-effort auto-detected via
a live GET /caps query against the host just entered, always overridable),
then - only if "ac_climate" was selected - which swing axis this specific
physical unit has a motor for.
"""

from __future__ import annotations

import logging
from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.const import CONF_HOST, CONF_NAME
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers.selector import (
    SelectSelector,
    SelectSelectorConfig,
    SelectSelectorMode,
)

from .api import ThingsApiClient, ThingsApiError
from .const import (
    CAP_AC_CLIMATE,
    CAP_LABELS,
    CONF_CAPS,
    CONF_NODE_CONFIG,
    CONF_SWING_AXIS,
    DOMAIN,
    KNOWN_CAPS,
    KNOWN_SWING_AXES,
    parse_node_config,
)

_LOGGER = logging.getLogger(__name__)


class ThingsConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config flow for a single Things node."""

    VERSION = 1

    def __init__(self) -> None:
        self._discovered_host: str | None = None
        self._discovered_name: str | None = None
        self._discovered_caps: list[str] = []
        self._discovered_node_config: dict[str, str] = {}

        # Manual-entry path state, accumulated across async_step_user ->
        # async_step_capabilities -> async_step_swing_axis. Reconfigure
        # reuses this exact same chain (see async_step_reconfigure) rather
        # than duplicating it, starting from async_step_capabilities
        # instead of async_step_user since name+host are already known.
        self._manual_name: str | None = None
        self._manual_host: str | None = None
        self._manual_caps: list[str] = []
        self._manual_node_config: dict[str, str] = {}
        # Cached so async_step_swing_axis doesn't re-query the node.
        self._detected_node_config: dict[str, str] = {}

        # Set only when reconfiguring an existing entry (see
        # async_step_reconfigure) - _create_manual_entry checks this to
        # decide between creating a new entry and updating this one in
        # place. Also doubles as the fallback source for the capabilities
        # form when a live /caps query fails (an unreachable node during
        # reconfigure shouldn't blank out its already-known-good config).
        self._reconfigure_entry: config_entries.ConfigEntry | None = None

    async def async_step_zeroconf(self, discovery_info) -> FlowResult:
        """Handle discovery via the node's SRP-registered _coap._udp service."""
        caps_raw = discovery_info.properties.get("caps", "")
        caps = [c for c in caps_raw.split(",") if c]
        config_raw = discovery_info.properties.get("config", "")
        node_config = parse_node_config(config_raw)
        # mDNS instance names look like "<hostname>._coap._udp.local."
        name = discovery_info.name.removesuffix("._coap._udp.local.")

        await self.async_set_unique_id(name)
        self._abort_if_unique_id_configured(updates={CONF_HOST: discovery_info.host})

        self._discovered_host = discovery_info.host
        self._discovered_name = name
        self._discovered_caps = caps
        self._discovered_node_config = node_config
        self.context["title_placeholders"] = {"name": name}

        return await self.async_step_discovery_confirm()

    async def async_step_discovery_confirm(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Confirm a discovered node before creating its config entry."""
        if user_input is not None:
            data = {
                CONF_HOST: self._discovered_host,
                CONF_CAPS: self._discovered_caps,
            }
            if self._discovered_node_config:
                data[CONF_NODE_CONFIG] = self._discovered_node_config
            return self.async_create_entry(title=self._discovered_name, data=data)

        return self.async_show_form(
            step_id="discovery_confirm",
            description_placeholders={
                "name": self._discovered_name,
                "caps": ", ".join(
                    CAP_LABELS.get(c, c) for c in self._discovered_caps
                )
                or "nothing declared",
            },
        )

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Manual fallback, step 1 of 3 (or 2, if ac_climate isn't chosen
        next): just the node's identity - capabilities come next, once we
        have a host to try auto-detecting against.
        """
        if user_input is not None:
            self._manual_name = user_input[CONF_NAME]
            self._manual_host = user_input[CONF_HOST]
            return await self.async_step_capabilities()

        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_NAME): str,
                    vol.Required(CONF_HOST): str,
                }
            ),
        )

    async def async_step_reconfigure(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Entry point for the "Reconfigure" option on an existing entry.

        Reuses the manual-entry capabilities -> swing_axis chain rather
        than a separate implementation - name and host are already known,
        so it jumps straight to capabilities and lets _create_manual_entry
        update the existing entry instead of creating a new one.
        """
        entry = self._get_reconfigure_entry()
        self._reconfigure_entry = entry
        self._manual_name = entry.title
        self._manual_host = entry.data[CONF_HOST]
        return await self.async_step_capabilities()

    async def _detect_node(self) -> tuple[list[str], dict[str, str]]:
        """Best-effort live GET /caps against self._manual_host.

        Never raises - a node that's unreachable (wrong host, not flashed
        yet, briefly offline) just means an empty starting point for the
        capabilities form, not a hard failure. The user can always fill it
        in by hand regardless of what auto-detect finds.
        """
        try:
            client = await ThingsApiClient.create(self._manual_host)
        except Exception:  # noqa: BLE001 - genuinely best-effort
            return [], {}
        try:
            info = await client.get_caps()
        except ThingsApiError:
            return [], {}
        finally:
            await client.close()

        caps = [c for c in info.get("caps", "").split(",") if c]
        node_config = parse_node_config(info.get("config", ""))
        return caps, node_config

    async def async_step_capabilities(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Manual fallback, step 2: which entity platforms this node needs.

        Pre-filled from a live /caps query when possible, always editable.
        """
        if user_input is not None:
            self._manual_caps = user_input[CONF_CAPS]
            if CAP_AC_CLIMATE in self._manual_caps:
                return await self.async_step_swing_axis()
            return await self._create_manual_entry()

        detected_caps, self._detected_node_config = await self._detect_node()
        if not detected_caps and self._reconfigure_entry is not None:
            # Node briefly unreachable during a reconfigure shouldn't blank
            # out its already-known-good config - fall back to what the
            # entry already has instead of an empty form.
            detected_caps = self._reconfigure_entry.data.get(CONF_CAPS, [])
            self._detected_node_config = self._reconfigure_entry.data.get(
                CONF_NODE_CONFIG, {}
            )

        return self.async_show_form(
            step_id="capabilities",
            data_schema=vol.Schema(
                {
                    vol.Required(CONF_CAPS, default=detected_caps): SelectSelector(
                        SelectSelectorConfig(
                            options=KNOWN_CAPS,
                            multiple=True,
                            translation_key="thing_caps",
                        )
                    ),
                }
            ),
            description_placeholders={
                "detected": (
                    "Pre-filled from the node - adjust if wrong."
                    if detected_caps
                    else "Couldn't reach the node to auto-detect - select manually."
                ),
            },
        )

    async def async_step_swing_axis(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Manual fallback, step 3 (only if ac_climate was selected): which
        physical swing axis this unit actually has a motor for - a split
        unit and a window unit can both declare ac_climate but only one
        axis is ever real per unit."""
        if user_input is not None:
            self._manual_node_config = {CONF_SWING_AXIS: user_input[CONF_SWING_AXIS]}
            return await self._create_manual_entry()

        default_axis = self._detected_node_config.get(CONF_SWING_AXIS)
        schema: dict[Any, Any] = {}
        key = (
            vol.Required(CONF_SWING_AXIS, default=default_axis)
            if default_axis in KNOWN_SWING_AXES
            else vol.Required(CONF_SWING_AXIS)
        )
        schema[key] = SelectSelector(
            SelectSelectorConfig(
                options=KNOWN_SWING_AXES,
                mode=SelectSelectorMode.LIST,
                translation_key="swing_axis",
            )
        )

        return self.async_show_form(step_id="swing_axis", data_schema=vol.Schema(schema))

    async def _create_manual_entry(self) -> FlowResult:
        data = {CONF_HOST: self._manual_host, CONF_CAPS: self._manual_caps}
        if self._manual_node_config:
            data[CONF_NODE_CONFIG] = self._manual_node_config

        if self._reconfigure_entry is not None:
            return self.async_update_reload_and_abort(
                self._reconfigure_entry, data=data
            )

        await self.async_set_unique_id(self._manual_name)
        self._abort_if_unique_id_configured(updates={CONF_HOST: self._manual_host})
        return self.async_create_entry(title=self._manual_name, data=data)
