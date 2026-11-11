# Architecture

## Product boundary

This branch targets a single-purpose ESP32 stock monitor:

- Market quotes are displayed only on the local 1.3-inch OLED.
- The Web application is a large-text configuration surface.
- SHT30, PIR, wellness reminders, weather, and their state machines are not
  part of the product or the firmware build.

## Startup sequence

`main/app_main.c` is the orchestration layer. Every startup service records
its result in `startup_service`, which makes boot degradation visible on the
serial console and in the final startup snapshot.

1. Record the reset reason and start the independent onboard status LED.
2. Initialize persistent configuration storage and the process-wide event
   dispatcher.
3. Start the RGB light-language and feedback services.
4. Start the stock configuration owner with persisted settings or in-memory
   defaults.
5. Start the button service, stock monitor, and OLED retry loop.
6. Start Wi-Fi in station plus SoftAP mode.
7. Start the HTTP configuration server.
8. Start network time synchronization as a background capability.
9. Finish the startup snapshot and select ready, degraded, or safe-mode
   indication.

`config_service` and `app_events` are required for normal persistence and event
delivery. Their failures record a diagnostic trail and enter
`STARTUP_STATE_SAFE_MODE`, but do not create a boot loop. Stock settings fall
back to in-memory defaults so the OLED, keys, SoftAP, and HTTP recovery surface
can remain available. Wi-Fi, time, feedback, display, key, and HTTP failures are
reported as degraded services.

`STARTUP_STATE_READY` means the local startup contract completed. It does not
wait for station IP, network time, or a first market quote. The stock service
waits for an actual station connection before refreshing and requests an
immediate refresh on connection or reconnection.

## Component ownership

### `app_config`

Owns immutable build-time defaults from `menuconfig`:

- OLED I2C port, pins, clock, and controller
- K1-K4 GPIOs and polarity
- buzzer and vibration motor GPIOs
- onboard status LED enable state, GPIO, and polarity
- RGB enable state and red, green, and blue GPIOs

Runtime code reads these values through `app_config_get()`.

### `config_service`

Owns NVS initialization and typed persistent records. Stock settings are saved
under the versioned `stock_cfg` blob contract. Feature components do not call
NVS directly.

Wi-Fi credentials are saved only after the station receives an IP address.
This prevents a failed password attempt from overwriting the last known-good
network.

### `wifi_manager`

Owns station and SoftAP lifecycle, event handling, saved credentials, and IP
state. Station logs and quote requests rely on `wifi_manager_sta_connected()`.

Wi-Fi provisioning priority is:

1. Saved NVS network.
2. Embedded `wifi.ini`.
3. Build-time station defaults.

### `button_service`

Owns a fixed four-button configuration and one scanner task. The service:

- samples every 10 ms
- applies 25 ms debounce
- emits short press after release
- emits one long-press event while held
- keeps per-button short and long press counters

Consumers only see `button_event_t` events and a snapshot. GPIO filtering is not
duplicated in the stock monitor.

### `stock_service`

Owns:

- the validated watch list, up to eight symbols
- refresh interval and per-stock upper/lower price-limit persistence
- Tencent quote HTTP retrieval
- bounded response buffering and quote parsing
- last-known-good quote retention on failed requests
- refresh and failure counters
- analysis input and snapshots

Symbols are normalized before duplicate checks, so `600519` and `sh600519`
cannot both be saved. A refresh failure does not erase previous quotes; the
monitor marks data stale using the refresh interval with a five-minute floor.

### `stock_monitor_state`

This is a pure C state machine with no GPIO, HTTP, NVS, or FreeRTOS dependency.
It is host-tested and owns:

- selected stock index
- stock count clamping
- quote, indicator, analysis, and decision view cycling
- data states: waiting, fresh, stale, error, empty
- refresh-request output

K4 cycles exactly four data views.

### `oled_display`

Owns the I2C bus and SH1106/SSD1306 controller differences. The public API is a
small framebuffer and text interface; business code does not know the
controller command set. Initialization is retried by `stock_monitor` so a
display connected late or reset independently can recover.

