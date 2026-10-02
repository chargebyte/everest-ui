# EVerest Web UI API

This document describes the API implemented by this source tree. It is an implementation reference, not a versioned stability contract. The protocol constants are in `backend/api/ProtocolSchema.hpp`.

## Network interface

`webserver` listens on the configured HTTP address (default `0.0.0.0:80`) and upgrades `GET /ws` to WebSocket. The browser-facing URL is `ws://<same-host>:<port>/ws`, or `wss://<same-host>:<port>/ws` when the page is served over HTTPS. The path and defaults are in `backend/webserver/config/frontend.conf.in`.

The webserver proxies the upgraded connection internally to `ws://127.0.0.1:9002` by default (`backend_ws` in the frontend config; backend listener port is `backend_port` in `backend/api/config/backend.conf`). A browser normally connects only to the webserver. Text and binary WebSocket messages are forwarded unchanged. Only one UI session is accepted.

## Authentication

Before opening `/ws`, use the HTTP endpoints:

```http
GET  /auth/status
POST /auth/setup   {"username":"...", "password":"..."}
POST /auth/login   {"username":"...", "password":"..."}
POST /auth/logout
```

Login sets the HttpOnly, SameSite=Strict cookie `everest_ui_session`; non-browser clients must retain and send it during the WebSocket handshake. `/auth/status` returns `setupRequired`, `authenticated`, `uiBusy`, and `appTitle`. Authentication and upgrade behavior are implemented in `backend/webserver/http/StaticServer.cpp`.

Request and response templates: [`auth.status`](examples/auth.status.request.json) ([response](examples/auth.status.response.json)), [`auth.setup`](examples/auth.setup.request.json) ([success](examples/auth.setup.response.json), [error](examples/auth.setup.response.error.json)), [`auth.login`](examples/auth.login.request.json) ([success](examples/auth.login.response.json), [error](examples/auth.login.response.error.json)), and [`auth.logout`](examples/auth.logout.request.json) ([response](examples/auth.logout.response.json)). These files wrap the HTTP method, path, headers, and body for requests; the `http_status`, headers, and `body` fields document the HTTP response. Only `body` is the JSON response body on the wire. `null` means the endpoint has no request body.

## Request

Each request is one WebSocket text message containing a JSON object, for example [`everest.read_config_parameters`](examples/everest.read_config_parameters.request.json):

`requestId` must be numeric in the C++ backend. `group` and `action` are strings, and `parameters` is an object. Requests are parsed and routed by `backend/api/RequestHandler.cpp`.

The frontend helper `public/js/protocol/requestBuilder.js` creates this envelope. A dotted catalog path becomes nested JSON: `Profinet.device` with value `eth2` becomes `{"Profinet":{"device":"eth2"}}`.

## Response

The matching JSON response envelope is shown in [`everest.read_config_parameters`](examples/everest.read_config_parameters.response.success.json). Its `parameters` value is action-specific; this example contains the requested Profinet setting and `_available_modules`.

`ok` is the operation result. `final:false` means more messages follow. `requestId` correlates to the request. `type` is `<group>.<action>.result`, `.ack`, or `.error`. `parameters` contains result data or an error object. `responseId` is a server delivery id. Response construction is in `backend/api/libs/ResponseBuilder.cpp`.

Every response with `responseId` must be acknowledged:

```json
{"type":"ack","responseId":"r-1"}
```

The backend sends one response at a time and retransmits an unacknowledged response after two seconds. The browser implementation is in `public/js/transport.js`.

Malformed JSON returns `request.parse.error` with `{"error":"invalid_json"}`. A missing or wrongly typed request field returns `request.template.error` with `{"error":"invalid_template"}`. An unknown group returns an error with `{"error":"unsupported_group"}`.

## Groups and actions

| Group | Actions |
| --- | --- |
| `pcap` | `read_interfaces`, `write`, `read` |
| `everest` | `read_config_parameters`, `write_config_parameters`, `download_config`, `upload_config` |
| `safety` | `read_settings`, `write_settings` |
| `ocpp` | `read_settings`, `write_settings` |
| `firmware` | `read_version`, `upload_image.start`, `upload_image.chunk`, `upload_image.finish`, `update_image`, `reboot` |
| `system_logs` | `read`, `download`, `extract` |
| `network` | `read_interfaces`, `read_settings`, `write_settings`, `reset_settings`, `cancel_reset_settings`, `apply` |
| `system` | `read_app_title` |

Names are defined in `backend/api/ProtocolSchema.hpp`; action-specific fields, results, and errors are in `backend/api/modules/*.cpp`. There is no OpenAPI or JSON Schema document.

### Network interface classification

The backend configuration key `network_iso_high_level_comms_drivers` is a comma-separated list of Linux kernel driver names used to identify interfaces for ISO high level communications (for example `mse102x,qcaspi`). It is shared by the `network.read_interfaces` and `pcap.read_interfaces` paths; it is not a PCAP-only setting.

