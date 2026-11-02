#!/usr/bin/env python3
"""fob-api - one compact JSON for the ESP8266 homelab fob.

READ-ONLY. Polls Proxmox, the local UPS dashboard API and the WeatherFlow cloud
in a background thread and serves the cached result, so the fob never waits on a
slow source and nothing here can block or mutate the hypervisor.

Deliberately stdlib-only: no pip on a production hypervisor.
"""
import json, os, re, subprocess, threading, time, urllib.request, urllib.error
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT        = 8788
POWER_BASE  = "http://127.0.0.1:8787"
TOKEN_FILE  = "/etc/fob-api/tempest.token"
STATION_ID  = "217205"

state = {"t": 0, "ups": {}, "gd": {}, "host": {}, "vms": [], "cts": [],
         "wx": {}, "run": {"active": 0, "kind": "none", "phase": "", "tl": [], "lines": []},
         # The last REAL run's timeline, kept separate and always present. A dry
         # run finishes in ~7s and carries no timeline at all, so if the fob had
         # to share one slot the dry run would erase the only thing worth
         # replaying. "req" is bumped by GET /replay to trigger one on demand.
         "rep": {"req": 0, "n": 0, "ec": 0, "st": 0, "fin": 0, "tl": []},
         # A graphical outage sequence: which guest stopped when, plus phase
         # markers. Derived from the run log so the fob can animate the rack
         # draining instead of listing stage labels.
         "seq": {"kind": "none", "dur": 0, "budget": 240, "live": 0,
                 "guests": [], "stages": [], "lo": [], "nl": 0},
         # Position reported by the dashboard's own replay, so the fob follows
         # the browser rather than running a second, drifting clock.
         "pos": {"i": 0, "n": 0, "ts": 0},
         # Weather for the standalone weather fob, served on its own endpoint so
         # that board parses ~1.5KB instead of the whole homelab payload.
         "wx2": {}, "wxhist": {"t": [], "p": []}}
lock = threading.Lock()


def get_json(url, timeout=6, token=None):
    req = urllib.request.Request(url)
    if token:
        req.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf8", "replace"))


def read_token():
    try:
        with open(TOKEN_FILE) as f:
            return f.read().strip()
    except OSError:
        return None


def poll_power():
    try:
        s = get_json(POWER_BASE + "/api/status")
    except Exception:
        return
    u, g, h = s.get("ups", {}), s.get("guardian", {}), s.get("host", {})
    rated = u.get("rated_va") or 0
    load = u.get("load") or 0
    with lock:
        state["ups"] = {
            "st": u.get("state"), "src": u.get("source"),
            "iv": u.get("input_voltage"), "ov": u.get("output_voltage"),
            "b": u.get("battery"), "rt": u.get("runtime_min"), "ld": load,
            "pct": round(load / rated * 100, 1) if rated else None,
            "ol": 1 if u.get("online") else 0, "cn": 1 if u.get("connected") else 0,
            "mdl": u.get("model"), "va": rated, "le": u.get("last_event"),
        }
        state["gd"] = {"mode": g.get("mode"),
                       "pf": 1 if g.get("powerfail_active") else 0}
        state["host"] = {"l1": h.get("load1"), "mem": h.get("memory_pct"),
                         "tmp": h.get("temperature"), "up": h.get("uptime"),
                         "cpu": h.get("cpu_count")}
        state["t"] = int(time.time())


def poll_run():
    """Mirror a dry run or a real recovery test while it is in flight."""
    best = None
    for kind, url in (("real", "/api/realtest/last"), ("dry", "/api/dryrun")):
        try:
            r = get_json(POWER_BASE + url, timeout=4)
        except Exception:
            continue
        running = bool(r.get("running"))
        started = r.get("started") or 0
        cand = {
            "active": 1 if running else 0, "kind": kind,
            "phase": r.get("phase") or ("running" if running else "idle"),
            "st": started, "fin": r.get("finished"), "ec": r.get("exit_code"),
            "tl": [{"k": e.get("key"), "l": e.get("label"), "ts": e.get("ts")}
                   for e in (r.get("timeline") or [])][-12:],
            "lines": [str(x)[:70] for x in (r.get("lines") or [])][-6:],
        }
        # A live run always wins; otherwise keep whichever finished most recently.
        if best is None or cand["active"] > best["active"] or \
           (cand["active"] == best["active"] and started > (best.get("st") or 0)):
            best = cand
    if best:
        with lock:
            state["run"] = best