### `stock_monitor`

Adapts the pure state machine to hardware and product behavior:

- reads snapshots once per monitor cycle
- polls key events without blocking
- applies K1/K2 navigation, K3 refresh, and K4 view changes
- redraws at a bounded rate
- keeps last quotes when the network fails
- integrates the intraday line and indicator values into data views
- triggers each stock's upper/lower price-limit feedback once per crossing
- re-reads stock configuration once per second so Web changes apply live

The header places the stock code on the left and the current local time plus
weekday abbreviation in the remaining centered space. The quote view shows the
large price, intraday line, percent/amount change, high/low, volume, and
turnover. The indicator view gives RSI, MACD histogram, EMA spread, and volume
ratio their own hierarchy. The analysis view combines score, trend direction,
open/previous close, VWAP distance, and Bollinger bands. The OLED uses page
marks rather than navigation or system-status text. The decision view maps
EMA, MACD, RSI, VWAP, Bollinger width, current price, and reward/risk into one
of `READY`, `WAIT`, `AVOID`, or `NO DATA`, then shows entry range, invalidation,
target, stop distance, and reward/risk. It is a technical-analysis reference,
not a promise of investment return.

### `feedback_service`

`buzzer_service` and `vibration_service` are independent actuator services.
Each owns its GPIO, timing task, stop behavior, pattern player, and snapshot.
They do not inspect stock state.

`feedback_service` maps semantic scenes to both actuators:

| Scene | Vibration | Vibrate total | Buzzer |
| --- | --- | ---: | --- |
| Navigation | 1 x 180 ms | 180 ms | 1 x 30 ms |
| Refresh started | 2 x 180 ms, 90 ms gap | 450 ms | 1 x 30 ms |
| Refresh success | 1 x 350 ms | 350 ms | 2 x 60 ms, 80 ms gap |
| Refresh failure | 2 x 500 ms, 200 ms gap | 1200 ms | 3 x 100 ms, 90 ms gap |
| Price alert | 2 x 700 ms, 250 ms gap | 1650 ms | 2 x 120 ms, 100 ms gap |
| Configuration saved | 1 x 320 ms | 320 ms | 2 x 60 ms, 80 ms gap |
| Configuration failed | 2 x 450 ms, 180 ms gap | 1080 ms | 3 x 100 ms, 90 ms gap |
| Wi-Fi connecting | 2 x 220 ms, 110 ms gap | 550 ms | 1 x 30 ms |
| Wi-Fi connected | 2 x 350 ms, 130 ms gap | 830 ms | 2 x 60 ms, 80 ms gap |
| Wi-Fi disconnected | 2 x 550 ms, 220 ms gap | 1320 ms | 3 x 100 ms, 90 ms gap |
| SoftAP client connected | 1 x 200 ms | 200 ms | 1 x 30 ms |
| SoftAP client disconnected | 1 x 260 ms | 260 ms | 1 x 30 ms |
| System degraded | 3 x 420 ms, two 180 ms gaps | 1620 ms | 3 x 100 ms, 90 ms gap |

The scene API keeps policy out of GPIO drivers. A future pairing, boot, or fault
scene can add a pattern without changing stock or button logic.

Feedback uses the same event priority model as RGB indication. Price alerts,
refresh failures, configuration failures, Wi-Fi disconnection, and system
degradation outrank successful status updates and navigation. A lower-priority
event cannot interrupt an active higher-priority pattern. Since feedback starts
before persisted stock settings are loaded, the highest-priority startup event
is held until the configured output mode is known.

### `rgb_indicator`

Owns the only RGB color policy in the firmware. The public API accepts semantic
system, network, and stock states plus semantic transient events. It does not
accept raw channel values.

The service combines two layers:

- one persistent base scene, selected from boot, network, system health, stock
  state, SoftAP, and ready states
- one temporary event scene with a priority, duration, color, brightness, and
  waveform

An event starts a new relative timeline when accepted. A lower-priority event
cannot interrupt the active event; an equal or higher-priority event replaces
it. When the event expires, rendering resumes from the current base scene.