`network.read_interfaces` returns `probably_iso_high_level_comms`, which means the interface's sysfs driver matches a configured name (case-insensitively). `pcap.read_interfaces` returns `likely_iso_high_level_comms`, a stricter suggestion requiring a matching driver, an operational non-loopback interface that is not a bridge member, and a link-local IPv6 address. These are heuristic indicators, not definitive protocol detection. The PCAP response additionally provides a human-readable `recommendation` when the stricter heuristic matches. See the [network](examples/network.read_interfaces.response.success.json) and [PCAP](examples/pcap.read_interfaces.response.success.json) response templates.

`network_device_whitelist.<compatible>` entries in `backend/api/config/backend.conf` map exact `/proc/device-tree/compatible` strings to comma-separated exact Linux interface names, for example `network_device_whitelist.chargebyte,imx93-charge-control-y=eth0,qca`. Compatible strings are read as NUL-delimited entries; every matching configured entry contributes its names to the allowed set. If the device-tree file cannot be read, or no configured compatible string matches, no whitelist is applied. A matching empty list permits no names. `expert_mode` is an optional boolean request parameter, defaulting to false; when true it bypasses only this platform whitelist. The existing type restrictions still apply: interfaces reported as `ether` and `bridge` expose IPv4 configuration; `can` exposes only CAN bitrate; other kinds remain masked. The backend filters `read_interfaces` and rechecks names on every per-interface action, so filtering is not merely a frontend behavior. The frontend contains hidden expert-mode state and refreshes interface discovery when it changes; it does not expose a user-facing switch.

## Important payloads

### Network settings

`network.read_settings` reports the selected main file in `network_file`, as reported by `networkctl status`. It reads that file and applicable systemd `.network.d/*.conf` drop-ins to populate the editor. Kinds `ether` and `bridge` expose IPv4 configuration; `can` exposes only CAN bitrate; loopback and other kinds remain masked. CAN reads return `can_bitrate` as integer bits per second or `null`, with `can_bitrate_source` set to `networkd`, `kernel`, or `unknown`, and `can_bitrate_override` indicating a bitrate in the UI-owned drop-in. The systemd setting takes precedence; if absent, the backend queries Linux link details with `ip -details -json`.

For Ethernet and bridge, the IPv4 mode is represented by `dhcp_ipv4`; static mode sends `ipv4_address` as a bare IPv4 address and `ipv4_prefix_length` as an integer from 0 through 32. DHCP mode sends an empty address and no static gateway or DNS values. Read and write payloads do not expose a fallback address; the backend preserves any existing fallback address when an overlay resets the configured address list. CAN writes use `can_bitrate` as an integer from 1 through 4294967295 and only modify `[CAN] BitRate=`. CAN reset removes only that directive, retaining unrelated UI drop-in settings. `network.write_settings` writes only settings that differ from the parsed underlay to `overlay_file`, using `/etc/systemd/network/<selected-network-filename>.d/50-everest-ui.conf` (for example, `10-wired.network.d/50-everest-ui.conf`); it never copies or rewrites the selected main file. `network_file` in the write result continues to identify the selected source file. `user_override` is true only when the overlay has the EVerest UI ownership marker. If status reports no selected network file, settings are not editable. All network actions accept optional `expert_mode`; pass true consistently when expert discovery is enabled.

The CAN editor offers the eight CANopen CC standard bitrates (10, 20, 50, 125, 250, 500, 800, and 1000 kbit/s) plus Custom. Custom input accepts an unsigned integer optionally suffixed by `k` or `M`, using decimal multipliers of 1000 and 1000000; all write requests carry the normalized integer in bits per second. A CAN interface without a selected network file is read-only because there is no reliable selected-file drop-in target for Save or Reset.

An existing unmarked `50-everest-ui.conf` in `/etc` is adopted and replaced when settings are saved. This is intentional implicit adoption: there is no separate confirmation or backup. IPv4 writes reconstruct the overlay from settings managed by the editor, so directives in an unmarked target that the editor does not manage may be lost; back up or move such a file before saving if it contains other configuration. CAN bitrate writes preserve other directives in the target. The successful write response marks the resulting file as UI-owned (`user_override: true`). If a lower-priority directory has that filename but `/etc` does not, the write is rejected rather than shadowing the lower-priority file. Reset removes only a marked overlay; it does not remove an unmarked overlay or a main `/etc/*.network` file. Current legacy main files have no ownership marker and are therefore preserved. Reset is staged until `network.apply`; an unmarked overlay causes `network_config_unowned_dropin` and is left unchanged. When replacing list-valued settings, systemd empty assignments clear prior values before the requested values are added. Edits that cannot preserve unsupported address or route configuration are rejected as `unsupported_network_configuration`.

