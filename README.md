# WeMos R32 Firmware Framework

ESP-IDF application skeleton for an ESP32-WROOM-32 device with 4 MB flash.
The default network mode is Wi-Fi SoftAP, with optional station mode for
connecting to an existing access point.

## Layout

```text
main/                       Application entry point
components/app_events/      Process-wide event queue and subscriber dispatch
components/app_config/      Build-time application configuration
components/config_service/  Persistent application and Wi-Fi configuration
components/wifi_manager/    Wi-Fi STA + SoftAP lifecycle
components/time_service/    RTC clock, timezone, and SNTP synchronization
components/weather_service/ Open-Meteo outdoor weather, cache, and refresh task
components/wellness_monitor/ Wellness sensors, reminders, and care settings
components/rgb_indicator/   Common-cathode RGB lighting and status effects
components/workstation_light/ Desk lamp relay controller for three relay inputs
components/http_api/        Embedded mobile dashboard and JSON status endpoint
docs/                       Architecture and development notes
include/                    Reserved for project-wide public headers
FreeRTOS/                   Reserved for application-specific RTOS modules
```

The partition table mirrors the existing firmware's 4 MB layout, including
the `at_customize` area and two 1.5 MB OTA application slots. The new
application is flashed to `ota_0`.

## Host tests

The business modules are plain C and can be tested without ESP-IDF:

```powershell
cmake -S tests/host -B build/host-tests -G "MinGW Makefiles"
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

The suite covers the sedentary state machine, PIR debounce and presence hold,
water reminder pacing, care scene priority, and actuator pulse playback. The
sedentary update result separates current state from occurrences: `snapshot`
describes the current phase and timers, `transitions` records ordered phase
changes, and `events` records reminder occurrences with their phase and level.
Transitions and events share one `order` sequence so consumers can preserve the
same timeline when forwarding them to the event bus. `output_count` is the next
sequence value; overflow flags report when a fixed-capacity list dropped an
entry. `sedentary_phase_name()` provides stable names such as `idle`, `sitting`,
`on_break`, and `alerting` for logs and management APIs. Every recorded
transition also carries a stable reason such as `motion_detected`,
`absence_detected`, `alert_due`, `short_break_return`, `break_return`, or
`break_completed`. The status API exposes the current name at
`wellness.sedentary.phase`.

## Requirements

- ESP-IDF 5.x
- Python and the ESP-IDF export script for the host platform
- USB serial driver for the board

## Build and flash

```powershell
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p COMx flash monitor
```

Configuration is available under `WeMos R32 application`. After startup:

- SoftAP SSID: `WeMosR32`
- SoftAP password: `wemosr32`
- Device homepage: `http://192.168.4.1/`
- Status API: `http://192.168.4.1/api/v1/status`
- Time API: `http://192.168.4.1/api/v1/time`
- Weather settings API: `http://192.168.4.1/api/v1/weather/settings`
- Weather refresh API: `http://192.168.4.1/api/v1/weather/refresh`
- Web dashboard settings API: `http://192.168.4.1/api/v1/web/settings`
- Wi-Fi scan API: `http://192.168.4.1/api/v1/wifi/scan`
- Wi-Fi configuration API: `http://192.168.4.1/api/v1/wifi`
- RGB lighting API: `http://192.168.4.1/api/v1/led`
- Workstation light API: `http://192.168.4.1/api/v1/workstation/light`
- Web app manifest: `http://192.168.4.1/manifest.webmanifest`

If a local `wifi.ini` exists in the project root, it is embedded at build time
and the device connects to that station network automatically while keeping
the SoftAP available:

```ini
wifi-name: Example-2.4GHz
wifi-password: example-password
```

`wifi.ini` is ignored by Git because it contains credentials. It provides the
initial station network used when no saved Wi-Fi configuration exists. Open
the device settings dialog to scan for a network or enter its SSID and password
manually. The device keeps the SoftAP running while it connects upstream.

Manual Wi-Fi configuration is written to NVS only after the station receives an
IP address. A failed password or unavailable network therefore does not
overwrite the last successfully saved configuration. On the next boot, the saved
NVS configuration takes precedence over `wifi.ini` and the build-time defaults.
If an older saved network exists after a failed manual attempt, restarting the
device restores that saved network.

```http
POST /api/v1/wifi
Content-Type: application/json

{"ssid":"Example-2.4GHz","password":"example-password"}
```

The response is `202 Accepted` once the connection attempt starts. Use
`/api/v1/status` to observe `station_connecting`, `station_connected`,
`station_saved`, and `station_last_error`.