def poll_replay():
    """The stored real recovery test, with each stage's offset from run start."""
    try:
        r = get_json(POWER_BASE + "/api/realtest/last", timeout=6)
    except Exception:
        return
    started = r.get("started") or 0
    tl = r.get("timeline") or []
    out = []
    for e in tl:
        ts = e.get("ts") or 0
        out.append({"l": (e.get("label") or "")[:33],
                    "o": max(0, ts - started) if (ts and started) else 0})
    with lock:
        state["rep"].update({"n": len(out), "ec": r.get("exit_code") or 0,
                             "st": started, "fin": r.get("finished") or 0,
                             "tl": out})


GUEST_STOP = re.compile(r"\b(CT|VM)\s+(\d+):\s*stopped\b", re.I)
# Grace window and whether the guest agent answered. A guest that used more than
# its grace was force-killed, which is the most interesting thing in the log.
GUEST_GRACE = re.compile(
    r"\b(CT|VM)\s+(\d+):\s*(NO agent response|agent responding)?,?\s*"
    r"shutdown with (\d+)s grace", re.I)
GUEST_START = re.compile(r"\bStarting\s+(LXC|VM)\s+(\d+)\b", re.I)
TS_HEAD = re.compile(r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{2}:\d{2})")

# Phase markers, matched in order against the log text.
STAGE_PATTERNS = [
    ("detect", "power failure detected"),
    ("remote", "powered down"),
    ("remote", "power-off"),
    ("guests", "guest shutdowns in parallel"),
    ("guests", "Dispatched"),
    ("cap",    "power cap"),
    ("done",   "on_battery complete"),
    ("restore", "restoration started"),
    ("uncap",  "cap removed"),
    ("up",     "Starting"),
    ("restored", "restoration completed"),
]


def _line_epoch(line):
    m = TS_HEAD.match(line)
    if not m:
        return None
    try:
        return datetime.fromisoformat(m.group(1)).timestamp()
    except ValueError:
        return None


def build_sequence(lines, started, finished, kind, live):
    """Turn a run log into per-guest down/up offsets plus phase markers.

    One record per guest carrying both offsets, rather than two events, which
    halves the payload and lets the fob animate a guest collapsing and coming
    back without matching anything up itself.
    """
    base = None
    for ln in lines:
        e = _line_epoch(ln)
        if e is not None:
            base = e
            break
    if base is None:
        base = started or 0

    order, guests = [], {}
    stages, seen_stage = [], set()
    for ln in lines:
        e = _line_epoch(ln)
        off = max(0, int(e - base)) if e is not None else 0

        m = GUEST_GRACE.search(ln)
        if m:
            gid = int(m.group(2))
            rec = guests.setdefault(gid, {"t": m.group(1).lower(), "id": gid,
                                          "o": -1, "u": -1})
            if gid not in order:
                order.append(gid)
            rec["gr"] = int(m.group(4))
            rec["ag"] = 0 if (m.group(3) or "").lower().startswith("no") else 1
            continue

        m = GUEST_STOP.search(ln)
        if m:
            gid = int(m.group(2))
            if gid in guests:
                guests[gid]["o"] = off
                continue
            if gid not in guests:
                order.append(gid)
                guests[gid] = {"t": m.group(1).lower(), "id": gid, "o": off, "u": -1}
            continue
        m = GUEST_START.search(ln)
        if m:
            gid = int(m.group(2))
            ty = "ct" if m.group(1).upper() == "LXC" else "vm"
            if gid not in guests:
                order.append(gid)
                guests[gid] = {"t": ty, "id": gid, "o": -1, "u": off}
            elif guests[gid]["u"] < 0:
                guests[gid]["u"] = off
            continue
        for key, needle in STAGE_PATTERNS:
            if key in seen_stage:
                continue
            if needle.lower() in ln.lower():
                seen_stage.add(key)
                stages.append({"k": key, "o": off})
                break

    rows = [guests[g] for g in order][:12]
    shed = next((x["o"] for x in stages if x["k"] == "guests"), 0)
    for r in rows:
        r.setdefault("gr", 45)
        r.setdefault("ag", 1)
        # Seconds of grace actually consumed, and whether it blew through it.
        r["us"] = max(0, r["o"] - shed) if r["o"] >= 0 else 0
        r["fx"] = 1 if (r["o"] >= 0 and r["us"] > r["gr"]) else 0
    modeled = 0

    # The dry run models only the shutdown half - its log ends at "on_battery
    # complete" and the dashboard's own stage list stops at 'done'. Append a
    # MODELED recovery so the fob can show a whole cycle, flagged so it is never
    # presented as something that actually happened.
    if kind == "dry" and not any(r["u"] >= 0 for r in rows):
        tail = max([r["o"] for r in rows] + [x["o"] for x in stages] + [0])
        stages.append({"k": "restore", "o": tail + 2})
        for i, r in enumerate(reversed(rows)):
            r["u"] = tail + 4 + i
        stages.append({"k": "up", "o": tail + 4})
        stages.append({"k": "restored", "o": tail + 5 + len(rows)})
        modeled = 1

    # Offset of every log line, so a position report from the browser maps onto
    # the same instant the browser is showing (its frames reveal lines, and the
    # lines are not evenly spread through the run).
    lo, last = [], 0
    for ln in lines:
        e = _line_epoch(ln)
        if e is not None:
            last = max(0, int(e - base))
        lo.append(last)

    dur = max([r["o"] for r in rows] + [r["u"] for r in rows]
              + [x["o"] for x in stages] + [0])
    if finished and started and finished > started and not modeled:
        dur = max(dur, int(finished - started))
    stages.sort(key=lambda x: x["o"])
    return {"kind": kind, "dur": dur, "budget": 240, "live": 1 if live else 0,
            "mod": modeled, "guests": rows, "stages": stages[:9],
            "lo": lo[:80], "nl": len(lo)}


