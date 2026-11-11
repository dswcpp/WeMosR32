# ESP32 Stock Monitor

Firmware for an ESP32-WROOM-32 based stock monitor. Market data is shown on a
1.3-inch 128x64 OLED. The built-in web application is configuration-only and
does not render live quotes.

## Hardware

| Function | Default connection |
| --- | --- |
| OLED I2C SDA | `GPIO21` |
| OLED I2C SCL | `GPIO22` |
| OLED controller | SH1106, address `0x3C` or `0x3D` |
| K1 previous stock / first stock | `GPIO27`, active low |
| K2 next stock / last stock | `GPIO16`, active low |
| K3 refresh now | `GPIO17`, active low |
| K4 change view / return to quote | `GPIO14`, active low |
| Active buzzer | `GPIO25`, active high |
| Vibration motor module input | `GPIO26`, active high |
| Board power LED | Hard-wired 3.3 V, always on |
| Board user LED | `GPIO2`, active high |
| RGB red | `GPIO32` |
| RGB green | `GPIO33` |
| RGB blue | `GPIO18` |
| RGB common | `GND`, common cathode |

Do not connect a bare vibration motor directly to an ESP32 GPIO. Use a
transistor or MOSFET driver, a gate/base resistor, and a flyback diode.

The ESPDUINO-32 HW707 schematic connects the red power LED directly between
3.3 V and ground, so it is never software controlled. The blue user LED is
connected from `IO2` through a 1 kOhm resistor to ground and is therefore
active high. `GPIO2` is also an ESP32 boot-strapping pin; this board's LED
circuit does not pull it high during reset.

The OLED controller and all GPIOs can be changed with `idf.py menuconfig` under
`WeMos R32 application`. The same defaults are recorded in
`sdkconfig.defaults`.

## OLED behavior

K1 and K2 select the watched stock. K3 requests an immediate quote refresh.
K4 cycles through four data views. The header shows the stock code on the
left and a centered `HH:MM MON` clock plus weekday on the right:

1. Quote: large price and intraday line, percent/amount change, high/low,
   volume, and turnover.
2. Indicators: large RSI and MACD values plus EMA spread and volume ratio.
3. Analysis: score and trend direction, open/previous close, VWAP distance,
   and Bollinger bands.
4. Decision: `READY`, `WAIT`, `AVOID`, or `NO DATA`, followed by the entry
   range, invalidation price, target, stop distance, and reward/risk ratio.

The OLED uses four small page marks instead of navigation text. Network
state, refresh counters, descriptions, and key hints are not drawn. Missing
analysis fields stay as aligned `--` placeholders until enough data exists.
Long-pressing K4 returns directly to the quote view.

The decision view is a deterministic technical-analysis reference, not a
guarantee of profit. `READY` requires bullish EMA and MACD alignment, price
above VWAP, healthy RSI, price inside the VWAP entry zone, and reward/risk of
at least 2.0. `WAIT` keeps the calculated levels visible while conditions are
incomplete or extended. `AVOID` disables entry levels when trend alignment is
bearish.

## Services and feedback

The application keeps business state separate from physical outputs:

- `stock_service` owns persisted stock settings, Tencent quote requests,
  retry behavior, stale data, and analysis input.
- `stock_monitor_state` is a host-testable state machine for selection,
  views, data freshness, and refresh requests.
- `stock_monitor` adapts the state machine to the OLED and keys.
- `button_service` owns GPIO scanning, debounce, short presses, and long
  presses.
- `buzzer_service` and `vibration_service` each own one actuator.
- `feedback_service` maps semantic scenes to bounded actuator patterns and
  arbitrates them by business priority.
- `status_led` owns the single-color onboard user LED.
- `rgb_indicator` owns the complete light language and is the only component
  that defines raw RGB values.

Business components publish semantic `app_events`; they do not drive the
buzzer or motor GPIO directly. Vibration patterns use phone-style sustained or
multi-pulse feedback rather than one short click:

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

The web setting can select both outputs, vibration only, sound only, or silent.
Price alerts and failures outrank status and navigation feedback, so ordinary
browsing cannot interrupt an active alert. Navigation uses the shortest pulse
so repeated browsing remains comfortable.

## Board status LED

The onboard blue LED provides a second, independent diagnostic channel:

| State | Pattern |
| --- | --- |
| Booting | Three 120 ms flashes, 120 ms gaps |
| Ready heartbeat | One 80 ms pulse every 2 seconds |
| Degraded | Two 120 ms pulses every 2 seconds |
| Error | Three 100 ms pulses every 1.6 seconds |