Once station mode obtains internet access, the device synchronizes its system
clock with `ntp.aliyun.com` and exposes it in the `Asia/Shanghai` timezone. The
`/api/v1/time` endpoint returns Unix time, ISO 8601 time, validity, source, and
last synchronization details. If the network blocks UDP port 123, the device
falls back to reading the `Date` response header. HTTPS connectivity endpoints
are preferred and validated against the small public CA set in
`components/time_service/certs`. Plain HTTP fixed service addresses with an
explicit `Host` header are tried next, with HTTP DNS names as the final
fallback. The API reports this source as `http-date`. The ESP32 internal RTC
keeps time while powered, but this board has no backup RTC battery, so network
time must be restored after a full power loss.

```json
{
  "valid": true,
  "synced": true,
  "source": "http-date",
  "unix_time": 1793886300,
  "iso8601": "2026-11-05T21:45:00+08:00",
  "timezone": "Asia/Shanghai",
  "utc_offset_s": 28800,
  "ntp_server": "ntp.aliyun.com",
  "sync_endpoint": "connect.rom.miui.com",
  "last_sync_unix_time": 1793886300,
  "last_sync_s": 42,
  "last_sync_age_s": 0,
  "sync_count": 1
}
```

`source` is `sntp` after an NTP sync, `http-date` after an HTTP Date fallback,
`rtc` when the internal clock is already valid but has not synchronized since
boot, and `unavailable` before a valid clock exists. `last_sync_s` is device
uptime at the last successful synchronization.

## Outdoor weather

The device reads current conditions and today's daily range from Open-Meteo.
No API key is required. The defaults are Hangzhou at `30.2741, 120.1551`, and
the location can be changed from the weather section of the care settings
dialog.

Weather refreshes only after the upstream station interface is connected and
the device clock is valid. Successful data is refreshed every 30 minutes;
failed requests are retried after one minute. The latest valid result is stored
in NVS under `weather_cfg` and remains visible after a reboot. Cached data is
marked stale after 45 minutes, while the API continues to expose its original
observation time and age.

```http
POST /api/v1/weather/settings
Content-Type: application/json

{"enabled":true,"location":"杭州","latitude":30.2741,"longitude":120.1551}
```

Changing latitude or longitude invalidates the previous cache and starts a
refresh. Disabling the service stops network requests while preserving the
saved location.

```http
POST /api/v1/weather/refresh
Content-Type: application/json

{}
```

The refresh endpoint returns `202 Accepted`; clients should poll
`/api/v1/status` and watch `weather.refreshing`, `weather.refresh_count`,
`weather.fetched_age_s`, and `weather.last_error`. The web dashboard adds an
outdoor weather carousel card and handles fresh, cached, unavailable, disabled,
refreshing, and device-offline states. When the OLED is available, it alternates
between the care summary and outdoor weather every eight seconds.

The mobile dashboard includes a fullscreen toggle in the header. On browsers
that do not expose the Fullscreen API, the button is hidden and the page still
works normally.

## Web dashboard cards

The care settings dialog can show or hide each of the eight dashboard cards.
The current selection is exposed at `web.pages` in `/api/v1/status` and is
persisted as a versioned blob under `web_cfg/pages`:

- `clock`
- `environment`
- `sedentary`
- `water`
- `presence`
- `today_care`
- `weather`
- `workstation`

Save a complete page selection with:

```http
POST /api/v1/web/settings
Content-Type: application/json

{
  "pages": {
    "clock": true,
    "environment": false,
    "sedentary": true,
    "water": true,
    "presence": false,
    "today_care": true,
    "weather": true,
    "workstation": true
  }
}
```

All eight booleans are required, and at least one card must remain enabled.
After saving, the dashboard applies the new layout immediately; a reboot keeps
the saved selection.

The dashboard ships as one HTML document and one HTTP response. At build time,
the page is compacted, gzip-compressed, and checked against a 64 KiB compressed
size budget before it is embedded in the firmware. The uncompressed copy is
not stored on the device. Card settings are generated from one `WEB_PAGES`
metadata table, so adding or reordering a card does not require duplicating
settings switches and carousel controls in the markup.

## Workstation desk light

The workstation controller uses the first three inputs of a four-channel relay
module. The fourth relay input is intentionally not configured.

| Function | ESP32 GPIO | Relay behavior |
| --- | --- | --- |
| Lamp power | `GPIO13` | 180 ms pulse connected to relay `IN1` |
| Brightness | `GPIO14` | 180 ms pulse connected to relay `IN2` |
| Color temperature | `GPIO23` | 180 ms pulse connected to relay `IN3` |
| Presence input | `GPIO27` | Existing PIR input from the wellness monitor |
| Onboard status LED | `GPIO2` | Active-high red software LED mirrors PIR presence |
| Fourth channel | Not assigned | Unused |