def poll_sequence():
    """Prefer a live run; otherwise the most recent one that has any detail.

    The real run's sequence is also kept on its own, because the dashboard's
    "replay last real run" plays THAT while the newest run may be a dry one -
    serving the wrong sequence would animate the wrong guests.
    """
    best = None
    real_seq = None
    for kind, url in (("dry", "/api/dryrun"), ("real", "/api/realtest/last")):
        try:
            r = get_json(POWER_BASE + url, timeout=5)
        except Exception:
            continue
        lines = [str(x) for x in (r.get("lines") or [])]
        if not lines:
            continue
        seq = build_sequence(lines, r.get("started") or 0, r.get("finished") or 0,
                             kind, bool(r.get("running")))
        seq["st"] = r.get("started") or 0
        if kind == "real":
            real_seq = seq
        if not seq["guests"] and not seq["stages"]:
            continue
        if best is None or seq["live"] > best["live"] or \
           (seq["live"] == best["live"] and seq["st"] > best.get("st", 0)):
            best = seq
    if best:
        with lock:
            state["seq"] = best
    if real_seq:
        with lock:
            state["seqR"] = real_seq


def poll_guests():
    try:
        out = subprocess.run(
            ["pvesh", "get", "/cluster/resources", "--type", "vm",
             "--output-format", "json"],
            capture_output=True, text=True, timeout=20)
        rows = json.loads(out.stdout) if out.returncode == 0 else []
    except Exception:
        return
    vms, cts = [], []
    for r in rows:
        if r.get("status") != "running":
            continue
        maxmem = r.get("maxmem") or 1
        maxdisk = r.get("maxdisk") or 0
        disk = r.get("disk") or 0
        item = {
            "id": r.get("vmid"), "n": (r.get("name") or "?")[:14],
            "cpu": round((r.get("cpu") or 0) * 100, 1),
            "nc": r.get("maxcpu"),
            # QEMU reports host RSS, which can exceed the configured max and
            # produce >100%; clamp so the fob's bars stay meaningful.
            "mem": min(100.0, round((r.get("mem") or 0) / maxmem * 100, 1)),
            "mgb": round(maxmem / 1073741824, 1),
            "up": r.get("uptime") or 0,
            "dsk": round(disk / maxdisk * 100, 1) if maxdisk and disk else None,
        }
        (vms if r.get("type") == "qemu" else cts).append(item)
    vms.sort(key=lambda x: x["id"])
    cts.sort(key=lambda x: x["id"])
    with lock:
        state["vms"], state["cts"] = vms, cts


