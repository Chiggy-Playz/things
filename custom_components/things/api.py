"""Minimal async CoAP client for a single Things node.

Matches the endpoints the firmware actually exposes (see node/lib/ir/ir.c
and node/lib/battery/battery.c) - no more, no less:
  - GET  /battery -> {"mv": int, "percent": int}
  - POST /ir      -> {"cmd": "<name>", ...fields}, fire-and-forget
"""

from __future__ import annotations

import json
import logging
from typing import Any

import aiocoap

_LOGGER = logging.getLogger(__name__)


class ThingsApiError(Exception):
    """Raised when a request to a Things node fails."""


class ThingsApiClient:
    """Thin async CoAP client wrapper for one node, one long-lived context."""

    def __init__(self, host: str, protocol: aiocoap.Context) -> None:
        self._host = host
        self._protocol = protocol

    @classmethod
    async def create(cls, host: str) -> "ThingsApiClient":
        """Create a client with its own CoAP context (one UDP socket, reused)."""
        protocol = await aiocoap.Context.create_client_context()
        return cls(host, protocol)

    async def close(self) -> None:
        """Shut down the underlying CoAP context."""
        await self._protocol.shutdown()

    async def _request(self, code: Any, path: str, payload: bytes = b"") -> bytes:
        request = aiocoap.Message(
            code=code, uri=f"coap://[{self._host}]/{path}", payload=payload
        )
        try:
            response = await self._protocol.request(request).response
        except Exception as err:  # aiocoap mixes its own + plain socket errors
            raise ThingsApiError(
                f"CoAP request to [{self._host}]/{path} failed: {err}"
            ) from err

        if not response.code.is_successful():
            raise ThingsApiError(
                f"Node [{self._host}] returned {response.code} for /{path}"
            )

        return response.payload

    async def get_battery(self) -> dict:
        """GET /battery."""
        payload = await self._request(aiocoap.GET, "battery")
        return json.loads(payload)

    async def send_ir_command(self, cmd: str, **fields: Any) -> None:
        """POST /ir - fire-and-forget AC command, no response payload expected."""
        body = json.dumps({"cmd": cmd, **fields}).encode()
        await self._request(aiocoap.POST, "ir", body)