`rgb_indicator_engine.c` is a pure C policy module and is host-tested. The
driver owns three LEDC channels, gamma and brightness scaling, the render task,
and GPIO output. Continuous business states call
`rgb_indicator_set_system_state()`, `rgb_indicator_set_network_state()`, or
`rgb_indicator_set_stock_state()`. Transient business events flow through
`app_events` and are translated to `rgb_indicator_notify()`.

Raw RGB profiles are intentionally private. Adding a business scene means
adding a semantic enum and profile in the engine, then publishing or setting
that semantic from the owning component.

### `status_led`

Owns the ESPDUINO-32 HW707 onboard blue user LED on `GPIO2`. The schematic
connects `IO2 -> blue LED -> 1 kOhm -> GND`, making the output active high. The
red power LED is hard-wired to 3.3 V and is outside firmware control.

The service has a non-blocking timing task and a host-tested pure pattern
module:

- booting: three 120 ms flashes
- ready: one 80 ms heartbeat every 2 seconds
- degraded: two 120 ms flashes every 2 seconds
- error: three faster flashes every 1.6 seconds

It stays independent from the external RGB service so a wiring or LEDC failure
cannot remove the board-level diagnostic indication. `GPIO2` is a strapping
pin, so the driver initializes the output off and the hardware LED circuit
must not pull the pin high during reset.

### `http_api`

Serves one embedded configuration page and a small JSON API:

- `GET /api/v1/config`
- `POST /api/v1/config`
- `POST /api/v1/buttons`
- `GET /api/v1/wifi/scan`
- `POST /api/v1/wifi`

The page handles Wi-Fi, stock list editing, refresh interval, per-stock upper
and lower price limits, output mode, and K1-K4 remote operation. Each stock row
is an independent component that displays the current price, today's high,
today's low, change percentage, and the limits owned by that symbol. Web key
commands are injected into `button_service` and consumed by `stock_monitor`
exactly like physical key events.

The HTML is gzip-compressed during the build. The compressed artifact is checked
against a 64 KiB budget before it is linked into firmware.

## Data flow

```text
Tencent quote response
        |
        v
  stock_service  ---> persisted stock settings
        |                    ^
        | snapshot           |
        v                    |
  stock_monitor_state        |
        |                    |
        v                    |
  stock_monitor ------------ OLED
        |
        +----> app_events ---> rgb_indicator ---> R/G/B LEDC
                         |
                         +--> feedback_service ---> buzzer_service
                                               +--> vibration_service

startup_state ---> status_led ---> onboard blue LED

button_service ---> stock_monitor
Web config ------> config_api ---> stock_service
Web keys --------> config_api ---> button_service ---> stock_monitor
```

The stock state machine decides navigation and data semantics. The monitor
decides how to present those states and publishes semantic events. Actuator
drivers only turn patterns into GPIO timing. RGB business callers provide
semantics only; color arbitration stays inside `rgb_indicator`.

## Flash layout

| Partition | Purpose | Offset | Size |
| --- | --- | ---: | ---: |
| `otadata` | OTA selection | `0x10000` | 32 KB |
| `nvs` | Wi-Fi and stock configuration | `0x12000` | 56 KB |
| `at_customize` | Retained custom area | `0x20000` | 896 KB |
| `ota_0` | Primary application | `0x100000` | 1.5 MB |
| `ota_1` | Secondary application | `0x280000` | 1.5 MB |

## Host tests

`tests/host` covers:

- Tencent response parsing
- stock symbol normalization and duplicate rejection
- selection wrapping and clamping
- four-view cycling
- data freshness and refresh-request state transitions
- stock analyzer core math and market aggregation
- decision state rules, entry/stop/target levels, and closed-bar fallback
- RGB state precedence, event priority, expiry, relative event timing, and
  complete event-to-scene coverage
- onboard status LED boot, heartbeat, degraded, and error patterns
- vibration pattern validity, pulse counts, and phone-style minimum durations

The physical actuator output, status/RGB GPIO output, FreeRTOS tasks, OLED I2C,
and HTTP handlers require target or hardware-in-the-loop validation.