All three relay contacts are momentary and are wired in parallel with the lamp
panel's low-voltage power, brightness, and color-temperature buttons. Relay 1
presses the power button once to change the lamp's on/off state. Presence
automation is enabled by default: when presence starts, relay 1 pulses once;
after the PIR presence hold expires, relay 1 pulses again. The wellness monitor
holds presence for 120 seconds after the last detected motion. One pulse on
relay 2 advances brightness through four levels, and one pulse on relay 3 cycles
through cool, warm, and mixed modes. The module defaults to active-low inputs, a
180 ms pulse, and a 160 ms release gap; all values are configurable under
`WeMos R32 application`.

The black Wemos D1 R32 board has two visible LEDs. The blue power LED lights
whenever 3.3 V is present and cannot be controlled by firmware. The red
software-controlled LED is connected to `GPIO2` and is active high. It turns on
while the PIR reports presence and turns off when the 120-second presence hold
expires. This indicator is independent of the lamp-power automation; it can
remain enabled when automatic relay switching is disabled. Clone boards can
swap the LED colors or polarity, so verify the hardware or change the GPIO and
polarity under `WeMos R32 application`.

```http
POST /api/v1/workstation/light
Content-Type: application/json

{"action":"brightness_next"}
```

Supported actions are `power_on`, `power_off`, `toggle_power`,
`brightness_next`, and `color_next`. The status response exposes the controller
at `workstation.light`. It reports relay power, readiness, cycle counters, pulse
timing, presence automation state, the last action source, and the last action.
It does not report a current brightness level or color mode because the relay
interface receives no position feedback from the lamp panel; only the physical
button-cycle count is known. The reported power value is the controller's
assumed lamp state, not measured electrical feedback. Operating the lamp's
physical panel buttons can therefore make that value differ from the real lamp
state. The controller releases all three relays during initialization, then
presence automation may immediately pulse relay 1 if the PIR already reports
presence. Manual actions remain available, but automation does not continuously
override a manual change until the presence state changes again.

Electrical safety is mandatory:

- Never connect mains voltage directly to an ESP32 GPIO. Use a relay module
  rated for the load, with suitable isolation, terminal spacing, and enclosure.
- Tap the lamp panel buttons only on their low-voltage control side. Confirm
  the panel's button topology before wiring relay contacts across it.
- Check whether the relay board is opto-isolated. Follow its documented input
  and ground wiring; non-isolated modules require a common ground with the
  ESP32. ESP32 GPIOs are not 5 V tolerant.
- Fit pull-ups on active-low relay inputs when the module does not already
  provide them, so the relays remain released during ESP32 reset and boot.
- Use the normally-open contacts and verify that every relay is released before
  energizing the lamp.

## Integrated button panel and OLED

The 1.3-inch OLED and four-key panel are independent from the desk-lamp panel.
The keys configure wellness, reminder, and display behavior; they do not
replace or duplicate the lamp's power, brightness, or color controls.

| Key | Function | ESP32 GPIO | Pull-up |
| --- | --- | --- | --- |
| `K1` | Acknowledge feedback | `GPIO32` | Internal |
| `K2` | Add or remove one cup of water | `GPIO33` | Internal |
| `K3` | Temporary mute or alert mode | `GPIO34` | External 10 kOhm required |
| `K4` | OLED page selection | `GPIO35` | External 10 kOhm required |

All keys are active low. Connect one side of each key to the matching GPIO and
the other side to GND. `GPIO34` and `GPIO35` are input-only pins without an
internal pull-up on ESP32, so each needs an external `10 kOhm` resistor to
`3.3 V`. The integrated key board may already include these resistors; verify
it before adding duplicates. Do not connect any key to a voltage above 3.3 V.

The default debounce interval is 25 ms, and a key held for at least 1.5 seconds
is classified as a long press when released:

- `K1`: short press stops the current buzzer and vibration alert. Long press is
  currently unassigned.
- `K2`: short press adds the configured `cup_ml` to today's water total. Long
  press removes one cup and never reduces the total below zero.
- `K3`: short press toggles a 30-minute temporary mute. Long press cycles the
  persistent alert mode through `both -> vibrate -> sound -> silent`.
- `K4`: short press cycles the manual OLED pages through care, water,
  environment, and weather. Long press restores the automatic care/weather
  carousel.

The OLED uses an inverted page header, one primary value, a short action line,
and a context-sensitive key strip. It does not expose raw PIR values, sensor
online state, or weather fetch timestamps. The key strip shows only the two or
three actions relevant to the current page; on-screen numbers `1` through `4`
correspond to `K1` through `K4`. A hint is highlighted when feedback is active,
water is due, temporary or persistent mute is active, or the display is on a
manually selected page.

