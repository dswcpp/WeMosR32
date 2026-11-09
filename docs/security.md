# Security baseline

This project is a development framework. It is not a production security
configuration until the items below are addressed for the target deployment.

## Current controls

- Status, time, and Wi-Fi scan responses do not expose saved credentials.
- Wi-Fi, care settings, water progress, and manual time changes use local HTTP
  endpoints. These mutating endpoints do not currently require authentication.
- Wi-Fi and care settings are persisted in NVS through `config_service`. NVS is
  not encrypted in the default configuration.
- `/api/v1/status` reports NVS initialization and recovery state so storage
  faults are observable without exposing saved credentials.
- The SoftAP uses WPA2-PSK when a password is configured. The development
  default is public and must be replaced before deployment.
- OTA endpoints are intentionally not implemented.
- Wi-Fi, IP, and HTTP errors are logged without passwords.
- The generated `sdkconfig` is excluded from version control.
- The HTTP server limits concurrent sockets and disables response caching.
- The framework targets a maintained ESP-IDF 5.x release rather than the
  unsupported ESP-IDF 3.0.3 used by the captured firmware.

## Required before production

### Credentials

The SoftAP password in Kconfig is a known development default. Replace it
through `idf.py menuconfig` or implement first-boot provisioning. Do not ship a
firmware image containing a common password.

Use NVS encryption or a protected provisioning partition for production
credentials. Avoid sending Wi-Fi passwords over an untrusted network until the
local HTTP interface is protected with TLS. Do not place device-unique secrets
in source code or commit a generated `sdkconfig`.

Flash encryption and secure boot are device-provisioning decisions and are not
enabled by the development defaults in this repository. Enabling them changes
the boot and key provisioning model and can be irreversible on a physical
device. Validate the complete provisioning, recovery, OTA, and manufacturing
flow on representative hardware before enabling either option in production.

### Network exposure

Status responses are intentionally unauthenticated and contain no credentials.
The existing endpoints that change Wi-Fi, care settings, water progress, or
time must be protected with authentication before the device is used on an
untrusted network. Any future endpoint that controls hardware or starts an OTA
update must also enforce authorization, request validation, and replay
protection.

Do not expose the development HTTP server directly to an untrusted WAN. For
remote access, place the device behind an authenticated gateway or use TLS with
server and client identity as appropriate.

### OTA

The two OTA partitions are only a storage layout. They do not provide update
authenticity. Before enabling OTA downloads:

- Enable secure boot and flash encryption for production devices.
- Sign application images and verify signatures before activation.
- Serve images over authenticated TLS.
- Pin the update host or public key.
- Keep the previous image bootable and define an explicit rollback policy.

### Debug access

Ship production firmware with UART and JTAG access disabled or protected by
device policy. Avoid logging Wi-Fi passwords, provisioning tokens, or private
keys. Treat physical access to the device as potentially hostile.

### Dependency maintenance

Track the selected ESP-IDF release and update it for security fixes. Build from
a pinned release in CI, and keep build tools and Python dependencies
reproducible.

