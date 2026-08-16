#!/usr/bin/env python3
# /// script
# requires-python = ">=3.12"
# dependencies = [
#     "smpclient>=7.3.0",
# ]
# ///
"""OTA helper for Things nodes over Thread/UDP.

Exists because neither of the "obvious" mcumgr clients speak our transport:
`nrfutil mcu-manager` only does serial/BLE, and the Apache mcumgr CLI needs a
Go toolchain. `smpclient` (pure Python, pulled in on demand via `uv run
--with smpclient`) has a UDP transport and needs neither.

Usage (run from node/, via uv so smpclient doesn't need a separate install):

  uv run --with smpclient tools/ota.py <addr> upload <path/to/zephyr.signed.bin>
  uv run --with smpclient tools/ota.py <addr> list
  uv run --with smpclient tools/ota.py <addr> test <hash-hex>
  uv run --with smpclient tools/ota.py <addr> confirm [<hash-hex>]
  uv run --with smpclient tools/ota.py <addr> reset

<addr> is the node's Thread mesh/OMR IPv6 address (no brackets, no port -
port 1337 is hardcoded to match CONFIG_MCUMGR_TRANSPORT_UDP_PORT).

Full update flow: stay_awake CoAP command first (not this tool - see
node/lib/thread/thread.c's /stay_awake endpoint), then upload, list (to
read back the hash), test <hash>, reset, smoke-test over CoAP, confirm.
"""

import argparse
import asyncio
import sys

from smpclient import SMPClient
from smpclient.requests.image_management import ImageStatesRead, ImageStatesWrite
from smpclient.requests.os_management import ResetWrite
from smpclient.transport.udp import SMPUDPTransport


async def run(addr: str, cmd: str, args: argparse.Namespace) -> None:
    client = SMPClient(SMPUDPTransport(), addr)
    await client.connect()
    try:
        if cmd == "upload":
            with open(args.file, "rb") as f:
                data = f.read()
            async for offset in client.upload_file(data, args.file):
                print(f"\r{offset}/{len(data)} bytes", end="", flush=True)
            print()
        elif cmd == "list":
            resp = await client.request(ImageStatesRead())
            print(resp.model_dump_json(indent=2))
        elif cmd == "test":
            resp = await client.request(
                ImageStatesWrite(hash=bytes.fromhex(args.hash), confirm=False)
            )
            print(resp.model_dump_json(indent=2))
        elif cmd == "confirm":
            h = bytes.fromhex(args.hash) if args.hash else None
            resp = await client.request(ImageStatesWrite(hash=h, confirm=True))
            print(resp.model_dump_json(indent=2))
        elif cmd == "reset":
            resp = await client.request(ResetWrite())
            print(resp.model_dump_json(indent=2))
    finally:
        await client.disconnect()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("addr", help="node's Thread IPv6 address")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_upload = sub.add_parser("upload")
    p_upload.add_argument("file")

    sub.add_parser("list")

    p_test = sub.add_parser("test")
    p_test.add_argument("hash")

    p_confirm = sub.add_parser("confirm")
    p_confirm.add_argument("hash", nargs="?", default=None)

    sub.add_parser("reset")

    args = parser.parse_args()
    asyncio.run(run(args.addr, args.cmd, args))


if __name__ == "__main__":
    sys.exit(main())
