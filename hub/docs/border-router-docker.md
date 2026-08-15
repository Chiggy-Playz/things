# Border router host: Docker Compose setup

## Use `openthread/border-router`, not `openthread/otbr`

These are two different Docker Hub images and it's easy to grab the wrong
one:

- **`openthread/otbr`** — explicitly a **testing/CI image**. Its own
  Docker Hub description says "not intended or optimized for real-world or
  production use." Its entrypoint (`/app/etc/docker/test/docker_entrypoint.sh`)
  reads different environment variable names entirely (`RADIO_URL`,
  `TUN_INTERFACE_NAME`, `BACKBONE_INTERFACE`, `DEBUG_LEVEL`), defaults to
  `/dev/ttyUSB0`, and — as of this writing — is what actually gets built
  for **every architecture** (amd64/arm64/armv7 all shared the same
  entrypoint when checked via `docker buildx imagetools inspect`, so this
  isn't an architecture-specific quirk).
- **`openthread/border-router`** — the actual production image. Documented
  at <https://openthread.io/guides/border-router/build-docker>. Uses
  `OT_RCP_DEVICE`, `OT_INFRA_IF`, `OT_THREAD_IF`, `OT_LOG_LEVEL`.

If you ever see logs mentioning `RADIO_URL` when you configured
`OT_RCP_DEVICE`, you're on the wrong image.

## `docker-compose.yml`

```yaml
services:
  otbr:
    image: openthread/border-router:latest
    container_name: otbr
    network_mode: host
    cap_add:
      - NET_ADMIN
    devices:
      - /dev/ttyACM0:/dev/ttyACM0
      - /dev/net/tun:/dev/net/tun
    volumes:
      - /var/lib/otbr:/data
    env_file:
      - otbr-env.list
    restart: unless-stopped
```

`network_mode: host` is required — it's how the container reaches the real
LAN interface for mDNS/infra routing, and how `OT_INFRA_IF` resolves to a
real host interface at all.

## `otbr-env.list`

```
OT_RCP_DEVICE=spinel+hdlc+uart:///dev/ttyACM0?uart-baudrate=460800
OT_INFRA_IF=<your real LAN interface — see gotcha below>
OT_THREAD_IF=wpan0
OT_LOG_LEVEL=7
```

**Gotcha: don't assume the interface name.** Every generic example
(including the official docs) uses `wlan0`. Check what your actual machine
calls its LAN-facing interface before assuming:

```bash
ip -brief link show
```

On a wired Debian/Arch box this is very commonly something like `enp12s0`,
not `wlan0`. If `OT_INFRA_IF` points at a nonexistent or dead interface,
everything that needs to reach the real LAN (mDNS advertisement, NAT64,
on-link routing) fails silently — the border router runs, forms a Thread
network, does everything internally correctly, and simply never becomes
visible to anything outside itself. This produces no obvious error; you
just never see it in Home Assistant and have to work backwards to realize
the infra interface was wrong the whole time.

## Real bug: `SetMacKey` / `keyIdMode` version-gap crash

Symptom:

```
otPlatRadioSetMacKey() at radio.cpp:964: InvalidArgument
```

immediately on `otbr-agent` startup, every time, even with a completely
fresh dataset.

**Root cause** (confirmed by reading the actual upstream commits, not
guessed): OpenThread's core MAC layer changed on **2026-07-24**
(`6ff0e4ed95`, PR #13380) to send the literal value `1` for key-ID-mode
instead of the old bit-shifted `(1 << 3) = 8`. `ot-br-posix`'s pinned
OpenThread submodule was bumped to a commit downstream of that change on
**2026-08-04**, but the compatibility shim that translates it back to `8`
for older RCP firmware (`3e6568b609`, PR #13474,
`OPENTHREAD_SPINEL_CONFIG_RCP_KEY_ID_MODE_CHECK_COMPATIBILITY_WORKAROUND_ENABLE`)
didn't land until **2026-08-06** — two days later. Any `openthread/border-router`
image built from a commit in that two-day gap will crash against any RCP
that still does the old strict `keyIdMode == 8` check (which is normal,
correct RCP behavior for anything built before the API change — this isn't
specific to our ESP32-C6 build).

**Fix used: build the image from source, with the submodule bumped past
the fix**, rather than patching RCP firmware (patching the RCP was
considered and explicitly rejected — don't go there again without a new
conversation about it):

```bash
git clone --recursive --depth=1 https://github.com/openthread/ot-br-posix
cd ot-br-posix/third_party/openthread/repo
git fetch origin
git checkout 3e6568b609   # the Aug 6 2026 fix commit, or anything after it
cd ../../..
docker build --no-cache -t openthread/border-router -f etc/docker/border-router/Dockerfile .
```

Notes on this build:

- The Dockerfile only does a fresh `git clone` from GitHub when
  `GIT_COMMIT` is overridden away from its default (`HEAD`) or the local
  copy is missing — with default args it does `COPY . /usr/src/ot-br-posix-local`
  and builds from your local checkout, so the manual submodule bump above
  actually gets used.
- The build tag `openthread/border-router` (no `:local` suffix) matches
  exactly what `docker-compose.yml` already references — Docker prefers a
  locally-cached image over pulling, so no compose file changes are needed
  after rebuilding.
- **This local git checkout state doesn't affect the running container
  once built** — the image is a self-contained set of compiled layers with
  no ongoing link back to the clone. It only matters if you rebuild later:
  a `git pull`/fresh `git submodule update --init --recursive` on that
  clone could silently reset the submodule back to the buggy pin, so
  redo the manual checkout above on any future rebuild until upstream
  `ot-br-posix` itself bumps its default pin past `3e6568b609`.

## Startup crash-loop from leftover host `ipset`/`iptables` state

Because of `network_mode: host`, the firewall rules `otbr-agent` creates
(`ip6tables`/`ipset`, chain `OT_FORWARD_INGRESS` or `OTBR_FORWARD_INGRESS`
depending on image) live directly in the **host's** kernel, not the
container's namespace. If a previous container instance didn't get to run
its own shutdown cleanup (crashed, was force-killed), the next start's own
cleanup step fails (`ipset ... Set cannot be destroyed: it is in use by a
kernel component`) because a leftover `iptables` rule is still referencing
it — which crashes the new container immediately, which `restart:
unless-stopped` then retries forever, looping.

Fix — purge stale state directly on the host (not inside the container),
in the right order (remove the referencing rule before destroying the
ipset it references):

```bash
docker compose down

sudo ip6tables -D FORWARD -o wpan0 -j OTBR_FORWARD_INGRESS 2>/dev/null
sudo ip6tables -F OTBR_FORWARD_INGRESS 2>/dev/null
sudo ip6tables -X OTBR_FORWARD_INGRESS 2>/dev/null
sudo ipset destroy otbr-ingress-deny-src 2>/dev/null
sudo ipset destroy otbr-ingress-deny-src-swap 2>/dev/null
sudo ipset destroy otbr-ingress-allow-dst 2>/dev/null
sudo ipset destroy otbr-ingress-allow-dst-swap 2>/dev/null

docker compose up
```

## Why `avahi-browse` might show nothing even when everything is working

`openthread/border-router` doesn't bundle `avahi-daemon` at all (confirmed
via `docker exec otbr which avahi-daemon` returning nothing) — it uses
`otbr-agent`'s own built-in mDNS publisher (`OTBR_MDNS=openthread` build
default), which binds UDP 5353 directly rather than going through
D-Bus/avahi. `ss -ulnp | grep 5353` inside the container confirms it's
bound and listening even when nothing is visible externally yet.

