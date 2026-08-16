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
import json
import sys

from smpclient import SMPClient
from smpclient.requests.image_management import ImageStatesRead, ImageStatesWrite
from smpclient.requests.os_management import ResetWrite
from smpclient.transport.udp import SMPUDPTransport

# 1500 (the smpclient/SMPUDPTransport default) assumes a full, unfragmented
# Ethernet-sized path all the way to the node. That's not this path - real
# node traffic crosses a Thread/6LoWPAN mesh, whose effective link sizes sit
# well under that, and IPv6 itself only *guarantees* 1280 on any link. A
# too-large MTU here doesn't degrade gracefully - it makes the outbound
# socket send() fail outright (EMSGSIZE, "Message too long") before the
# packet leaves the sending machine, on the very first upload chunk.
UDP_MTU = 1200


def to_jsonable(obj):
    """Recursively convert a pydantic response (or dict/list) into something
    json.dumps can handle - bytes fields (e.g. an image hash) come back as
    raw binary, not text, and aren't valid UTF-8, so the naive
    model_dump_json() crashes on them. Hex-encode bytes instead of trying to
    decode them as a string."""
    if hasattr(obj, "model_dump"):
        obj = obj.model_dump()
    if isinstance(obj, bytes):
        return obj.hex()
    if isinstance(obj, dict):
        return {k: to_jsonable(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [to_jsonable(v) for v in obj]
    return obj


def print_resp(resp) -> None:
    sys.stdout.write(json.dumps(to_jsonable(resp), indent=2) + "\n")
    sys.stdout.flush()


async def run(addr: str, cmd: str, args: argparse.Namespace) -> None:
    client = SMPClient(SMPUDPTransport(mtu=UDP_MTU), addr)
    await client.connect()
    try:
        if cmd == "upload":
            # upload_file() (a prior version of this script used it) goes
            # through file management (fs_mgmt, SMP group 8) - generic
            # arbitrary-file transfer to a device filesystem, which our
            # firmware never enabled and correctly rejects (ENOTSUP). upload()
            # is the actual firmware-image path: ImageUploadWrite, SMP group 1
            # (image management, CONFIG_MCUMGR_GRP_IMG - the one we enabled).
            # client.upload()'s subsequent-chunk timeout defaults to the
            # client's general 2.5s timeout, which is fine for a plain
            # request/response like `list` but too tight for real chunks
            # over a Thread mesh (radio round trip + an actual flash write
            # on the device for each chunk) - only the *first* chunk gets a
            # generous default (40s). Give every chunk the same generous
            # budget, not just the first.
            with open(args.file, "rb") as f:
                data = f.read()
            async for offset in client.upload(data, first_timeout_s=40.0, subsequent_timeout_s=40.0):
                print(f"\r{offset}/{len(data)} bytes", end="", flush=True)
            print()
        elif cmd == "list":
            resp = await client.request(ImageStatesRead())
            print_resp(resp)
        elif cmd == "test":
            resp = await client.request(
                ImageStatesWrite(hash=bytes.fromhex(args.hash), confirm=False)
            )
            print_resp(resp)
        elif cmd == "confirm":
            h = bytes.fromhex(args.hash) if args.hash else None
            resp = await client.request(ImageStatesWrite(hash=h, confirm=True))
            print_resp(resp)
        elif cmd == "reset":
            resp = await client.request(ResetWrite())
            print_resp(resp)
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
