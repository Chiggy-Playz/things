# Border router host: native (no Docker), on a Raspberry Pi

This is the currently-active border router setup (as of 2026-08-16), running on
a Raspberry Pi (`iris.local`, mDNS hostname `iris`) instead of Docker Compose
on Omarchy. Same RCP firmware, same Thread dataset, same credentials as the
Docker approach — only the host-side packaging is different.

**End-to-end confirmed working, including LAN→node CoAP commands (`ac
on`/`ac off`) and their responses** — see the two "RESOLVED" sections
below. One of the two fixes (`wlan0` `accept_ra=2` on iris) is currently
**live-only, not yet persisted** — see that section for why and what's
left to do.

## Why native instead of Docker here

The Pi's boot drive is tiny (a ~3.7G USB stick, ~1.1G free at the time). A
from-source `ot-br-posix` build (git clone with submodules — mbedtls alone is
~200MB — plus the actual C++ compile) does not fit. Even the Docker *engine*
itself costs another ~150-300MB just to install, on top of an image.

The actual footprint needed to run this, once you have a working build,
is much smaller than either of those: `otbr-agent` is a 14MB binary, `ot-ctl`
is 71KB, and both link against nothing but `libc`/`libstdc++`/`libgcc_s`/`libm`
— the most baseline libraries on any Linux system. So: build the real
`openthread/border-router` Docker image *elsewhere* (a machine with real
disk/CPU), extract just those two binaries out of it, and run them directly
via systemd on the Pi. No Docker engine on the Pi at all.

## Step 0: build the image elsewhere (not on the Pi)

On a machine with real resources (this was done on a Mac; if it's Apple
Silicon, `--platform linux/arm64` is a *native* build, not emulated, since
Apple Silicon and a 64-bit Pi OS are both `arm64`):

```bash
git clone --recursive --depth=1 https://github.com/openthread/ot-br-posix
cd ot-br-posix
docker buildx build --platform linux/arm64 \
  -t openthread/border-router \
  -f etc/docker/border-router/Dockerfile . --load
```

Check first whether the manual submodule-bump workaround in
[border-router-docker.md](./border-router-docker.md) (for the `keyIdMode`
bug) is even still needed — `ot-br-posix`'s own default submodule pin may
have already moved past it by the time you're reading this (it had, as of
2026-08-14):

```bash
cd third_party/openthread/repo
git fetch origin
git merge-base --is-ancestor 3e6568b609 HEAD && echo "already past the fix"
```

## Step 1: extract the binaries

```bash
docker create --name otbr-inspect openthread/border-router:latest
docker export otbr-inspect > /tmp/otbr.tar
docker rm otbr-inspect

tar -xOf /tmp/otbr.tar usr/sbin/otbr-agent > /tmp/otbr-agent-bin
tar -xOf /tmp/otbr.tar usr/sbin/ot-ctl > /tmp/ot-ctl-bin
chmod +x /tmp/otbr-agent-bin /tmp/ot-ctl-bin
```

(`otbr-web` also exists in the image but is skippable — it's an optional web
UI; Home Assistant sees the border router via `otbr-agent`'s own mDNS
advertisement, not via `otbr-web`.)

**Sanity-check before transferring**: confirm the binary actually runs on the
*target* architecture/libc, don't just trust `ldd` from the build machine —
copy it over and run `ldd`/`--help` on the Pi itself. This matters because
the image is built `FROM ubuntu:24.04`, and the Pi runs a Debian-based OS —
glibc is forward- but not backward-compatible, so a version mismatch would
show up as `GLIBC_2.XX not found` at actual runtime, not at `ldd`-on-the-
build-machine time.