def poll_weather():
    """Local UDP is unreachable from every segment here, so use the cloud API."""
    tok = read_token()
    if not tok:
        return
    try:
        j = get_json(
            "https://swd.weatherflow.com/swd/rest/observations/station/" + STATION_ID,
            timeout=12, token=tok)
    except Exception:
        return
    obs = (j.get("obs") or [{}])[0]
    if not obs:
        return
    trend = obs.get("pressure_trend") or "steady"
    with lock:
        state["wx"] = {
            "tc": obs.get("air_temperature"), "fl": obs.get("feels_like"),
            "dc": obs.get("dew_point"), "rh": obs.get("relative_humidity"),
            "mb": obs.get("sea_level_pressure"),
            "tr": {"falling": -1, "steady": 0, "rising": 1}.get(trend, 0),
            "wm": obs.get("wind_avg"), "gm": obs.get("wind_gust"),
            "wd": obs.get("wind_direction"),
            "uv": obs.get("uv"), "sr": obs.get("solar_radiation"),
            "bri": obs.get("brightness"),
            "rn": obs.get("precip"), "rnd": obs.get("precip_accum_local_day"),
            "s1": obs.get("lightning_strike_count_last_1hr"),
            "s3": obs.get("lightning_strike_count_last_3hr"),
            "skm": obs.get("lightning_strike_last_distance"),
            "sts": obs.get("lightning_strike_last_epoch"),
            "age": max(0, int(time.time()) - int(obs.get("timestamp") or 0)),
        }


