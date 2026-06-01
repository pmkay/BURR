# Flying Blind for Pebble

A minimalist Pebble watch face that tells you the time by **vibration** instead
of by sight. The screen stays blank — flick your wrist to briefly reveal the
time. Inspired by the [DURR](https://www.skreksto.re/products/durr) watch.

- Long buzz on the hour.
- Optional buzzes every 30 / 15 / 5 minutes, in a pattern distinct from the
  hourly chime so you can tell them apart by feel.
- Shake to reveal the time (and, optionally, the battery level).
- **Quiet hours** so it never buzzes while you sleep.
- Light / dark themes, plus an accent colour for the time on colour watches.
- Bluetooth-disconnect alert.
- All settings configured on-device via **Clay** — no website, no account.

![Flying Blind](watchface.jpg)

## Building (2026 Pebble SDK / Core Devices)

This project targets the modern Pebble SDK maintained by
[Core Devices](https://repebble.com) after Google open-sourced PebbleOS.

```sh
# 1. Install the Pebble CLI (needs Python 3.10–3.13)
uv tool install pebble-tool
pebble sdk install latest

# 2. Fetch the Clay configuration dependency
pebble package install @rebble/clay

# 3. Build
pebble build

# 4a. Run in the desktop emulator…
pebble install --emulator basalt      # or aplite / chalk / diorite / emery

# 4b. …or sideload to a real watch (enable Developer Connection in the
#     Pebble mobile app first)
pebble install --phone <PHONE_IP>
# or, via your linked account:
pebble install --cloudpebble
```

Prefer the browser? Open the project in
[CloudPebble](https://github.com/coredevices/CloudPebble) — no local install.

### Platforms

`targetPlatforms` in `package.json` builds for all seven supported devices:
aplite, basalt, chalk, diorite, emery, and the newest Core Devices hardware
**flint** (Core 2 Duo) and **gabbro** (Pebble Round 2). Trim the list if you
only want to iterate on the platforms you actually test on.

## Configuration

Open the watch face's settings from the Pebble mobile app (the gear icon next to
the face). The Clay page lets you set:

| Setting | Options |
| --- | --- |
| Theme | Dark / Light |
| Time colour | Any colour (colour watches only) |
| Reveal seconds | 2–10 |
| Show battery on reveal | On / Off |
| Buzz on the hour | On / Off |
| Between-hour buzzes | None / 30 min / 15 min / 5 min |
| Buzz strength | Subtle / Normal / Strong |
| Quiet hours | On / Off + start & end hour |
| Bluetooth-disconnect buzz | On / Off |

## Project layout

```
package.json          App manifest (replaces the old appinfo.json)
wscript               waf build script (standard SDK boilerplate; required)
src/c/flying_blind.c  The watch face
src/pkjs/index.js     Clay bootstrap (PebbleKit JS)
src/pkjs/config.json  Settings UI schema
resources/            Menu icon
```

## Credits

Original "Flying Blind" watch face by
[Matthew Congrove](http://github.com/mcongrove) (2013). DURR-style 5-minute
vibration and the 2026 modernization/revitalization by PK.

## License

Apache 2.0 — see [LICENSE.md](LICENSE.md).