```bash
scp /tmp/otbr-agent-bin iris:~/otbr-agent
scp /tmp/ot-ctl-bin iris:~/ot-ctl
ssh iris '
  sudo mv ~/otbr-agent /usr/local/bin/otbr-agent
  sudo mv ~/ot-ctl /usr/local/bin/ot-ctl
  sudo chmod +x /usr/local/bin/otbr-agent /usr/local/bin/ot-ctl
  ldd /usr/local/bin/otbr-agent
  /usr/local/bin/otbr-agent --help | head -5
'
```

## Step 2: what the image's own entrypoint actually does

Worth knowing this even though we're not running the container — the image's
`ENTRYPOINT ["/init"]` runs a full s6-overlay init, with two real services:
`otbr-agent` and `otbr-web`. Their `run`/`finish` scripts are the ground
truth for what setup is actually needed — no black box, just plain shell:

- **`otbr-agent`'s `run`**: symlinks a data directory, sets up `ipset`/
  `ip6tables` firewall rules and `iptables` NAT64 MASQUERADE (same commands
  already documented in [border-router-docker.md](./border-router-docker.md)'s
  boot-loop-fix section), then execs `otbr-agent` with plain CLI flags.
- **`finish`**: tears down that same firewall state on exit.
- **`otbr-web`**: separate optional process, skipped here (see above).

The native setup below is exactly this, minus the container-volume
indirection (`/data/thread` symlinked to `/var/lib` existed purely to redirect
writes onto Docker's bind-mounted volume — on bare metal, `/var/lib/thread`
is already real persistent host storage, no indirection needed).

## Step 3: small dependency check

```bash
ssh iris 'which ipset iptables ip6tables || sudo apt install -y --no-install-recommends ipset iptables'
```

## Step 4: start/stop scripts

`/usr/local/bin/otbr-agent-start.sh` — **fill in `OT_INFRA_IF` from
`ip -brief link show`, and confirm the RCP device path via `ls /dev/ttyACM*`
after plugging the RCP in** (same two gotchas
[border-router-docker.md](./border-router-docker.md) already documents for
the Docker path — still apply here):

```bash
#!/usr/bin/env bash
set -euo pipefail

OT_THREAD_IF="wpan0"
OT_INFRA_IF="wlan0"          # confirmed via: ip -brief link show
OT_RCP_DEVICE="spinel+hdlc+uart:///dev/ttyACM0?uart-baudrate=460800"
OT_LOG_LEVEL=7
OT_FORWARD_INGRESS_CHAIN="OT_FORWARD_INGRESS"

mkdir -p /var/lib/thread

echo "Configuring OpenThread firewall..."
ipset create -exist otbr-ingress-deny-src hash:net family inet6
ipset create -exist otbr-ingress-deny-src-swap hash:net family inet6
ipset create -exist otbr-ingress-allow-dst hash:net family inet6
ipset create -exist otbr-ingress-allow-dst-swap hash:net family inet6

ip6tables -N "${OT_FORWARD_INGRESS_CHAIN}" 2>/dev/null || true
ip6tables -I FORWARD 1 -o "${OT_THREAD_IF}" -j "${OT_FORWARD_INGRESS_CHAIN}"
ip6tables -A "${OT_FORWARD_INGRESS_CHAIN}" -m pkttype --pkt-type unicast -i "${OT_THREAD_IF}" -j DROP
ip6tables -A "${OT_FORWARD_INGRESS_CHAIN}" -m set --match-set otbr-ingress-deny-src src -j DROP
ip6tables -A "${OT_FORWARD_INGRESS_CHAIN}" -m set --match-set otbr-ingress-allow-dst dst -j ACCEPT
ip6tables -A "${OT_FORWARD_INGRESS_CHAIN}" -m pkttype --pkt-type unicast -j DROP
ip6tables -A "${OT_FORWARD_INGRESS_CHAIN}" -j ACCEPT

echo "Configuring OpenThread NAT64..."
iptables -t mangle -A PREROUTING -i "${OT_THREAD_IF}" -j MARK --set-mark 0x1001
iptables -t nat -A POSTROUTING -m mark --mark 0x1001 -j MASQUERADE
iptables -t filter -A FORWARD -o "${OT_INFRA_IF}" -j ACCEPT
iptables -t filter -A FORWARD -i "${OT_INFRA_IF}" -j ACCEPT

echo "Starting otbr-agent..."
exec /usr/local/bin/otbr-agent \
    --vendor-name "OpenThread" \
    --model-name "BorderRouter" \
    -d"${OT_LOG_LEVEL}" -v -s \
    -I "${OT_THREAD_IF}" \
    -B "${OT_INFRA_IF}" \
    "${OT_RCP_DEVICE}"
```