The service is active-high by default for the HW707. Its GPIO and polarity
remain configurable for other boards.

## RGB light language

Business code reports system, network, stock, and user-action semantics. It
never sends RGB channel values. `rgb_indicator` arbitrates a persistent base
state with short event overlays:

| Base state | Light language |
| --- | --- |
| Boot | Blue slow breathing |
| Wi-Fi disconnected | Amber breathing |
| Wi-Fi connecting | Bright blue fast breathing |
| System degraded | Amber slow breathing |
| No configured stocks | Purple breathing |
| Waiting for first quote | Cyan breathing |
| Refreshing | Bright cyan fast breathing |
| Fresh quote | Dim green solid |
| Stale quote | Amber breathing |
| Stock data error | Red breathing |
| SoftAP with no stock state | Blue slow breathing |
| Ready without a stock signal | Dim blue solid |

Transient events temporarily replace the base scene. A higher-priority event
can replace the active event; a lower-priority event is ignored. The pattern
always starts at the trigger time.

| Event | Light language | Priority |
| --- | --- | ---: |
| Price alert, rise | Fast red blink, China market convention | 90 |
| Price alert, fall | Fast green blink, China market convention | 90 |
| Stock refresh failed | Red triple blink | 80 |
| Configuration save failed | Red-orange triple blink | 75 |
| Wi-Fi disconnected | Orange double blink | 70 |
| System degraded | Amber triple blink | 65 |
| Stock refresh succeeded | Green double blink | 60 |
| Stock refresh started | Cyan double blink | 55 |
| Wi-Fi connected | Green-cyan double blink | 50 |
| Configuration saved | Ice-blue double blink | 40 |
| SoftAP client connected | Cool-white pulse | 20 |
| SoftAP client disconnected | Dim cool-white pulse | 15 |
| Button navigation | White quick pulse | 10 |

The base-state precedence is boot, Wi-Fi offline, Wi-Fi connecting, system
degraded, stock data state, SoftAP, ready. Price alerts and failure events are
therefore never hidden by ordinary status indication. Raw channel values and
waveform timing stay private to
`components/rgb_indicator/rgb_indicator_engine.c`.

## Web configuration

The SoftAP remains available for local setup:

- SSID: `StockMonitor`
- Password: `wemosr32`
- Home page: `http://192.168.4.1/`
- Configuration API: `GET` or `POST /api/v1/config`
- Web key API: `POST /api/v1/buttons`
- Wi-Fi scan API: `GET /api/v1/wifi/scan`
- Wi-Fi connect API: `POST /api/v1/wifi`

The page provides large text and controls for Wi-Fi setup, up to eight stock
codes, refresh interval, per-stock upper and lower price limits, alert output
mode, and K1-K4 operations. Each stock card shows the current price, today's
high, today's low, and its independently persisted limits. Web key clicks and
holds are injected into the same event queue as the physical keys, so
navigation, refresh, and view switching use identical logic.
The Web UI is compressed at build time and enforced below a 64 KiB gzip budget.

## Wi-Fi startup order

Station Wi-Fi is resolved in this order:

1. Valid Wi-Fi configuration saved in NVS.
2. Embedded `wifi.ini`.
3. Build-time station settings.

`wifi.ini` is only an initial provisioning source. If a saved NVS network
exists, it takes precedence, so editing `wifi.ini` alone will not change an
already provisioned device. Reconfigure Wi-Fi from the Web page or erase the
device flash when switching to a new network.

Create `wifi.ini` in the project root before building:

```ini
wifi-name: Example-2.4GHz
wifi-password: example-password
```

Common connection failures are an ESP32-incompatible 5 GHz-only SSID, a
password shorter than eight characters, a stale NVS network, or a failed
connection attempt that did not receive an IP address. The SoftAP is kept
available so `http://192.168.4.1/` can still be used for recovery.

## Build and test

```powershell
$env:IDF_TOOLS_PATH='D:\Espressif\.espressif'
. 'D:\Espressif\esp-idf\export.ps1'
idf.py build
```

Host tests do not require ESP-IDF:

```powershell
cmake -S tests\host -B build_host_tests
cmake --build build_host_tests
ctest --test-dir build_host_tests --output-on-failure
```

Flash with the detected serial port:

```powershell
idf.py -p COMx flash monitor
```

The 4 MB partition layout keeps two 1.5 MB OTA application slots. GPIO,
service ownership, flash persistence, and startup flow are documented in
`docs/architecture.md`.