The 1.3-inch display defaults to the `SH1106` controller. The firmware uses the
SH1106 132-column RAM layout with the required two-column offset, probes I2C
addresses `0x3C` and `0x3D`, and keeps the existing `SDA=GPIO21`,
`SCL=GPIO22` wiring. Select `SSD1306 or SSD1315 compatible` under
`WeMos R32 application` if the module documentation identifies another
controller. A uniform two-column shift or clipping usually indicates a
controller-selection mismatch; a completely blank display can also indicate
wiring, address, or power problems.

## Alert outputs

The active buzzer is high-level triggered and uses `GPIO25`:

| Active buzzer module | ESP32 |
| --- | --- |
| `VCC` | Module-rated supply |
| `GND` | Common ground |
| `I/O` or `IN` | `GPIO25` |

The default three-wire vibration motor module uses an active-high `IN` input:

| Motor module | ESP32 |
| --- | --- |
| `VCC` | Module-rated supply (`3.3 V` only if the module explicitly supports it) |
| `GND` | Common ground |
| `IN` | `GPIO26` |

The motor output is independent from the active buzzer on `GPIO25`. `GPIO26`
is held low at boot so the motor stays off until an alert. If the motor has
only two wires, do not connect it directly to an ESP32 pin: add an
NPN/MOSFET low-side driver, a base/gate resistor, and a flyback diode across
the motor. A 5 V module may use 5 V for `VCC`, but its ground must remain
common with the ESP32.

The buzzer and motor are implemented as separate services. `care_coordinator`
selects one semantic care scene at a time so overlapping reminders do not stack
into a loud or continuous alert. Escalation wins over a first sedentary alert,
which wins over a water reminder. `feedback_service` only translates the chosen
scene into actuator-specific pulse patterns and the selected alert mode.

| Scene | Active buzzer | Buzzer window | Vibration motor | Motor window |
| --- | --- | --- | --- | --- |
| Water reminder | 2 x 120 ms, 200 ms gap | 440 ms | 2 x 700 ms, 250 ms gap | 1650 ms |
| First sedentary alert | 2 x 160 ms, 220 ms gap | 540 ms | 3 x 800 ms, 250 ms gap | 2900 ms |
| Escalated sedentary alert | 3 x 160 ms, 180 ms gap | 840 ms | 4 x 900 ms, 250 ms gap | 4350 ms |
| Confirmation | 1 x 80 ms | 80 ms | 1 x 300 ms | 300 ms |

No vibration pulse is longer than 900 ms. The longer pulses allow a small
ERM motor to reach noticeable speed, while the 250 ms gaps let it stop and
rest between bursts. This reduces heat and avoids holding the motor on
continuously. The existing `both`, `vibrate`, `sound`, and `silent` settings
apply to every scene.

## RGB indicator

The four-wire `R/G/B/GND` light is treated as a common-cathode RGB LED. The
three color inputs are high-active and are driven by three 5 kHz LEDC PWM
channels:

| RGB module | ESP32 |
| --- | --- |
| `R` | `GPIO4` |
| `G` | `GPIO19` |
| `B` | `GPIO18` |
| `GND` | Common ground |

The Web settings dialog supports automatic status indication, a fixed color,
rainbow cycling, brightness control, and off mode. Automatic mode uses cyan
while sitting, green while on a valid break, blue for water reminders, and a
red alert flash for strong sedentary reminders. The selected mode is stored in
NVS and restored after reboot.

```http
POST /api/v1/led
Content-Type: application/json

{"mode":"solid","r":0,"g":160,"b":255,"brightness":60}
```

Supported modes are `auto`, `solid`, `rainbow`, and `off`. `r`, `g`, and `b`
must be integers from 0 to 255; `brightness` must be from 0 to 100. This is a
three-channel analog RGB light, not an addressable WS2812 strip, so all LEDs
connected to the module display the same mixed color. If the LED board does not
already include current-limiting resistors, add one resistor in series with
each color input. Never drive a bare high-power LED directly from an ESP32 GPIO.

Change both Wi-Fi credentials before deploying the device outside a lab.
The default SoftAP password is a development convenience, not a production
secret. See `docs/security.md` before exposing the device to an untrusted
network.

## Compatibility note

The logged firmware was built with ESP-IDF 3.0.3. This framework targets a
maintained ESP-IDF 5.x release because the old SDK is no longer suitable for
new development. The partition layout is preserved, but application code
cannot be copied unchanged from ESP-IDF 3.0.3 APIs.
