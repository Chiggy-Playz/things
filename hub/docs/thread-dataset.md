# Thread dataset: matching the border router to the nodes

The Zephyr nodes in `node/lib/thread/thread.c` have a **hardcoded** Thread
dataset baked into firmware. Sleepy End Devices (SEDs) generally can't form
a network on their own — they need a router/leader-capable device to form
it first. That's the border router's job: it needs the exact same dataset
configured, or the nodes have nothing to attach to.

## The credentials (must match `node/lib/thread/thread.c` exactly)

| Field | Value |
|---|---|
| Channel | 15 |
| PAN ID | `0x173A` |
| Network name | `OpenThread` |
| Extended PAN ID | `2c66561f1a0734af` |
| Network key | `86e99d7a56d33605c087658af3d90cdc` |
| PSKc | `0c8879e3eaa0f26fbd4b47f6fabe0045` |
| Mesh-local prefix | `fd00:db8:a0:0::/64` |

If these two docs (this file and `thread.c`) ever disagree, `thread.c` is
the source of truth — update this file to match, not the other way around.

## Gotcha: `dataset clear` + individual fields ≠ a valid dataset

Setting fields one at a time after `dataset clear` produces a dataset
**missing the Active Timestamp and Security Policy** that `dataset init
new` normally auto-populates. Without a valid active timestamp, the device
doesn't confidently form a new partition as leader — it instead loops
trying to *attach* to a network it can never find, showing symptoms like:

```
Attach attempt 5 failed
attachstate idle -> start
```

repeating indefinitely, with `ot-ctl state` stuck on `detached`.

**Fix**: start from `ot-ctl dataset init new` (fills in a complete, valid
dataset with a fresh timestamp and sane security policy), then override
only the specific fields that need to match the nodes:

```
ot-ctl thread stop
ot-ctl ifconfig down
ot-ctl dataset init new
ot-ctl dataset channel 15
ot-ctl dataset panid 0x173a
ot-ctl dataset extpanid 2c66561f1a0734af
ot-ctl dataset networkname OpenThread
ot-ctl dataset networkkey 86e99d7a56d33605c087658af3d90cdc
ot-ctl dataset pskc 0c8879e3eaa0f26fbd4b47f6fabe0045
ot-ctl dataset meshlocalprefix fd00:db8:a0:0::
ot-ctl dataset commit active
ot-ctl ifconfig up
ot-ctl thread start
ot-ctl state    # should show "leader"
```

## Persistence across restarts

The border router's `otbr-agent` service script does this on every start:

```bash
mkdir -p /data/thread && ln -sft /var/lib /data/thread
```

`/data` is the bind-mounted `/var/lib/otbr` from the host (see
`docker-compose.yml`'s `volumes:`). This means the dataset set above
**already persists across plain container restarts and recreations** for
free, as long as `/var/lib/otbr` on the host isn't deleted and you don't
run `docker compose down -v`. Confirm with:

```bash
docker compose restart otbr
sleep 3
docker exec otbr ot-ctl dataset active
```

## Config-as-code: reproducing this on a fresh volume or new machine

`otbr-agent` doesn't take a dataset as a startup flag or read one from an
environment variable — the only way to seed it is imperatively via
`ot-ctl`/the REST API after the daemon is already running. For a
fresh/wiped volume or a new machine, use these two files (not yet
committed to the repo — copy them alongside `docker-compose.yml` wherever
the border router actually runs):

`dataset.conf`:
```ini
CHANNEL=15
PANID=0x173a
EXTPANID=2c66561f1a0734af
NETWORKNAME=OpenThread
NETWORKKEY=86e99d7a56d33605c087658af3d90cdc
PSKC=0c8879e3eaa0f26fbd4b47f6fabe0045
MESHLOCALPREFIX=fd00:db8:a0:0::
```

`apply-dataset.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/dataset.conf"

CONTAINER=otbr
otctl() { docker exec "$CONTAINER" ot-ctl "$@"; }

echo "Waiting for otbr-agent to respond..."
until otctl state >/dev/null 2>&1; do
    sleep 1
done

# Idempotency check — skip if this exact network is already active.
active_extpanid=$(otctl dataset extpanid 2>/dev/null | tr -d '[:space:]' || true)
if [ "$active_extpanid" = "$EXTPANID" ]; then
    echo "Dataset already matches (extpanid $EXTPANID) — nothing to do."
    exit 0
fi

echo "Applying dataset..."
otctl thread stop || true
otctl ifconfig down || true
otctl dataset init new
otctl dataset channel "$CHANNEL"
otctl dataset panid "$PANID"
otctl dataset extpanid "$EXTPANID"
otctl dataset networkname "$NETWORKNAME"
otctl dataset networkkey "$NETWORKKEY"
otctl dataset pskc "$PSKC"
otctl dataset meshlocalprefix "$MESHLOCALPREFIX"
otctl dataset commit active
otctl ifconfig up
otctl thread start

echo "Done. State: $(otctl state)"
```

Usage: `chmod +x apply-dataset.sh && ./apply-dataset.sh` — run once after
`docker compose up -d`. Safe to re-run anytime (fresh volume, new
hardware, manual dataset wipe) since it no-ops if the ext PAN ID already
matches.

Tradeoff vs. a raw TLV hex blob (`ot-ctl dataset active -x`): the
field-by-field approach above is reviewable in a git diff ("channel
changed from 15 to 20"); a TLV blob is a single opaque hex string — more
compact and exactly what a real commissioner/QR-code flow expects, but not
something you'd want as the checked-in source of truth. Grab the TLV form
on-demand from a working border router (`ot-ctl dataset active -x`) only
when something else specifically needs that wire format.