def sun_times(lat, lon, when=None):
    """Sunrise/sunset as epochs (NOAA approximation). The forecast endpoint does
    not return them, and a day-arc needs them."""
    import math
    now = when or time.time()
    day = int(now // 86400)
    jd = day + 2440587.5
    n = jd - 2451545.0 + 0.0008
    J = n - lon / 360.0
    M = math.radians((357.5291 + 0.98560028 * J) % 360)
    C = 1.9148 * math.sin(M) + 0.02 * math.sin(2 * M) + 0.0003 * math.sin(3 * M)
    lam = math.radians(((math.degrees(M) + C + 180 + 102.9372) % 360))
    Jt = 2451545.0 + J + 0.0053 * math.sin(M) - 0.0069 * math.sin(2 * lam)
    dec = math.asin(math.sin(lam) * math.sin(math.radians(23.44)))
    try:
        w = math.acos((math.sin(math.radians(-0.83)) - math.sin(math.radians(lat)) * math.sin(dec))
                      / (math.cos(math.radians(lat)) * math.cos(dec)))
    except ValueError:
        return 0, 0
    wd = math.degrees(w) / 360.0
    rise = (Jt - wd - 2440587.5) * 86400
    sett = (Jt + wd - 2440587.5) * 86400
    return int(rise), int(sett)


def poll_weather2():
    """Richer current conditions plus forecast, for the weather fob."""
    tok = read_token()
    if not tok:
        return
    try:
        j = get_json("https://swd.weatherflow.com/swd/rest/better_forecast"
                     "?station_id=" + STATION_ID +
                     "&units_temp=c&units_wind=mps&units_pressure=mb"
                     "&units_precip=mm&units_distance=km", timeout=14, token=tok)
    except Exception:
        return
    cc = j.get("current_conditions") or {}
    if not cc:
        return
    fc = j.get("forecast") or {}
    daily = (fc.get("daily") or [])[:3]
    hourly = (fc.get("hourly") or [])[:12]
    lat, lon = j.get("latitude") or 0, j.get("longitude") or 0
    rise, sett = sun_times(lat, lon)
    trend = {"falling": -1, "steady": 0, "rising": 1}.get(cc.get("pressure_trend"), 0)

    out = {
        "t": int(time.time()),
        "loc": (j.get("location_name") or "")[:18],
        "cond": (cc.get("conditions") or "")[:22],
        "icon": (cc.get("icon") or "")[:26],
        "tc": cc.get("air_temperature"), "fl": cc.get("feels_like"),
        "dc": cc.get("dew_point"), "rh": cc.get("relative_humidity"),
        "wb": cc.get("wet_bulb_temperature"),
        "mb": cc.get("sea_level_pressure"), "tr": trend,
        "wm": cc.get("wind_avg"), "gm": cc.get("wind_gust"),
        "wd": cc.get("wind_direction"),
        "wc": (cc.get("wind_direction_cardinal") or "")[:4],
        "uv": cc.get("uv"), "sr": cc.get("solar_radiation"),
        "bri": cc.get("brightness"),
        "rnd": cc.get("precip_accum_local_day"),
        "rmin": cc.get("precip_minutes_local_day"),
        "pp": cc.get("precip_probability"),
        "s1": cc.get("lightning_strike_count_last_1hr"),
        "s3": cc.get("lightning_strike_count_last_3hr"),
        "skm": cc.get("lightning_strike_last_distance"),
        "sts": cc.get("lightning_strike_last_epoch"),
        "rise": rise, "set": sett,
        "age": max(0, int(time.time()) - int(cc.get("time") or 0)),
        "day": [{"hi": d.get("air_temp_high"), "lo": d.get("air_temp_low"),
                 "c": (d.get("conditions") or "")[:18],
                 "pp": d.get("precip_probability"),
                 "ic": (d.get("icon") or "")[:26]} for d in daily],
        "hr": [{"h": h.get("local_hour"), "tc": h.get("air_temperature"),
                "pp": h.get("precip_probability")} for h in hourly],
    }
    with lock:
        state["wx2"] = out
        # Six hours of history at ten-minute spacing, for sparklines.
        h = state["wxhist"]
        if not h["t"] or int(time.time()) - h.get("last", 0) > 600:
            h["last"] = int(time.time())
            if out["tc"] is not None:
                h["t"].append(round(out["tc"], 1))
                h["t"][:] = h["t"][-36:]
            if out["mb"] is not None:
                h["p"].append(round(out["mb"], 1))
                h["p"][:] = h["p"][-36:]


def worker():
    n = 0
    while True:
        try:
            poll_power()
            # A run is polled hard so the fob's timeline animation is live.
            if n % 2 == 0 or state["run"].get("active"):
                poll_run()
            if n % 4 == 0:
                poll_guests()
            if n % 2 == 0 or state["seq"].get("live"):
                poll_sequence()
            if n % 8 == 0:
                poll_replay()
            if n % 12 == 0:
                poll_weather()
            if n % 24 == 0:
                poll_weather2()
        except Exception:
            pass
        n += 1
        time.sleep(2.5)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/replay/pos":
            # The dashboard ticks this every 800ms while replaying.
            q = self.path.split("?", 1)[1] if "?" in self.path else ""
            args = dict(p.split("=", 1) for p in q.split("&") if "=" in p)
            try:
                with lock:
                    state["pos"] = {"i": int(args.get("i", 0)),
                                    "n": int(args.get("n", 0)),
                                    "ts": int(time.time())}
            except ValueError:
                pass
            body = b'{"pos":"ok"}'
        elif path == "/replay":
            # Ask the fob to replay the last real run now. Safe and read-only
            # with respect to the hypervisor: it only bumps a counter the fob
            # polls, and touches no guardian state.
            with lock:
                state["rep"]["req"] = int(time.time())
            body = b'{"replay":"queued"}'
        elif path == "/wx.json":
            with lock:
                w = dict(state.get("wx2") or {})
                w["ht"] = list(state["wxhist"]["t"])
                w["hp"] = list(state["wxhist"]["p"])
                body = json.dumps(w, separators=(",", ":")).encode()
        elif path in ("/", "/health"):
            body = b'{"ok":1}'
        elif path == "/fob.json":
            with lock:
                out = dict(state)
                pos = dict(state.get("pos") or {})
                age = int(time.time()) - (pos.get("ts") or 0)
                pos["age"] = age if pos.get("ts") else 9999
                out["pos"] = pos
                # While the browser is replaying, serve the REAL run's sequence
                # so both screens are describing the same event.
                if pos["age"] <= 4 and state.get("seqR"):
                    out["seq"] = state["seqR"]
                out.pop("seqR", None)
                body = json.dumps(out, separators=(",", ":")).encode()
        else:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        # The dashboard runs on :8787 and calls /replay here on :8788, which is
        # cross-origin. Read-only, LAN-only service, so a permissive header is
        # the simplest correct answer.
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass          # the fob polls constantly; do not fill the journal


if __name__ == "__main__":
    threading.Thread(target=worker, daemon=True).start()
    ThreadingHTTPServer(("0.0.0.0", PORT), Handler).serve_forever()
