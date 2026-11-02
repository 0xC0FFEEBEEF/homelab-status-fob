# homelab-fob

A pocket status fob: ESP8266 on an **HW-364A** board (0.96" SSD1306 128x64 OLED,
CH340 USB), showing live nbdevlab state. Four UI designs live side by side and
share one data layer, so you can flash whichever you want.

    ~/.esp-venv/bin/pio run -e design4 -t upload    # current: live data + pages
    ~/.esp-venv/bin/pio run -e design1 -t upload    # swiping card carousel
    ~/.esp-venv/bin/pio run -e design2 -t upload    # auto-focus hero
    ~/.esp-venv/bin/pio run -e design3 -t upload    # sky over rack
    ~/.esp-venv/bin/pio run -e diag   -t upload     # I2C joint finder
    ~/.esp-venv/bin/pio device monitor              # serial, 115200

PlatformIO lives in a venv (`~/.esp-venv`), nothing system-wide. The firmware is
plain Arduino C++ over U8g2, so it also builds in the Arduino IDE — only
`platformio.ini` is PlatformIO-specific.

**First run:** copy `src/common/secrets.h.example` to `src/common/secrets.h` and
fill in WiFi. That file is gitignored.

## The board, and two things that define the design

**The OLED is on non-default I2C pins** — HW-364A is SDA=GPIO14 (D6),
SCL=GPIO12 (D5) at 0x3C, and the **B revision swaps SDA and SCL**. Autoprobing
turned out to be a dead end: a bare-address write NAKs (rc=2) on every pin pair
and address on this board while U8g2's own transmissions to the same pins drive
the panel perfectly, so the probe was measuring something other than presence and
printed a wrong diagnosis at every boot. The pinout is therefore explicit —
`-D FOB_BOARD_REV_B` if the screen stays blank. `-e diag` re-probes continuously
and prints only transitions, for hunting an intermittent solder joint.

**The panel is two-colour**: rows 0–15 are yellow, 16–63 blue. That is also the
byte-page boundary (pages 0–1 vs 2–7), which every design exploits — chrome in
the amber band, data in the blue, and horizontal slides done by `memmove`ing
whole pages.

**There is no button.** Nothing waits for input.

## design4 — live data (current)

15 auto-rotating pages built from what is actually running: one per running VM,
one for containers, UPS, slerm, security, and three weather pages. Boot types
`nbdevlab` in; a sonar screen covers WiFi association and shows `joining` /
`linked` / `lost` so an unplug-replug reads correctly.

**The outage screen** appears only when triggered — a live run, a dashboard
replay, or `curl http://192.0.2.2:8788/replay`. It is deliberately *not* in the
rotation; it used to be, and kept replaying a stale dry run.

Its amber rail changes meaning by phase, because a deadline means nothing once
met and a battery level means nothing while mains is up:

| Phase | Label | Rail shows |
|---|---|---|
| shedding | `MAKING SAFE` | guests stopped / total |
| shutdown done, on battery | `ON BATTERY` | battery capacity |
| restoring | `COMING BACK` | guests restarted / total |
| finished | `RESTORED` | full |

Plus a 26 px line orbiting the border every 5.5 s (ambient only, encodes
nothing), the count that matters at 24 px, and `deadline` / `runtime` / `draw`
as small right-aligned values. Power figures are **real telemetry when a run is
live and modelled on replay** — the label says `runtime` vs `runtime est`.

**Dashboard sync:** your power dashboard's replay is browser-only and paces
itself at `min(max(lines,tl,1),45)` frames × 800 ms. Rather than copy that
formula, `settings.js` reports its frame position and the fob follows it,
mapping the reported frame through **each log line's real offset** (the 73 lines
are bunched — four at +4 s, then a 122 s gap). Header reads `real sync`.

## weather — the roaming weather fob

    ~/.esp-venv/bin/pio run -e weather -t upload

Reads the **WeatherFlow Tempest cloud API directly**, so it works on any WiFi
with no dependency on the home LAN — it is meant to travel. Six pages on an 8 s
dwell: NOW, WIND, SKY, STORM, FORECAST, TREND, with a line orbiting the border
and contrast dropping to 70 after dark so it is not glaring next to a bed.

