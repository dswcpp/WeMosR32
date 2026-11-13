# Architecture

## Startup sequence

`app_main` is an orchestration layer. It records each initialization result in
`startup_service` and does not treat every failed subsystem as fatal:

1. Record the ESP reset reason and enter the core stage.
2. Initialize persistent configuration storage through `config_service`.
3. Start the application event queue and subscriber dispatcher.
4. Resolve build-time defaults from `app_config` and start Wi-Fi in
   STA+SoftAP mode.
5. Start time synchronization, the weather refresh task, the feedback services,
   and the wellness monitor.
6. Initialize the workstation desk-light relay controller.
7. Start the HTTP status API with up to three attempts.
8. Finish the startup snapshot and return from `app_main`; ESP-IDF and FreeRTOS
   continue running the system tasks.

Only `config_service` and `app_events` are boot-critical. A failure there records
the failed stage, waits briefly so the serial log can be captured, and performs
a controlled restart. Wi-Fi, time, weather, feedback, wellness, workstation
light, and HTTP failures remain visible as failed service records while the rest
of the device continues in degraded mode. Feedback can additionally be
`degraded` when only one output device is available.

Disabling a feature records `disabled`; it is not treated as a boot failure.
Pending or starting services make the final snapshot degraded, which catches
missing startup steps instead of silently reporting success. Startup failures
are sticky, so a repeated lower-severity result cannot hide the original error.

`startup_service` owns a fixed-size, thread-safe snapshot containing:

- overall state: `booting`, `ready`, `degraded`, or `failed`
- current stage
- reset reason
- total startup duration
- per-service state, first error, start time, finish time, and duration

The `/api/v1/status` response exposes this snapshot under `startup` without
changing existing fields. This gives diagnostics access to boot health even when
an optional subsystem failed before the web UI became available.

## Components

### `app_events`

Owns the process-wide event queue and serialized subscriber dispatch. Producers
publish small typed events for state changes that consumers must observe.
Current device state remains available through component snapshots; events are
reserved for occurrences such as reminders, alerts, and recovery actions.

Every event carries source, severity, timestamp, and a process-wide sequence.
The `value` and `detail` fields are event-specific payloads. In particular,
`APP_EVENT_SEDENTARY_PHASE_CHANGED` uses `value` for the target phase and
`detail` for the previous phase. Its `cause` field carries the
`sedentary_transition_reason_t` value. Phase values are integer
`sedentary_phase_t` values; phase and reason values can be converted to stable
names with `sedentary_phase_name()` and
`sedentary_transition_reason_name()`.

Publishing is non-blocking and returns `ESP_ERR_TIMEOUT` when the queue is full.
Subscriber callbacks run in the event task, so they must not perform long or
blocking work.

### `app_config`

Owns immutable build-time settings sourced from `menuconfig`. Product-specific
defaults belong here rather than inside networking code.

### `config_service`

Owns NVS initialization, recovery, and all mutable persistent configuration.
Feature components use typed Wi-Fi operations or registered blob items instead
of calling NVS directly. Existing namespaces and keys are preserved so firmware
updates retain saved Wi-Fi and care settings.

### `wifi_manager`

Owns the Wi-Fi and IP event lifecycle. It exposes a small API for startup and
status queries. SoftAP remains enabled when station mode is configured. Saved
station credentials are loaded and written through `config_service`. Station
and SoftAP client transitions are also published through `app_events`.

### `weather_service`

Owns outdoor weather requests, scheduling, validation, and cached conditions.
It uses Open-Meteo over HTTPS with the ESP-IDF CA bundle and requires both an
upstream Wi-Fi connection and a valid device clock before issuing a request.
Successful data refreshes every 30 minutes; failures retry after one minute.

Settings and the latest valid response are serialized records stored through
`config_service` in the `weather_cfg` namespace. Changing coordinates clears the
old cache. Snapshots expose current conditions, daily high/low, observation
time, data age, stale state, refresh state, and the last error. The HTTP layer
consumes snapshots and delegates validated settings updates and manual refresh
requests to this component.

### `feedback_service`