Also worth knowing: `ot-ctl ifconfig up` alone (without a committed
dataset) starts OpenThread's internal `DnssdServer`, which sends mDNS-style
traffic — but **over the Thread radio** (`MeshForwarder` frames to
`ff02::fb`, link-local scope on the Thread interface), not onto the LAN.
This is a completely different component from the border agent's
LAN-facing `_meshcop._udp` advertisement, and seeing this traffic in the
logs does not mean HA-visible mDNS is working. The border agent's own
LAN advertisement requires a real, fully-formed active dataset (see
[thread-dataset.md](./thread-dataset.md)) — `ifconfig up` alone isn't
enough.

## Portability

Everything above is standard Docker Compose on a normal Linux host — it
carries over unchanged to any other Linux+Docker machine, including
running it on the same box as Home Assistant (fine, as long as HA is
Container/Supervised on a normal OS and not the locked-down HAOS
appliance, which manages its own Docker and doesn't give you free-form
`docker compose` access the same way). Two things to re-check on any new
machine, both because they've already caused real problems once:

1. `OT_INFRA_IF` — confirm via `ip -brief link show`, don't assume `wlan0`.
2. The RCP's device path — confirm via `ls /dev/ttyACM*` after plugging it
   in, don't assume `/dev/ttyACM0` (though it usually is).