### EVerest configuration

Read and write use nested module/parameter objects:

```json
{"requestId":1,"group":"everest","action":"read_config_parameters","parameters":{"Profinet":{"device":null}}}
{"requestId":2,"group":"everest","action":"write_config_parameters","parameters":{"Profinet":{"device":"eth2"}}}
```

The read response contains requested values and `_available_modules`. `download_config` returns `file` and `config_yaml`. `upload_config` expects `file_name` and `config_yaml`, validates it, installs it, and may restart EVerest. The UI catalog `public/config/parameter_catalog.json` supplies display metadata and `backend_path`; it does not define backend validation.

## Example templates

WebSocket template filenames use `<group>.<action>.request.json`, `<group>.<action>.response.success.json`, and `<group>.<action>.response.error.json`. Each request and response file contains the actual JSON message sent on the WebSocket. The error response uses `<action-specific-error-code>` as a placeholder; see the module implementation for possible concrete errors. Templates use `requestId: 42`; response `requestId` values must match the request.

| Action | Request | Success response | Error response |
| --- | --- | --- | --- |
| `pcap.read_interfaces` | [request](examples/pcap.read_interfaces.request.json) | [response](examples/pcap.read_interfaces.response.success.json) | [response](examples/pcap.read_interfaces.response.error.json) |
| `pcap.write` | [request](examples/pcap.write.request.json) | [response](examples/pcap.write.response.success.json) | [response](examples/pcap.write.response.error.json) |
| `pcap.read` | [request](examples/pcap.read.request.json) | [response](examples/pcap.read.response.success.json) | [response](examples/pcap.read.response.error.json) |
| `everest.read_config_parameters` | [request](examples/everest.read_config_parameters.request.json) | [response](examples/everest.read_config_parameters.response.success.json) | [response](examples/everest.read_config_parameters.response.error.json) |
| `everest.write_config_parameters` | [request](examples/everest.write_config_parameters.request.json) | [response](examples/everest.write_config_parameters.response.success.json) | [response](examples/everest.write_config_parameters.response.error.json) |
| `everest.download_config` | [request](examples/everest.download_config.request.json) | [response](examples/everest.download_config.response.success.json) | [response](examples/everest.download_config.response.error.json) |
| `everest.upload_config` | [request](examples/everest.upload_config.request.json) | [response](examples/everest.upload_config.response.success.json) | [response](examples/everest.upload_config.response.error.json) |
| `safety.read_settings` | [request](examples/safety.read_settings.request.json) | [response](examples/safety.read_settings.response.success.json) | [response](examples/safety.read_settings.response.error.json) |
| `safety.write_settings` | [request](examples/safety.write_settings.request.json) | [response](examples/safety.write_settings.response.success.json) | [response](examples/safety.write_settings.response.error.json) |
| `ocpp.read_settings` | [request](examples/ocpp.read_settings.request.json) | [response](examples/ocpp.read_settings.response.success.json) | [response](examples/ocpp.read_settings.response.error.json) |
| `ocpp.write_settings` | [request](examples/ocpp.write_settings.request.json) | [response](examples/ocpp.write_settings.response.success.json) | [response](examples/ocpp.write_settings.response.error.json) |
| `firmware.read_version` | [request](examples/firmware.read_version.request.json) | [response](examples/firmware.read_version.response.success.json) | [response](examples/firmware.read_version.response.error.json) |
| `firmware.upload_image.start` | [request](examples/firmware.upload_image.start.request.json) | [response](examples/firmware.upload_image.start.response.success.json) | [response](examples/firmware.upload_image.start.response.error.json) |
| `firmware.upload_image.chunk` | [request](examples/firmware.upload_image.chunk.request.json) | [response](examples/firmware.upload_image.chunk.response.success.json) | [response](examples/firmware.upload_image.chunk.response.error.json) |
| `firmware.upload_image.finish` | [request](examples/firmware.upload_image.finish.request.json) | [response](examples/firmware.upload_image.finish.response.success.json) | [response](examples/firmware.upload_image.finish.response.error.json) |
| `firmware.update_image` | [request](examples/firmware.update_image.request.json) | [final response](examples/firmware.update_image.response.success.json), [progress](examples/firmware.update_image.response.progress.json) | [response](examples/firmware.update_image.response.error.json) |
| `firmware.reboot` | [request](examples/firmware.reboot.request.json) | [response](examples/firmware.reboot.response.success.json) | [response](examples/firmware.reboot.response.error.json) |
| `system_logs.read` | [request](examples/system_logs.read.request.json) | [response](examples/system_logs.read.response.success.json) | [response](examples/system_logs.read.response.error.json) |
| `system_logs.download` | [request](examples/system_logs.download.request.json) | [response](examples/system_logs.download.response.success.json) | [response](examples/system_logs.download.response.error.json) |
| `system_logs.extract` | [request](examples/system_logs.extract.request.json) | [response](examples/system_logs.extract.response.success.json) | [response](examples/system_logs.extract.response.error.json) |
| `network.read_interfaces` | [request](examples/network.read_interfaces.request.json) | [response](examples/network.read_interfaces.response.success.json) | [response](examples/network.read_interfaces.response.error.json) |
| `network.read_settings` | [request](examples/network.read_settings.request.json) | [response](examples/network.read_settings.response.success.json) | [response](examples/network.read_settings.response.error.json) |
| `network.write_settings` | [request](examples/network.write_settings.request.json) | [response](examples/network.write_settings.response.success.json) | [response](examples/network.write_settings.response.error.json) |
| `network.read_settings` (CAN) | [request](examples/network.read_settings.can.request.json) | [response](examples/network.read_settings.can.response.success.json) | - |
| `network.write_settings` (CAN) | [request](examples/network.write_settings.can.request.json) | [response](examples/network.write_settings.can.response.success.json) | - |
| `network.reset_settings` | [request](examples/network.reset_settings.request.json) | [response](examples/network.reset_settings.response.success.json) | [response](examples/network.reset_settings.response.error.json) |
| `network.cancel_reset_settings` | [request](examples/network.cancel_reset_settings.request.json) | [response](examples/network.cancel_reset_settings.response.success.json) | [response](examples/network.cancel_reset_settings.response.error.json) |
| `network.apply` | [request](examples/network.apply.request.json) | [response](examples/network.apply.response.success.json) | [response](examples/network.apply.response.error.json) |
| `system.read_app_title` | [request](examples/system.read_app_title.request.json) | [response](examples/system.read_app_title.response.success.json) | [response](examples/system.read_app_title.response.error.json) |