Owns the active buzzer and vibration motor as two independent services:
`buzzer_service` and `vibration_service`. Each actuator service owns its GPIO,
output polarity, pulse player, stop behavior, and status snapshot.
`feedback_service` maps semantic scenes (`water`, `sedentary`, `escalated`, and
`confirm`) to actuator-specific pulse patterns and applies the current
`both`/`vibrate`/`sound`/`silent` mode.

Only one scene is played at a time. Business priority is escalation, first
sedentary alert, then water reminder, so two reminders cannot stack into a
continuous or confusing output. Patterns use bounded pulses rather than long
continuous drive. The longest buzzer pulse is 160 ms, the longest motor pulse
is 900 ms, and the longest complete motor pattern is 4350 ms with 250 ms rest
gaps between bursts.

`care_coordinator` owns cross-domain scene priority. The actuator services never
inspect sedentary, water, or care state. This keeps the buzzer and motor
reusable for future confirmations, pairing flows, or fault alerts.

### `wellness_monitor`

Owns the wellness task and composes focused modules without exposing their
mutable state to callers:

```text
pir_input -> pir_service -> sedentary_reminder --------+
                                                       |
care_guidance -> water_reminder -> care_coordinator ---+-> feedback_service
                                      |
                                      +-> RGB and display policy inputs

button_input -> button_service -> wellness actions and OLED page selection
```

`pir_input` is the GPIO adapter. `pir_service` owns sampling cadence, debounce,
stable motion edges, the 120-second presence hold, and presence-expiration
events. Consumers see only `motion`, `present`, counters, and time remaining.
This prevents every business module from rebuilding its own PIR filtering.

`button_input` is the four-key GPIO adapter. `button_service` owns active-low
input masks, 25 ms debounce, stable press state, and release-time short/long
classification. The wellness task translates those events into feedback
acknowledgement, water progress, temporary mute, persistent alert-mode
selection, and OLED page policy. The key panel remains independent from the
desk-lamp relay controller, so a display or wellness failure cannot change the
lamp's own button behavior.

The default keys are `K1=GPIO32`, `K2=GPIO33`, `K3=GPIO34`, and `K4=GPIO35`.
GPIO34 and GPIO35 are input-only pins without internal pull-ups on ESP32 and
therefore require external 10 kOhm pull-ups to 3.3 V. All keys pull to ground
when pressed. A long press is emitted only after release, which prevents an
action from repeating while the key is held.

The OLED adapter uses the same 128x64 framebuffer for both supported controller
families. The build-time controller selection configures either native
SSD1306 addressing or the SH1106 132-column RAM layout with a two-column
offset. The 1.3-inch default is SH1106. Address probing tries `0x3C` before
`0x3D`, and controller selection remains separate from I2C wiring because the
controller cannot be identified reliably by address alone.

`sedentary_reminder` remains a pure state machine driven by stable motion
evidence. `care_guidance` owns caregiver settings, daily progress, water
scheduling, and quiet periods; `water_reminder` independently owns reminder
pacing so tests can advance time without NVS, HTTP, or FreeRTOS.

`care_coordinator` receives sedentary occurrences and a water reminder event,
then chooses at most one primary scene: escalated sedentary, first sedentary
alert, or water. Soft nudges remain screen and log guidance. The coordinator
does not touch GPIO, OLED, FreeRTOS, or persistent storage, so its priority
rules are deterministic and host-testable.

Sensor-facing status is published as a snapshot, while reminder and alert
transitions are published through `app_events`. Care settings and daily
progress use versioned blobs in `config_service`.

The sedentary state machine has one external seam:

- `sedentary_update()` accepts time and motion evidence.
- `sedentary_update_result_t.snapshot` is the read-only view for the current
  phase, timers, counters, and alert state.
- Ordered `transitions` describe every phase change in that update.
- Ordered `events` carry the phase and reminder level that were active when the
  event occurred.

Transitions and events use one `order` timeline. The wellness monitor merges
both lists by `order` before publishing to `app_events`, so a consumer observes
the same sequence that the state machine recorded. For example, a valid break
followed by immediate motion produces `on_break -> idle`, then
`break_completed`, then `idle -> sitting`.

Each transition also records a stable business reason. This separates the
state transition from why it happened, so management code can distinguish a
short break return from a completed break and an absence from a timer-driven
alert. The HTTP status endpoint exposes the current stable phase name at
`wellness.sedentary.phase`.

