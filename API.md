# EVerest Web UI API

This document describes the API implemented by this source tree. It is an implementation reference, not a versioned stability contract. The protocol constants are in `backend/api/ProtocolSchema.hpp`.

## Network interface

`webserver` listens on the configured HTTP address (default `0.0.0.0:80`) and upgrades `GET /ws` to WebSocket. The browser endpoint is `ws://<same-host>:<port>/ws`; defaults are in `backend/webserver/config/frontend.conf.in`.

The webserver proxies the upgraded connection to the backend WebSocket server, which listens on `backend_port` (default `9002`, from `backend/api/config/backend.conf`). A browser normally connects only to the webserver. Text and binary WebSocket messages are forwarded unchanged. Only one UI session is accepted.

## Authentication

Before opening `/ws`, use the HTTP endpoints:

```http
GET  /auth/status
POST /auth/setup   {"username":"...", "password":"..."}
POST /auth/login   {"username":"...", "password":"..."}
POST /auth/logout
```

Login sets the HttpOnly, SameSite=Strict cookie `everest_ui_session`; non-browser clients must retain and send it during the WebSocket handshake. `/auth/status` returns `setupRequired`, `authenticated`, `uiBusy`, and `appTitle`. Authentication and upgrade behavior are implemented in `backend/webserver/http/StaticServer.cpp`.

## Request

Each request is one WebSocket text message containing:

```json
{"requestId":42,"group":"everest","action":"read_config_parameters","parameters":{}}
```

`requestId` must be numeric in the C++ backend. `group` and `action` are strings, and `parameters` is an object. Requests are parsed and routed by `backend/api/RequestHandler.cpp`.

The frontend helper `public/js/protocol/requestBuilder.js` creates this envelope. A dotted catalog path becomes nested JSON: `Profinet.device` with value `eth2` becomes `{"Profinet":{"device":"eth2"}}`.

## Response

```json
{"ok":true,"final":true,"requestId":42,"type":"everest.read_config_parameters.result","parameters":{},"responseId":"r-1"}
```

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

## Important payloads

### EVerest configuration

Read and write use nested module/parameter objects:

```json
{"requestId":1,"group":"everest","action":"read_config_parameters","parameters":{"Profinet":{"device":null}}}
{"requestId":2,"group":"everest","action":"write_config_parameters","parameters":{"Profinet":{"device":"eth2"}}}
```

The read response contains requested values and `available_modules`. `download_config` returns `file` and `config_yaml`. `upload_config` expects `file_name` and `config_yaml`, validates it, installs it, and may restart EVerest. The UI catalog `public/config/parameter_catalog.json` supplies display metadata and `backend_path`; it does not define backend validation.

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