**WiFi provisioning.** It joins the network compiled into `secrets.h`; if it
cannot, it hosts an **open** AP called `nbdevlab-weather` with a setup portal at
`192.168.4.1` (WiFiManager, non-blocking so the screen keeps drawing). A wrong
password simply fails to join and it hosts again. Once credentials are saved
through the portal a magic byte goes into EEPROM and those are preferred from
then on — needed because the ESP SDK retains the last SSID it ever saw (this
board still had a stale `Bratnet2` in it), so "something is stored" is not
evidence that it is usable. While the portal is up it retries the join every
45 s, so walking back into range recovers on its own.

### Three traps in doing TLS + JSON on an ESP8266

**`http.useHTTP10(true)` is mandatory.** WeatherFlow answers with chunked
encoding (`Content-Length: -1`), and `getStream()` hands back the raw socket with
the chunk-size headers still in it. ArduinoJson then parsed `1f4a\r\n{...` and
returned an **empty document with no error**, which is a miserable thing to
debug. HTTP/1.0 suppresses chunking.

**Build the ArduinoJson filter before opening the connection.** The TLS handshake
takes ~16 KB (WeatherFlow does not negotiate a smaller max fragment length, so
`probeMaxFragmentLength` fails and BearSSL keeps the full buffer), leaving ~11 KB.
Allocating the filter after that is asking for a silent partial filter.

**Filter with direct assignments, not a held `JsonObject`.** The document grows as
members are added and a held reference can dangle.

`better_forecast` returns ~240 hourly entries and will never fit in RAM. `hourly`
is simply absent from the filter, so it is skipped while streaming. Measured:
**heap bottoms at ~11 KB during a fetch and returns to 42 KB**, 38–42 fps.

## Gotchas worth keeping

**Every u8g2 primitive takes an unsigned coordinate, and `u8g2_DrawLine` has no
bounds check** (its source says so). A negative x wraps to ~65500 and its
Bresenham loop runs for millions of iterations — this hung the board mid-slide on
design1's sparkline and silently vanished the outgoing card's text. Defences:
`safeLine()` clamps in signed space, and slides `memmove` byte-pages instead of
drawing at a negative x.

**`constrain()` is a macro** and evaluates its argument up to three times, so
`constrain(x + random(...), lo, hi)` clamps a *different* draw than it returns.
That produced `-0.6 mph` wind. Always step into a local first.

**Bus runs at 600 kHz**, above the SSD1306's 400 kHz spec but stable here,
giving 37–40 fps measured. Drop `oledBegin()` to 400000 if it ever glitches.

## Data

`deploy/fob-api` on byteme2 aggregates Proxmox, the UPS dashboard API and the
WeatherFlow cloud into one ~3.5 KB JSON that the fob GETs over plain HTTP. See
`deploy/README.md`. The Tempest hub's local UDP broadcast (:50222) was
unreachable from every segment tested, hence the cloud path.

`src/common/data_mock.cpp` still drives designs 1–3 with a random walk plus
periodic events, so every state shows up without a network.

## Layout

    src/common/    status.h, fobdata.h/.cpp (live), data_mock.cpp (mock),
                   weather.h/.cpp (Tempest UDP), oled.h/.cpp, anim.h, secrets.h
    src/design1/   ui.h gfx.cpp cards.cpp screens.cpp main.cpp
    src/design2/   ui.h metrics.cpp render.cpp main.cpp
    src/design3/   ui.h sky.cpp rack.cpp event.cpp main.cpp
    src/design4/   ui.h pages.cpp scenes.cpp main.cpp
    src/diag/      main.cpp
    deploy/        fob_api.py, fob-api.service, settings.js.patch

## About this copy

A public snapshot of my private working repo; addresses are documentation-range stand-ins. More of my projects: [nbdevlab.com](https://www.nbdevlab.com).


## About this copy

A public snapshot of my private working repo; addresses are documentation-range stand-ins. More of my projects: [nbdevlab.com](https://www.nbdevlab.com).

## License

GNU General Public License v3.0 or later, see [`LICENSE`](LICENSE).

<sub>Preparing this public copy from my private repo was done with help from Claude.</sub>