All phase changes go through one internal transition helper. `sedentary_update()`
dispatches to one handler for each phase, and cascaded changes at the same
observed time are settled in timeline order. Add a new phase by updating the
phase enum, its handler, and its transition reasons in `sedentary_reminder.c`;
callers should continue consuming snapshots and events instead of reading
mutable state fields. Physical output is not part of the sedentary state
machine. The coordinator selects a care scene, and the wellness task adapts it
to the feedback service.

Manual OLED pages are runtime state and are intentionally not persisted. This
avoids restoring a diagnostic page such as environment or weather after a
reboot when the automatic care summary is the safer default. Temporary mute is
also runtime-only and expires after 30 minutes; the long-press alert mode is
part of care settings and is persisted through `config_service`.

### `workstation_light`

Owns the first three inputs of a four-channel relay module and exposes a small
serialized action API. Relays 1, 2, and 3 are momentary outputs on `GPIO13`,
`GPIO14`, and `GPIO23`, intended to close across the low-voltage power,
brightness, and color-temperature buttons of the lamp panel. The fourth relay
is deliberately unconfigured.

The component drives all outputs to their released level before enabling them
as outputs, then holds all three relays off on every boot. A mutex serializes
actions so two HTTP requests cannot overlap pulses. Relay 2 advances through
four brightness steps; relay 3 advances through cool, warm, and mixed color
modes. Relay 1 pulses once whenever the assumed lamp power state changes.
Snapshots expose that assumed power state, busy state, pulse timing, and cycle
counters. They intentionally do not infer the lamp panel's actual power,
brightness, or color state because the relay interface has no feedback path.
These values are runtime state only and are not persisted in NVS.

Presence automation is implemented as a small workstation task with an
injected presence-provider callback. `app_main` supplies the wellness monitor's
stable `pir_present` value, so the relay component does not depend on the
wellness component directly. The task samples every 500 ms and changes relay 1
only when presence changes: present pulses the power button, and presence
expiration pulses it again. The existing PIR service holds presence for 120
seconds after the last motion. Manual relay actions remain serialized with
automatic actions.

The same presence sample drives the black Wemos D1 R32 board's red software LED
on `GPIO2`, active high. The blue 3.3 V power LED is hard-wired and remains
uncontrollable. The status LED mirrors presence independently of relay
automation, so it is useful for checking PIR behavior even when automatic lamp
switching is disabled. Firmware initialization leaves it off before the first
PIR sample. GPIO and polarity are configurable because clone boards
occasionally use a different LED circuit.

### `http_api`

Provides an embedded mobile status homepage and local JSON endpoints over the
active interfaces. Mutable settings are handled by their owning feature
component and persisted through `config_service`; OTA endpoints are not
implemented. The web UI includes a weather card and weather settings, with
fullscreen and landscape carousel modes for mobile use. It also includes a
workstation-light card that calls the serialized relay action endpoint.
Dashboard card
visibility is validated by `http_api`, persisted through `config_service` as a
versioned blob under `web_cfg/pages`, and reported to the UI through
`web.pages` in the status response. At least one card must remain enabled.

The browser application is deliberately packaged as one HTML document. A CMake
custom command compacts and gzip-compresses it, enforces a 64 KiB compressed
budget, and embeds only the gzip artifact in flash. The original HTML is not
linked into the firmware. Page labels, descriptions, visibility switches, and
carousel controls are generated from one metadata table at runtime; this keeps
the embedded payload and future page additions from carrying parallel copies
of the same configuration.

## Flash layout

The partition table preserves the existing device layout:

| Partition | Purpose | Offset | Size |
| --- | --- | ---: | ---: |
| `otadata` | OTA selection | `0x10000` | 32 KB |
| `nvs` | Wi-Fi and application data | `0x12000` | 56 KB |
| `at_customize` | Retained vendor/custom area | `0x20000` | 896 KB |
| `ota_0` | Primary application slot | `0x100000` | 1.5 MB |
| `ota_1` | Secondary OTA slot | `0x280000` | 1.5 MB |

## Next modules

Add new capabilities as components instead of expanding `app_main.c`:

- `device_control` for board-specific I/O
- `ota_manager` for image download and rollback policy
- `mqtt_client` or a cloud transport as needed