The transport acknowledgements are not group actions: see [`response ack`](examples/transport.response-ack.request.json) and [`PCAP chunk ack`](examples/pcap.chunk-ack.request.json). PCAP binary chunks cannot be represented as JSON; their wire header is described below. Firmware update progress and final responses share one request id and use distinct `type` values.

### Firmware upload

Start:

```json
{"image":{"file_name":"image.raucb","size_bytes":123456,"chunk_count":3,"chunk_size_bytes":65536}}
```

Chunks must be sent in order from zero:

```json
{"image":{"chunk_index":0,"dataB64":"<base64 bytes>"}}
```

Finish with the SHA-256 of the complete file:

```json
{"image":{"sha256":"<lowercase hex sha256>"}}
```

Only a successful finish permits `firmware.update_image`. Installation emits `update_image.progress` responses and then a final `update_image` response. See `backend/api/modules/FirmwareUpdateRuntime.cpp`.

### PCAP transfer

`pcap.read` first returns a non-final JSON response such as:

```json
{"file":"capture.pcap","transfer":"binary","chunk_size_bytes":65536,"size_bytes":123456}
```

After acknowledging it, binary WebSocket messages follow. The big-endian header is:

| Bytes | Field |
| ---: | --- |
| 0..3 | ASCII `PCAP` |
| 4 | Version, currently `1` |
| 5 | Final flag (`0` or `1`) |
| 6..7 | Reserved, zero |
| 8..15 | Request id, unsigned 64-bit |
| 16..19 | Chunk sequence, unsigned 32-bit |
| 20..end | Capture bytes |

After each chunk send `{"type":"pcap.chunk_ack","requestId":42,"sequence":0}`. The next chunk is sent only after this acknowledgement. Framing and sequencing are in `backend/api/RequestHandler.cpp` and `backend/api/modules/PCAP.cpp`.

## Underlying EVerest RPC

Some UI actions use a separate internal backend-to-EVerest WebSocket RPC connection. The backend reads the EVerest YAML, locates the configured `rpc_api` module, derives its URL, and performs an EVerest hello handshake. This is not the browser API; see `backend/api/RpcApiClient.cpp`.

## Current limitations

- There is no negotiated protocol version, formal schema, or capability-discovery message.
- Error strings are implementation values; the list in `ProtocolSchema.hpp` is not exhaustive.
- Several payloads are open-ended because they represent EVerest YAML or controller configuration.
- The browser has a 50-second pending-request timeout; backend response delivery uses a two-second acknowledgement timeout.
- Requests are serialized by the backend control path, and only one UI WebSocket session is accepted.

Source of truth: `backend/api/ProtocolSchema.hpp`, `backend/api/RequestHandler.cpp`, `backend/api/libs/ResponseBuilder.cpp`, `backend/api/modules/*.cpp`, `backend/webserver/http/StaticServer.cpp`, `backend/webserver/ws/WebSocketProxySession.cpp`, and `public/js/transport.js`.
