# Deploying the byteme2 side

The fob needs one plain-HTTP JSON endpoint on the LAN, because an ESP8266 doing
TLS costs ~20 KB of its ~44 KB free heap per connection and the fob needs four
sources. `fob-api` aggregates them and serves ~3.5 KB of JSON.

## fob-api (on byteme2, 192.0.2.2:8788)

Read-only, stdlib-only, no pip on the hypervisor. Runs *alongside*
`ups-dashboard.service` — it does not modify it.

**Updates deploy on push.** byteme2 runs autodeploy (see `homelab-tools/autodeploy`): within about two
minutes of a push to `master`, a changed `fob_api.py` or `fob-api.service` is copied into place and
`fob-api` restarts (`autodeploy.json` at the repo root). The commands below are only for a fresh install.

    scp fob_api.py      root@192.0.2.2:/opt/fob-api/fob_api.py
    scp fob-api.service root@192.0.2.2:/etc/systemd/system/
    ssh root@192.0.2.2 'systemctl daemon-reload && systemctl enable --now fob-api'

The WeatherFlow token goes in `/etc/fob-api/tempest.token` (root, 0600). Pipe it
so it never appears in a command line:

    cat ~/.config/tempest/token | ssh root@192.0.2.2 \
      'install -D -m 600 /dev/stdin /etc/fob-api/tempest.token'

Endpoints:

| Path | Does |
|---|---|
| `/fob.json` | everything the fob renders (~3.5 KB) |
| `/replay` | bumps a counter; the fob replays the last real run |
| `/replay/pos?i=&n=` | the dashboard reports its replay frame so the fob follows it |
| `/health` | `{"ok":1}` |

Aggregates: Proxmox via `pvesh get /cluster/resources`, the UPS dashboard API on
localhost:8787, and the WeatherFlow cloud (the Tempest hub's local UDP broadcast
on :50222 was unreachable from every segment tested — desktop, the fob itself,
wazuh and cryoai all saw nothing).

## Dashboard patch (optional)

Three additive lines in `/opt/ups-dashboard/static/settings.js` make the
dashboard's "Replay last real run" button also drive the fob, and one more
reports its frame position so the two screens stay in step. Both are wrapped so
a missing fob cannot affect the dashboard. `settings.html`'s cache-buster must be
bumped or browsers keep the old file. See `settings.js.patch`.

Backup left on the host: `settings.js.bak-before-fob-2026-09-20`.