**Do not add `"trel://${OT_INFRA_IF}"` as a second RadioURL argument here**,
even though the original Docker `run` script this is based on includes it.
This node has zero TREL (Thread Radio Encapsulation over IP) support — it's
a plain 802.15.4-only Zephyr device. It was present in an earlier version
of this script, removed 2026-08-16 as a (disproven) hypothesis for the
LAN→mesh CoAP delivery issue below — removing it didn't fix that issue, but
there's also no reason to keep an unused radio link around, so it stays
out.

## RESOLVED (2026-08-16, early morning): hermes→node CoAP was unreliable

**Symptom**: any CoAP request sent from the LAN (e.g. hermes) toward a node
— `ac on`, `ac off`, a plain `/led` test on `blinky_thread` — sent, retried
4x over ~70-85s (CoAP's own exponential backoff), then gave up with zero
response, every single time tested (5/5 in a row). This was the *inbound*
direction (LAN → mesh); the *outbound* direction (mesh → LAN, e.g. the
node's UDP announce, or its SRP registration) worked completely reliably —
see the two fixes above. This asymmetry was the key clue, but it pointed
at the wrong host initially (iris, not hermes).

**Confirmed NOT the cause** (checked with hard evidence, in this order):
1. App-level code — identical failure on two unrelated, simple CoAP
   handlers (`/ir` and `/led`), rules out anything node-firmware-specific.
2. `ip6tables` policy — `sudo ip6tables -L OT_FORWARD_INGRESS -v -n` shows
   the relevant ACCEPT rule has a nonzero packet counter (traffic *has*
   gotten through this rule at least once), and `sudo ipset list
   otbr-ingress-allow-dst` contains the node's actual OMR prefix.
3. Kernel routing on iris — `ip -6 route get <node-address>` correctly
   resolves to `dev wpan0`.
4. `trel://` radio link — removed entirely (see above), re-tested 5x,
   identical failure every time. Not the (sole) cause.

**The actual root cause**: nothing on iris at all. `tcpdump -i wpan0 -n` on
iris during a failed attempt from hermes showed **zero packets from hermes
ever arriving** — only otbr-agent's own routine `ff02::1` multicast
advertisements, confirming the failure was upstream of iris entirely. The
same test repeated from a Mac on the same LAN worked immediately (LED
turned on), isolating the problem to hermes specifically.

On hermes, `ip -6 route show` had **no route at all** to the node's OMR
prefix (`fd21:6d0:845:1::/64`) — its CoAP packets were going out hermes's
normal internet default gateway and vanishing. Cause:
`net.ipv6.conf.enp2s0.accept_ra_rt_info_max_plen` — the **per-interface**
value, not `conf.all.*` — was `0`, so hermes was ignoring the Route
Information Option in iris's Router Advertisement that says "route this
prefix via me". This is the same sysctl the retired Docker-era notes in
`PROGRESS.md` mention needing (`accept_ra_rt_info_max_plen=64`) — but it
had never actually been (re-)applied for the *new* OMR prefix that came
with the switch to iris, and even when set, only the `all` variant was set
at first, which does **not** propagate to an already-existing interface's
own value on this kernel.

**Fix, applied and persisted on hermes** (not on iris — this whole issue
was hermes-side):
```bash
sudo sysctl -w net.ipv6.conf.enp2s0.accept_ra_rt_info_max_plen=64
echo 'net.ipv6.conf.enp2s0.accept_ra_rt_info_max_plen=64' | sudo tee /etc/sysctl.d/99-ra-route-info.conf
sudo sysctl --system
sudo rdisc6 enp2s0   # forces a fresh RA immediately (needs the `ndisc6` package)
```
Confirmed fixed: `ip -6 route show` on hermes now shows
`fd21:6d0:845:1::/64 via fe80::dea6:32ff:fe0e:ed71 dev enp2s0 proto ra`, and
`ac on`/`ac off`/`/led` color changes from hermes worked 3/3 in a row
(watched the physical LED turn on, off, then red).

**Takeaway for next time**: when LAN→mesh traffic silently fails while
mesh→LAN works, don't assume the border router is the problem just because
it's the newer/more-complex piece — check whether the *client* (whichever
LAN host is initiating) actually has a route to the mesh's OMR prefix at
all (`ip -6 route show | grep <OMR prefix>`) before diving into
`otbr-agent`/`ip6tables`/`wpan0` on the border router side. A `tcpdump` on
the border router's own mesh interface (`wpan0`) settles this fast — if
literally nothing from the client shows up there, the problem is upstream
of the border router, full stop.

**Second part, also RESOLVED the same session**: even with the above
fixed, the CoAP client (`coap-client`/`ac.sh`) initially never received
the ACK back from the node — every test showed retransmissions and a
client-side timeout, despite the command demonstrably executing on the
node every time (LED changes confirmed visually). A `tcpdump -i any -n
port 5683` capture on iris (spanning both `wpan0` and `wlan0` in one
capture) showed the node's ACK arriving on `wpan0` every time but never
going back out `wlan0` — the mesh/node side was fine, the drop was in
iris's own mesh→LAN forwarding.

**Root cause: a direct side effect of the `forwarding=1` fix above.**
Enabling `net.ipv6.conf.all.forwarding=1` makes Linux treat the interface
as a router, and a router's default `accept_ra=1` means "accept Router
Advertisements only if forwarding is disabled" — so the moment forwarding
was turned on, `wlan0` silently stopped being able to (re-)learn a global
address/route from the real LAN router via RA. It didn't fail
immediately (iris kept using an address/route learned *before*
forwarding was enabled — explaining why the UDP-announce test passed
right after that fix), only once that old lease expired with nothing
renewing it, leaving `wlan0` with just a link-local address:
`ip -6 route get <hermes-address>` → `Network is unreachable`.

**Fix — confirmed working, currently LIVE-ONLY, deliberately NOT YET
PERSISTED** (holding off to watch for further side effects, given this
bug was itself a side effect of the previous fix):
```bash
sudo sysctl -w net.ipv6.conf.wlan0.accept_ra=2
sudo rdisc6 wlan0   # forces a fresh RA immediately (needs `ndisc6`)
```
`accept_ra=2` means "accept RAs even though forwarding is enabled". After
this, `wlan0` picked up real global addresses and a default route, and a
full CoAP round-trip from hermes (`ac off` / `ac on` equivalent) came back
clean — `t:ACK c:2.04`, exit code `0`, no retransmissions, confirmed twice
in a row with the LED physically toggling both times. Re-verified the
original forwarding fix wasn't itself broken by this: `forwarding` still
`1` on `all`/`wpan0`/`wlan0`, `OT_FORWARD_INGRESS`'s ACCEPT rule still
counting packets, LED still toggles reliably.

**To persist** (not yet done — do this once satisfied there are no other
side effects):
```bash
echo 'net.ipv6.conf.wlan0.accept_ra=2' | sudo tee -a /etc/sysctl.d/99-thread-forwarding.conf
sudo sysctl --system
```
Also add `sysctl -w net.ipv6.conf.wlan0.accept_ra=2` next to the existing
`forwarding=1` line in `otbr-agent-start.sh`'s defense-in-depth block —
otherwise every `otbr-agent` restart/reboot re-applies `forwarding=1`
fresh and re-triggers this exact same bug (with the same delayed-onset
behavior, once whatever address/route was learned in the meantime
eventually expires again).

**Worth knowing, not necessarily a problem on this home network**:
`wlan0` now holds a real global IPv6 address, and iris's `ip6tables
INPUT` chain is default `ACCEPT` with zero rules — iris is more directly
internet-reachable than it was before this session. Also has a real
default route out `wlan0` now, which it effectively didn't have before.

Full investigation detail and exact commands used: see `PROGRESS.md` at the
repo root.

**Gotcha**: `--vendor-name`/`--model-name` are required — `otbr-agent` exits
immediately with `Vendor name must be set.` without them. Easy to miss
because `otbr-agent --help | head -5` truncates before showing these two
flags; read the full `--help` output, not just the first few lines.

`/usr/local/bin/otbr-agent-stop.sh`:

```bash
#!/usr/bin/env bash
OT_THREAD_IF="wpan0"
OT_FORWARD_INGRESS_CHAIN="OT_FORWARD_INGRESS"

while ip6tables -C FORWARD -o "${OT_THREAD_IF}" -j "${OT_FORWARD_INGRESS_CHAIN}" 2>/dev/null; do
    ip6tables -D FORWARD -o "${OT_THREAD_IF}" -j "${OT_FORWARD_INGRESS_CHAIN}"
done
if ip6tables -L "${OT_FORWARD_INGRESS_CHAIN}" 2>/dev/null; then
    ip6tables -F "${OT_FORWARD_INGRESS_CHAIN}"
    ip6tables -X "${OT_FORWARD_INGRESS_CHAIN}"
fi
for s in otbr-ingress-deny-src otbr-ingress-deny-src-swap otbr-ingress-allow-dst otbr-ingress-allow-dst-swap; do
    ipset destroy "$s" 2>/dev/null || true
done
echo "OpenThread firewall rules removed."
```

```bash
sudo chmod +x /usr/local/bin/otbr-agent-start.sh /usr/local/bin/otbr-agent-stop.sh
```

## Step 4.5: enable IPv6 forwarding (easy to miss, silently breaks all LAN↔mesh traffic)

**Found 2026-08-16, after a long debugging session.** Thread-level attach
(MLE, child table, `Mle-----------:` log lines) works completely fine
without this — the node shows up as an attached child, everything *looks*
healthy. What silently breaks is anything that needs to actually cross
between the Thread mesh (`wpan0`) and the LAN (`wlan0`) as real IP traffic —
the node's UDP announce to hermes, CoAP commands from hermes to the node,
etc. `ip6tables` FORWARD policy being `ACCEPT` (confirmed via `sudo
ip6tables -L FORWARD -v -n`) is **not sufficient on its own** — if the
kernel itself isn't forwarding, packets get dropped before ever reaching
the firewall logic. Check the actual flag directly, don't trust the
firewall rules alone:

```bash
cat /proc/sys/net/ipv6/conf/all/forwarding   # 0 = broken, 1 = fine
```

Symptom this produces: the border router's own log clearly shows a packet
*arriving* from the node (`MeshForwarder-: Received IPv6 UDP msg ...
dst:[some-LAN-address]:5555`), but no corresponding "Sent" line, and nothing
ever lands on the LAN-side destination. Confusingly, the Pi *can* reach that
same LAN address fine on its own (e.g. a plain `ping` from the Pi itself
succeeds) — that's the Pi's own outbound traffic, a completely different
path from forwarding *between* two of its interfaces.

**Why this wasn't already handled**: the original Docker image's `run`
script (see `border-router-docker.md`) never sets this either — it must
have relied on the Docker host (Omarchy, in the old setup) already having
it enabled some other way (Docker frequently touches host-level forwarding
sysctls as a side effect of its own networking setup). Running bare-metal on
a fresh Raspberry Pi OS install, nothing ever turns this on — Debian ships
with it off by default.

Fix, both persistent and self-healing:

```bash
sudo tee /etc/sysctl.d/99-thread-forwarding.conf > /dev/null <<'EOF'
net.ipv6.conf.all.forwarding=1
EOF
sudo sysctl --system
```

Also added directly into `otbr-agent-start.sh` (right before the firewall
setup) as a defense-in-depth safety net, in case the sysctl.d file is ever
missing on a fresh setup:

```bash
sysctl -w net.ipv6.conf.all.forwarding=1 >/dev/null
```

## Step 5: systemd unit

```bash
sudo tee /etc/systemd/system/otbr-agent.service > /dev/null <<'EOF'
[Unit]
Description=OpenThread Border Router agent (native, no Docker)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=/usr/local/bin/otbr-agent-start.sh
ExecStopPost=/usr/local/bin/otbr-agent-stop.sh
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable --now otbr-agent
```

`enable` (not just `start`) is what makes this survive a reboot — confirmed
by the symlink it creates:
`/etc/systemd/system/multi-user.target.wants/otbr-agent.service`.

## Step 6: apply the dataset

Same credentials, same commands as
[thread-dataset.md](./thread-dataset.md) — just run as `sudo`:

**Gotcha**: `ot-ctl` talks to `otbr-agent` over a Unix socket at
`/run/openthread-wpan0.sock`, created root-owned with no write access for
other users (`srwxr-xr-x`). Plain `ot-ctl` as a non-root user fails with
`connect session failed: Permission denied` — always run it via `sudo`,
matching how `otbr-agent` itself runs as root.

```bash
sudo ot-ctl thread stop
sudo ot-ctl ifconfig down
sudo ot-ctl dataset init new
sudo ot-ctl dataset channel 15
sudo ot-ctl dataset panid 0x173a
sudo ot-ctl dataset extpanid 2c66561f1a0734af
sudo ot-ctl dataset networkname OpenThread
sudo ot-ctl dataset networkkey 86e99d7a56d33605c087658af3d90cdc
sudo ot-ctl dataset pskc 0c8879e3eaa0f26fbd4b47f6fabe0045
sudo ot-ctl dataset meshlocalprefix fd00:db8:a0:0::
sudo ot-ctl dataset commit active
sudo ot-ctl ifconfig up
sudo ot-ctl thread start
sudo ot-ctl state    # "detached" immediately after is normal, takes ~20-30s to reach "leader"
```

## Persistence across reboots

Both the service (`systemctl enable`) and the dataset (`otbr-agent` writes
its settings to `/var/lib/thread` — real persistent storage, not a tmpfs —
and defaults to `--auto-attach=1`) survive a reboot with no manual steps.
Confirmed by testing: after `sudo reboot`, `otbr-agent` came back up and
re-formed as leader on its own.

## Why the mDNS name Home Assistant shows isn't `iris.local`

Home Assistant will show something like `OpenThread BorderRouter #D68E` at
`otf68dd64d93b0d68e.local.` — **not** `iris.local`, even though it's the same
machine. This is expected: `otbr-agent` advertises its Border Agent service
under its own separate identity, derived from OpenThread's internally
generated Extended Address (`sudo ot-ctl extaddr`), not the host's own
mDNS hostname. Confirmed directly: `ot-ctl extaddr` returns exactly
`f68dd64d93b0d68e`, matching both the hostname and the `#D68E` discriminator.
This is deliberate Border Agent design (a stable identity independent of
whatever host it happens to run on) and it stays stable across reboots since
it's persisted, not regenerated.

## Portability notes carried over from the Docker doc

Same two gotchas, still apply here, still worth re-checking on any future
machine rather than assuming:
1. `OT_INFRA_IF` — via `ip -brief link show`.
2. RCP device path — via `ls /dev/ttyACM*`.
