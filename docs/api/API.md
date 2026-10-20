# EVerest Web UI API

This document describes the API implemented by this source tree. It is an implementation reference, not a versioned stability contract. The protocol constants are in `backend/api/ProtocolSchema.hpp`.

## Runtime Configuration

The launcher/API read distribution `backend.conf` and the webserver reads distribution `frontend.conf` from
`${prefix}/share/everest-ui/config/` (normally `/usr/share/everest-ui/config/` in distribution builds), then
apply the corresponding `/etc/everest-ui/` file key by key. Local `key=value` replaces a value, `key=` is
explicitly empty, and a standalone `!key` removes an inherited key. Removing the local directive restores
inheritance. Existing complete local files remain valid and are preserved during installation. The shared
overlay writer writes only local directives; there is no new browser API for editing these files. Protocol
payloads are unchanged. See [Configuration Layers](../configuration.md) for fallback semantics, migration,
relative paths, runtime reload/error behavior, and package-update preservation requirements.

## Network interface

The `everest-ui` launcher starts the `everest-ui-webserver` and `everest-ui-api` helpers from
`${prefix}/libexec/everest-ui/`. The systemd unit is `everest-ui.service`.

`everest-ui-webserver` listens on the configured HTTP address (default `0.0.0.0:80`) and upgrades `GET /ws` to WebSocket. The browser-facing URL is `ws://<same-host>:<port>/ws`, or `wss://<same-host>:<port>/ws` when the page is served over HTTPS. The path and defaults are in `backend/webserver/config/frontend.conf.in`.

The webserver proxies the upgraded connection internally to `ws://127.0.0.1:9002` by default (`backend_ws` in the frontend config; backend listener port is `backend_port` in `backend/api/config/backend.conf`). The backend binds only to IPv4 loopback and is not reachable from the network. A browser must connect through the authenticated webserver. Text and binary WebSocket messages are forwarded unchanged. Only one UI session is accepted.

## Authentication

Before opening `/ws`, use the HTTP endpoints:

```http
GET  /auth/status
POST /auth/setup   {"username":"...", "password":"..."}
POST /auth/login   {"username":"...", "password":"..."}
POST /auth/reset   {"confirm":true}
POST /auth/logout
```

Login sets the HttpOnly, SameSite=Strict cookie `everest_ui_session`; non-browser clients must retain and send it during the WebSocket handshake. `/auth/status` returns `setupRequired`, `authenticated`, `uiBusy`, `appTitle`, `bootId`, and `passwordReset`. `uiBusy` reports occupancy only for an authenticated requester. `bootId` is the device's Linux boot UUID, stable across service restarts but different after a device reboot; it is empty if unavailable. It is public recovery metadata, not a session token.

### Password recovery lifecycle

Recovery is available once per boot, within the configured window after the first `everest-ui-webserver` start, only when the boot marker permits cold-boot recovery. Regular software reboots do not enable it. Restarting or stopping/starting `everest-ui.service` neither extends the deadline nor restores a consumed allowance. Unusable boot information or runtime state leaves recovery unavailable. See [recovery configuration and kernel requirements](../../README.md#configuration-and-kernel-requirements) for the reset window, boot marker, and `RUNTIME_DIRECTORY` contract.

1. Read `/auth/status` to determine whether reset is available. The browser may count down locally, but the server checks the deadline again when reset is requested.
2. Send `POST /auth/reset` with `{"confirm":true}`. No authenticated session or existing password is required. Success removes credentials and all sessions, clears the requester's cookie, and closes active browser WebSockets with code `1008` (policy violation) and reason `credentials reset`. Forwarding to the backend is stopped and queued proxy messages are discarded; this is not cancellation of operations already accepted by the backend. An upgrade whose session became invalid is also closed with code `1008`, reason `session expired`.
3. `/auth/status` now reports `setupRequired: true`. Send `POST /auth/setup` with `username` and `password` to create replacement credentials. Setup is not limited by the recovery deadline. Success returns `{"success":true}` but does not set a session cookie.
4. Send `POST /auth/login` with the new credentials to obtain a cookie, then connect to `/ws`. The browser performs login automatically after setup; setup itself does not authenticate the client.

If credential deletion fails, existing credentials remain usable, while the reset allowance stays closed once consumed. Clients should read status after a storage error rather than assume reset can safely be repeated. A `503` lock conflict can be retried while the original window remains open; it does not extend the window.

### Recovery status

| `passwordReset` field | Type | Meaning |
| --- | --- | --- |
| `available` | boolean | Whether a reset may currently be attempted. The operation can still fail if eligibility changes or the lock is busy. |
| `remainingSeconds` | number | Seconds until the deadline; may be fractional. Zero whenever unavailable. |
| `windowSeconds` | integer | Configured window length, normally `60`; `0` disables recovery. |
| `reason` | string | Empty while available; otherwise one of the reasons below. |

If credentials do not exist, `setup_required` overrides every other reason. Otherwise the first applicable reason wins in the following order:

| Reason | Meaning | Status example |
| --- | --- | --- |
| `disabled` | Recovery is disabled or initialization could not enable it, for example because `RUNTIME_DIRECTORY` is missing. | [disabled](examples/auth.status.response.disabled.json) |
| `boot_info_invalid` | Boot classification could not be established. | [invalid boot information](examples/auth.status.response.boot_info_invalid.json) |
| `warm_boot` | The boot marker identifies a regular software reboot. | [warm boot](examples/auth.status.response.warm_boot.json) |
| `unavailable_this_boot` | Allowance consumed or blocked, state invalid, marker clearing pending, or another runtime prerequisite failed. | [unavailable this boot](examples/auth.status.response.unavailable_this_boot.json) |
| `expired` | An otherwise eligible recovery window has ended. | [expired](examples/auth.status.response.expired.json) |

Also see [available](examples/auth.status.response.json) and [setup required](examples/auth.status.response.setup_required.json). These reasons report the implementation's current state, not a complete diagnosis of an underlying failure.

### Endpoint results

All rows in this table have JSON bodies. Error bodies are `{"error":"<identifier>"}`; successful setup/reset bodies are `{"success":true}`.

| Endpoint | HTTP status | Result | Example |
| --- | --- | --- | --- |
| `/auth/reset` | `200` | Credentials/sessions removed; session cookie cleared. | [success](examples/auth.reset.response.json) |
| `/auth/reset` | `400` | `reset_confirmation_required`: invalid JSON, non-object body, or `confirm` not boolean `true`. | [confirmation required](examples/auth.reset.response.confirmation_required.json) |
| `/auth/reset` | `403` | `reset_unavailable`: reset not currently eligible, including when no user exists. | [unavailable](examples/auth.reset.response.error.json) |
| `/auth/reset` | `503` | `reset_temporarily_unavailable`: reset lock could not be acquired. | [temporary conflict](examples/auth.reset.response.temporarily_unavailable.json) |
| `/auth/reset` | `500` | `reset_failed`: persisting state or removing credentials failed. | [storage failure](examples/auth.reset.response.failed.json) |
| `/auth/setup` | `200` | Credentials created; no session cookie issued. | [success](examples/auth.setup.response.json) |
| `/auth/setup` | `409` | `setup_not_required`: credentials already exist. | [not required](examples/auth.setup.response.error.json) |

Existing setup validation also returns `400` for `invalid_json`, `missing_credentials`, or the credential/storage validation error supplied by the authentication manager. These endpoint results are separate from shared validation failures below.

### Shared setup/reset validation

Both `/auth/setup` and `/auth/reset` validate `Host`: it must be an IP literal, `localhost`, the device hostname, its `.local` name, the hostname configured by `allow_origin`, or an entry in `allowed_hosts`. Any syntactically valid IP literal is accepted; the server does not check whether it belongs to a local interface. Hostnames are compared against this allowlist without DNS resolution. A port may be supplied in Host, and IPv6 authorities use brackets. The Host allowlist does not apply to other HTTP routes or WebSocket upgrades.

Both POST endpoints require `Content-Type: application/json`; valid media-type parameters such as `charset=utf-8` are accepted. `Origin` may be omitted. When supplied, it must be an `http://` origin matching the Host hostname and effective port (default `80`); paths, `https://`, and literal `null` are rejected. Forwarded headers and TLS-terminating reverse proxies are not supported by this policy.

Validation order is HTTP framing/parsing, Host validation for these paths, method validation (`POST` required), Content-Type, Origin, then endpoint body and operation checks. Thus malformed requests can fail before any JSON endpoint error is generated. Unsupported methods return plain-text `405 Method Not Allowed` after Host validation.

| HTTP status | Body type | Body/error identifier | Shared example |
| --- | --- | --- | --- |
| `400` | text/plain | `Invalid Host` | [invalid Host](examples/auth.mutation.response.invalid_host.json) |
| `421` | application/json | `host_not_allowed` | [unlisted Host](examples/auth.mutation.response.host_not_allowed.json) |
| `415` | application/json | `unsupported_media_type` | [unsupported media type](examples/auth.mutation.response.unsupported_media_type.json) |
| `403` | application/json | `origin_forbidden` | [disallowed Origin](examples/auth.mutation.response.origin_forbidden.json) |
| `500` | application/json | `internal_error`: internal Content-Type validator failure | [internal validation failure](examples/auth.mutation.response.internal_error.json) |

The HTTP parser additionally rejects the following requests on all routes with `400` and a **plain-text** body, not a JSON error object:

| Rejection | Exact body | Example |
| --- | --- | --- |
| Header line starting with space/tab | `Folded header is not supported` | [folded header](examples/http.response.folded_header.json) |
| Missing colon or invalid header-name characters | `Invalid header name` | [invalid header name](examples/http.response.invalid_header_name.json) |
| Repeated Host, Origin, Content-Type, or Content-Length (case-insensitive) | `Duplicate header` | [duplicate header](examples/http.response.duplicate_header.json) |
| Any Transfer-Encoding header | `Transfer-Encoding is not supported` | [unsupported transfer encoding](examples/http.response.transfer_encoding.json) |

Framing checks precede these parser checks; an invalid Content-Length, for example, can be rejected first with `Invalid Content-Length`.

### Authentication templates

Requests: [status](examples/auth.status.request.json), [setup](examples/auth.setup.request.json), [login](examples/auth.login.request.json), [reset](examples/auth.reset.request.json), and [logout](examples/auth.logout.request.json). Responses: status variants and setup/reset results above, [login success](examples/auth.login.response.json), [login error](examples/auth.login.response.error.json), and [logout success](examples/auth.logout.response.json).

These files wrap the HTTP method, path, headers, and body for requests; `http_status`, headers, and `body` describe responses. Headers shown are illustrative subsets; clients must supply normal HTTP framing, including Content-Length for bodies. Only `body` is sent as the payload: an object for JSON responses, or the literal string contents for plain-text responses (without JSON quotes). `null` denotes an absent request body. The shared `auth.mutation.*` responses apply to both setup and reset; `http.response.*` examples apply across routes. WebSocket close codes/reasons above describe control frames, not JSON messages.

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
| `factory_reset` | `read_status`, `reset` |
| `system_logs` | `read`, `download`, `extract` |
| `network` | `read_interfaces`, `read_settings`, `write_settings`, `reset_settings`, `cancel_reset_settings`, `apply` |
| `ssh` | `read`, `enable`, `disable`, `set_password` |
| `system` | `read_app_title`, `read_reboot_settings` |

Names are defined in `backend/api/ProtocolSchema.hpp`; action-specific fields, results, and errors are in `backend/api/modules/*.cpp`. There is no OpenAPI or JSON Schema document.

### Network interface classification

The backend configuration key `network_iso_high_level_comms_drivers` is a comma-separated list of Linux kernel driver names used to identify interfaces for ISO high level communications (for example `mse102x,qcaspi`). It is shared by the `network.read_interfaces` and `pcap.read_interfaces` paths; it is not a PCAP-only setting.

`network.read_interfaces` returns `probably_iso_high_level_comms`, which means the interface's sysfs driver matches a configured name (case-insensitively). `pcap.read_interfaces` returns `likely_iso_high_level_comms`, a stricter suggestion requiring a matching driver, an operational non-loopback interface that is not a bridge member, and a link-local IPv6 address. These are heuristic indicators, not definitive protocol detection. The PCAP response additionally provides a human-readable `recommendation` when the stricter heuristic matches. See the [network](examples/network.read_interfaces.response.success.json) and [PCAP](examples/pcap.read_interfaces.response.success.json) response templates.

`network_device_whitelist.<compatible>` entries in `backend/api/config/backend.conf` map exact `/proc/device-tree/compatible` strings to comma-separated exact Linux interface names, for example `network_device_whitelist.chargebyte,imx93-charge-control-y=eth0,qca`. Compatible strings are read as NUL-delimited entries; every matching configured entry contributes its names to the allowed set. If the device-tree file cannot be read, or no configured compatible string matches, no whitelist is applied. A matching empty list permits no names. `expert_mode` is an optional boolean request parameter, defaulting to false; when true it bypasses only this platform whitelist. The existing type restrictions still apply: interfaces reported as `ether` and `bridge` expose IPv4 configuration; `can` exposes only CAN bitrate; other kinds remain masked. The backend filters `read_interfaces` and rechecks names on every per-interface action, so filtering is not merely a frontend behavior. The frontend contains hidden expert-mode state and refreshes interface discovery when it changes; it does not expose a user-facing switch.

### Safety Controller settings

E-stop `estops_<index>.enabled` is a string enum: `active-low`, `active-high`, or `disabled`; the historical key name does not indicate a boolean. Contactor `contactors_<index>.type` accepts `disabled`, `without-feedback`, `with-feedback-normally-open`, or `with-feedback-normally-closed`. Visibility remains controlled by `safety_controller_available_settings`; clients must not assume every group is exposed.

PT1000 channels use `overtemperature-protection` (boolean), `abort-temperature` (degrees Celsius), and `resistance-offset` (ohms). Under **Overtemperature Protection**, each channel has an Enable checkbox and two numeric columns. Unknown values are returned as empty strings, not zeroes or omitted keys. Unchecking the checkbox disables those inputs without clearing them; Save still includes both fields. The hardware YAML encodes an off channel as `disabled`, so submitted numeric values do not enable it and are retained only in the open page. No preference storage is introduced: after a reload, unknown values remain empty because the former threshold cannot be recovered from hardware. E-stops are presented as labeled rows with one dropdown per channel. These presentation changes do not rename API fields.

For submitted, permitted PT1000 entries with protection enabled, both numeric fields are mandatory and must contain finite JSON numbers or numeric strings. Missing, null, empty, malformed, and non-finite values are rejected before preparing files or starting any process with `safety_controller_pt1000_invalid_settings` and a concise `message`; see the [invalid-settings response](examples/safety.write_settings.response.pt1000_invalid_settings.json). The GUI reports the first invalid enabled input before sending a request. Explicit zero is valid. Disabled channels may submit empty values without blocking other settings; see the [write request](examples/safety.write_settings.request.json) for retained and empty disabled-channel values.

`safety.read_settings` accepts the nested setting fields to read and returns them per controller in `parameters.controllers`. Each controller identifies its `device_name`, `bsp_instance`, and `driver_module`; `available` indicates whether cached settings could be read, and `writable` indicates both that EVerest JSON-RPC is available and the owning EVSE index can be resolved safely. With multiple `EvseManager` instances, this requires `mapping.module.evse` in the effective config. A controller can be unavailable or read-only while the overall read succeeds. `rpc_available` reports the shared RPC state, and `resolution_error` is present when configuration parsing could not resolve all controllers.

`safety.write_settings` selects exactly one controller with `device_name` and sends its setting blocks. A successful write is a final `.result` response containing the selected `device_name`. The backend sends non-final `.ack` responses during a long write to keep the browser request alive; clients must wait until `final: true`. A Safety write received while the system-control path is occupied is rejected with `safety_controller_busy` rather than queued. A Safety read during an active Safety write returns the same busy error and can be retried. Writing is allowed only when every EVSE associated with that physical controller is in a configured allowed state; missing or unreadable state blocks the write. The backend then stops EVerest, flashes through `ra-update`, restarts it, waits for a fresh JSON-RPC Hello handshake and usable status from every associated EVSE within one shared startup deadline, and then monitors errors on those same EVSEs. Updated YAML is published to the controller's `/run/ra-utils/<device>.yaml` cache after a successful flash. Flash process details stay in the backend journal. A `safety_controller_flash_failed` response includes the minor `flash_outcome` and `exit_code` fields. An error does not always mean the flash failed: for example, `rpc_api_not_connected` can be returned after flashing succeeded, and a cache-publication error explicitly carries `flash_succeeded: true`. Writes for an unresolved multi-EVSE mapping return `safety_controller_evse_mapping_unavailable`.

The cached YAML is the authoritative full-image write baseline. Only submitted, permitted entries are changed; other sections and nested fields are preserved semantically, including parameters not exposed by the GUI. Comments and formatting need not be retained. The external producer must atomically publish a dump for the correct physical controller and refresh it after external changes. External writers must coordinate exclusive access with UI flashing/recovery. The backend does not establish freshness from file age, controller identity, or a generation token; no live pre-flash dump is performed.

Immediately before flashing, the cache is atomically renamed to `<device>.yaml.invalid`; invalidation failure aborts flashing with `safety_controller_cache_invalidation_failed`. Verified flash success publishes the full updated document **before** restarting EVerest. Publication failure returns `safety_controller_yaml_publish_failed_after_flash`, `flash_succeeded: true`, and `cache_state: "unavailable"`.

After a started flash times out, crashes, or exits unsuccessfully, the backend attempts one device-specific `ra-update ... dump-pb <temporary.bin>` followed by `ra-pb-dump <temporary.bin>`, before restarting EVerest. Each command uses the existing process start/run limits (2/30 seconds); the browser continues receiving keepalive acknowledgements. Successful commands and structurally valid YAML are required; YAML output from a CRC-failed conversion is rejected. Recovery does not retry flashing or turn a failed flash into success.

Flash-failure responses include `cache_state`: `unchanged` means the flash definitively failed to start and the original cache was restored; `refreshed` means validated controller readback was published; `unavailable` means no trustworthy cache could be published. Optional `recovery_error` and `restart_error` provide secondary status without replacing the original flash failure. Process details remain in the journal. The GUI disables the selected editor for `refreshed` (reload before editing) and `unavailable` (a successful external refresh is required). Other controllers are unaffected. An interrupted backend operation leaves the invalid backup unused; it is never automatically restored. A producer can restore availability by atomically publishing a fresh canonical YAML file. Examples: [refreshed](examples/safety.write_settings.response.flash_failed.json), [unchanged](examples/safety.write_settings.response.cache_unchanged.json), [unavailable](examples/safety.write_settings.response.cache_unavailable.json), [invalidation failure](examples/safety.write_settings.response.cache_invalidation_failed.json), and [unavailable read](examples/safety.read_settings.response.cache_unavailable.json).

If an associated EVSE reports `error_present: true` during post-restart monitoring, the write returns a final `.error` response even though flashing succeeded. Both `parameters.error` and `parameters.message` contain: "Safety Controller settings were applied successfully. EVerest reported an error state after restarting. Check the EVerest error logs." The GUI displays `parameters.message` directly, without an "Unable to apply" prefix. This diagnostic does not establish that the changed settings caused the error, and does not roll back the flashed settings. See the [post-restart error example](examples/safety.write_settings.response.post_restart_error.json).

Safety Controller discovery and `everest.read_config_parameters` read the effective EVerest configuration: the configured base `config.yaml`, merged with the corresponding `user-config/<config filename>` overlay when present. The overlay uses YAML Merge Patch behavior, including `null` removing a key. The separate `download_config` action continues to return the configured base file.

## Important payloads

### Network settings

`network.read_settings` reports the selected main file in `network_file`, as reported by `networkctl status`. It reads that file and applicable systemd `.network.d/*.conf` drop-ins to populate the editor. Kinds `ether` and `bridge` expose IPv4 configuration; `can` exposes only CAN bitrate; loopback and other kinds remain masked. CAN reads return `can_bitrate` as integer bits per second or `null`, with `can_bitrate_source` set to `networkd`, `kernel`, or `unknown`, and `can_bitrate_override` indicating a bitrate in the UI-owned drop-in. The systemd setting takes precedence; if absent, the backend queries Linux link details with `ip -details -json`.

For Ethernet and bridge, the IPv4 mode is represented by `dhcp_ipv4`; static mode sends `ipv4_address` as a bare IPv4 address and `ipv4_prefix_length` as an integer from 0 through 32. DHCP mode sends an empty address and no static gateway or DNS values. Read and write payloads do not expose a fallback address; the backend preserves any existing fallback address when an overlay resets the configured address list. CAN writes use `can_bitrate` as an integer from 1 through 4294967295 and only modify `[CAN] BitRate=`. CAN reset removes only that directive, retaining unrelated UI drop-in settings. `network.write_settings` writes only settings that differ from the parsed underlay to `overlay_file`, using `/etc/systemd/network/<selected-network-filename>.d/50-everest-ui.conf` (for example, `10-wired.network.d/50-everest-ui.conf`); it never copies or rewrites the selected main file. `network_file` in the write result continues to identify the selected source file. `user_override` is true only when the overlay has the EVerest UI ownership marker. If status reports no selected network file, settings are not editable. All network actions accept optional `expert_mode`; pass true consistently when expert discovery is enabled.

The CAN editor offers the eight CANopen CC standard bitrates (10, 20, 50, 125, 250, 500, 800, and 1000 kbit/s) plus Custom. Custom input accepts an unsigned integer optionally suffixed by `k` or `M`, using decimal multipliers of 1000 and 1000000; all write requests carry the normalized integer in bits per second. A CAN interface without a selected network file is read-only because there is no reliable selected-file drop-in target for Save or Reset.

Saving currently replaces an existing unmarked `50-everest-ui.conf` at the target path; IPv4 writes reconstruct the overlay from settings managed by the editor, so unrelated directives in that file may be lost. The successful write response marks the resulting file as UI-owned (`user_override: true`). This differs from Reset, which refuses to manage an unmarked overlay and returns `network_config_unowned_dropin`; reset does not remove a main `/etc/*.network` file. If a lower-priority directory has the same drop-in filename but `/etc` does not, the write is rejected rather than shadowing the lower-priority file. When replacing list-valued settings, systemd empty assignments clear prior values before the requested values are added. Edits that cannot preserve unsupported address or route configuration are rejected as `unsupported_network_configuration`.

At startup and before handling each network action, the backend recovers interrupted reset backups. An uncommitted overlay backup is restored over a staged replacement only when that replacement has the EVerest UI ownership marker; otherwise recovery stops without overwriting it. Restored files are kept alongside their backups until `networkctl reload` succeeds, allowing recovery to retry after a failed reload. While recovery is unresolved, network actions return `network_config_recovery_failed`. Recovery is per file rather than journaled as one batch: if a crash occurs during promotion of multiple reset backups, entries already promoted may remain committed while unpromoted entries are restored. This can leave a mixed reset outcome that the operator should inspect.

### SSH configuration

All four SSH actions use the same authenticated `/ws` WebSocket connection and request/response envelopes as the other groups. `ssh.read`, `ssh.enable`, and `ssh.disable` accept an empty `parameters` object. Enable and disable return final `.ack` responses with empty parameters on success; the GUI then issues `ssh.read` to refresh status.

The backend configuration key `ssh_control_method` selects `ssh-autostart` (the default, also used when the key is missing or empty) or `systemd`. It is read for each status/enable/disable request and is not a request parameter. Unknown values return `ssh_control_method_invalid`. There is no automatic fallback when the selected method fails.

With `ssh-autostart`, the backend executes `/usr/sbin/ssh-autostart` directly with `--status`, `--enable`, or `--disable`. Enable/disable require a normal exit with code 0. Status accepts trimmed stdout `enabled` with exit code 0, or `disabled` with exit code 1; the latter is a successful status read, not an operation failure. Unexpected output or mismatched exit codes fail. Commands have a 3-second startup deadline and a 30-second execution deadline; after a timeout, the process is killed and waited for up to 1 second. Launch failures, timeouts, crashes, and invalid results return `ssh_autostart_failed`. Detailed failed-command diagnostics stay in the backend journal. Service handling and effects on existing SSH sessions are delegated entirely to the helper.

A successful `ssh.disable.ack` in helper mode confirms that `ssh-autostart --disable` exited successfully. The backend does not additionally stop `sshd.socket` or `sshd.service`, terminate session processes, or verify that established connections have closed. The subsequent `ssh.read` reports the helper's enablement state, not SSH reachability or a count of open sessions. In particular, `enabled: false` with `active: null` must not be interpreted as confirmation that all existing SSH sessions have ended. The helper and systemd methods intentionally retain their different session-handling behavior.

With `systemd`, enable prefers `sshd.socket`, falling back to `sshd.service` if the socket is unavailable. It enables the selected unit, reloads systemd, starts it, and waits up to 5 seconds for activation; reload/start/activation failure triggers the existing enablement rollback. Disable stops all available SSH units, waits up to 5 seconds per unit for inactivity, disables their unit files, reloads systemd, and then sends SIGTERM to processes matching `^sshd(-session)?: ` through `pkill`. Enable/disable failures return `ssh_systemd_failed`.

`ssh.read.result` contains `enabled` and `active`. In helper mode, `enabled` is the reported boolean and `active` is `null`, because runtime activity is not reported. In systemd mode, both fields are booleans: `active` is true if either available SSH unit is active; `enabled` is true if either has unit-file state `enabled` or `enabled-runtime`. Runtime enablement does not persist across reboot. The GUI displays On if either field is exactly true. These fields replace the previous `socket_enabled` and `socket_active` names.

`ssh.set_password` accepts `{"password":"example-password"}` and changes the Linux **root account password**, independently of the selected SSH control method. It is separate from the Web UI login password. Passwords must be non-empty, at most 256 QString code units, and contain no LF, CR, or NUL; invalid values return `invalid_params`. The backend runs `chpasswd`, writes `root:<password>\n` to stdin, and closes stdin. It allows 3 seconds for startup and 10 seconds for completion, with a 1-second wait after killing a timed-out process. Failure returns `ssh_password_failed`; success returns a final `.ack` with empty parameters. Passwords are not placed in command-line arguments. When enabling SSH with filled password fields, the GUI first sends `set_password` and sends `enable` only after its successful acknowledgement.

The target must provide the selected control method's dependencies (`/usr/sbin/ssh-autostart`, or systemd system-bus access and `pkill`) and `chpasswd` for password changes. The backend needs the corresponding operating-system permissions, including permission to change the root password. Web UI authentication does not grant these OS permissions.

### Reboot Recovery

Before enabling reboot or factory reset, read `system.read_reboot_settings` with `{}`. Its final result
contains `wait_timeout_seconds` (a positive integer) and `boot_id` (the current Linux boot UUID, or an
empty string if unavailable). The setting is supplied by the backend, not by static assets or frontend
configuration. See [request](examples/system.read_reboot_settings.request.json) and
[response](examples/system.read_reboot_settings.response.success.json).

`reboot_wait_timeout_seconds` defaults to `30`. Optional `.<compatible>` suffixes, such as
`reboot_wait_timeout_seconds.chargebyte,imx6ull-tarragon-master=120`, use the NUL-delimited device-tree
compatible list in order: first valid matching entry, then base setting, then the default. Invalid,
empty, non-positive, or values above `2147483` seconds are skipped. The browser caches this policy before
issuing the operation so it remains available while the backend is down or reset removes local files.
Settings reads are available even while factory reset blocks other system-control operations.

Send an explicit reboot request once and do not wait for an acknowledgement or final response before
entering recovery. Any acknowledgement means scheduled, not completed, and may never arrive. The
existing `firmware.reboot` endpoint and its preconditions remain available. The browser releases its
WebSocket after sending reboot; a rejection is not guaranteed to reach it. Never resend a reboot command
because of disconnection, a missing acknowledgement, or a browser timeout.

The shared browser overlay blocks interaction during recovery. It probes `/auth/status` with
`cache: no-store` and same-origin credentials, allowing three seconds per request and waiting two seconds
between probes. Only a different nonempty `bootId`, `authenticated:false`, and boolean `setupRequired`
confirm that the device has returned to an authentication-ready state. A successful static-page fetch,
HTTP 200 alone, or an unchanged boot ID cannot confirm reboot. Missing boot identity leaves manual
reload as the fallback. A proxy must forward the existing `/auth/*` and `/ws` endpoints to the device;
it must not cache authentication status or substitute its own machine's boot identity.

The WebSocket is closed when entering reboot waiting, after sending explicit reboot or receiving and
acknowledging factory-reset success, releasing exclusive UI occupancy. It stays open during reset
execution for progress and errors. `/auth/status` is informational: `uiBusy` does not mean that the HTTP
request was rejected, and polling does not produce blocked-access logs. Actual WebSocket occupancy
restrictions remain enforced.

The configured wait period starts when reboot is requested, or when factory reset reports success or
disconnects. Expiry does not cancel the operation or stop probing: it changes the message to indicate
that reboot is unconfirmed. Manual Reload is available immediately while waiting, but not during reset
execution. Pending operation kind, baseline boot ID, policy, and wait start are kept in session
storage, so an ordinary refresh resumes recovery without replaying any command. Explicit manual
Reload clears that context, stops probes and reconnection, and dismisses an existing factory-reset
reboot expectation for this boot in the same browser tab. Page navigation or repeated status replies
cannot re-open the overlay. A different boot identity or new explicit maintenance operation clears
dismissal; if identity is unavailable, dismissal lasts for the tab session. Reload never cancels or
retries a command and does not clear the backend maintenance lock. If no reboot occurred, the existing
authentication session can still be valid, so manual Reload need not prompt for login.
Automatic reload keeps recovery context until the next authentication status succeeds;
temporary startup failures resume recovery. After reboot, sessions are not restored: normal login is
required, or initial setup if factory reset erased the credentials.

### Factory Reset

Factory reset is a separate maintenance operation, initially hosted on the Firmware Update page.
It uses the normal authenticated WebSocket and response ACK protocol, not an HTTP endpoint.
The pane can be mounted independently and uses the shared reboot overlay; it has no Reboot button.

| Action | Request parameters | Responses |
| --- | --- | --- |
| `read_status` | `{}` | Final result with `state`, `can_reset`, and `reboot_expected`. Allowed during reset. |
| `reset` | `{"confirm":true}` | Initial and two-second non-final `.ack` messages; a final `.result`/`.error` if the API survives command completion. |

`state` is `idle`, `running`, `rebooting`, or `failed`. `running` includes pre-execution safety checks.
`can_reset` is boolean eligibility at the time of reading, not a reservation: execution rechecks it.
`reboot_expected` becomes true only when the reset command exits successfully. The `rebooting` state
keeps maintenance blocked until reboot; it does not prove the device has rebooted. Failure never enables
a separate reboot action. The former `factory_reset.reboot` action and `reboot_available` field are removed.
The backend rejects requests that would wait behind another operation with `factory_reset_busy` rather
than queueing reset. During reset/reboot, other system-control requests are rejected with the same error;
reset status and reboot-settings reads remain available. Firmware uploads also exclude reset between requests.

A normal command exit with code `0` produces `factory_reset.reset.result` with
`{"reboot_expected":true}` if that response can be delivered. Every command-attempt failure produces
`factory_reset.reset.error` with `{"error":"factory_reset_failed"}` if the API is still available. Output, exit codes, and failure causes
are not transmitted; consult the backend journal. There is no rollback of partial changes.

| Error | Meaning |
| --- | --- |
| `factory_reset_confirmation_required` | `confirm` is absent or not boolean `true`; no execution. |
| `factory_reset_busy` | A conflicting maintenance request is active or reboot is pending; no new reset execution. |
| `factory_reset_not_allowed` | Pre-execution safety policy failed; no command attempt. |
| `factory_reset_failed` | Command attempt completed unsuccessfully; the device may not reboot automatically. |
| `unsupported_action` | Unknown action in the factory-reset group. |

The guard defaults to requiring all configured `EvseManager` instances to resolve to EVSE indices and
report exactly `Unplugged`. A single instance with no mapping falls back to index `1`; an incomplete
multi-EVSE mapping, absent RPC/configuration, or unreadable state fails closed. The guard is independent
of `everest_restart_allowed_states`. With `factory_reset_require_unplugged=false`, the backend skips
EVSE/config discovery and RPC entirely; this explicitly permits resetting during charging.

`factory_reset_command` defaults to `/usr/sbin/factory-reset --force --reboot` and is executed without an implicit
shell. `factory_reset_timeout_seconds` defaults to `20`, measured from successful startup; startup has
a separate two-second budget. Timeout sends termination, escalating to kill after one second, and the
backend waits for process exit before the final response. The tool is responsible for its descendants.
The tool reboots automatically on success and may terminate the API before any final response. Reset
itself does not stop EVerest separately. Explicit Controller Firmware reboot still uses
`firmware_reboot_command` and its `reboot` fallback; the reset tool owns its automatic reboot.
See [configuration and deployment caveats](../../README.md#factory-reset).

Keepalives are not completion. During command execution, a client may receive a final failure and show
the generic error without promising reboot. Once a confirmed reset disconnects or reports success,
enter reboot recovery without requiring a final response. The operation continues without its browser;
backend state lasts only for that API process and cannot prove a new boot. A disconnect alone does not
prove that reset succeeded. Closing/cancelling the initial confirmation makes no API call. Status is
polled every five seconds while the pane is mounted and connected; destructive commands are never replayed.
If a reported command failure is immediately followed by disconnection within the configured recovery
window, the browser can still enter reboot recovery: shutdown may have caused the reported process failure.

Examples: [status request](examples/factory_reset.read_status.request.json),
[idle](examples/factory_reset.read_status.response.idle.json),
[running](examples/factory_reset.read_status.response.running.json),
[rebooting](examples/factory_reset.read_status.response.rebooting.json),
[failed](examples/factory_reset.read_status.response.failed.json),
[confirmed reset](examples/factory_reset.reset.request.json),
[keepalive](examples/factory_reset.reset.response.ack.json),
[success](examples/factory_reset.reset.response.success.json),
[command failure](examples/factory_reset.reset.response.error.json),
[missing confirmation](examples/factory_reset.reset.response.confirmation_required.json),
[safety rejection](examples/factory_reset.reset.response.not_allowed.json),
[busy rejection](examples/factory_reset.reset.response.busy.json).

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
| `safety.read_settings` | [request](examples/safety.read_settings.request.json) | [response](examples/safety.read_settings.response.success.json), [unavailable cache](examples/safety.read_settings.response.cache_unavailable.json) | [response](examples/safety.read_settings.response.error.json) |
| `safety.write_settings` | [request](examples/safety.write_settings.request.json) | [progress](examples/safety.write_settings.response.progress.json), [success](examples/safety.write_settings.response.success.json) | [generic error](examples/safety.write_settings.response.error.json), [invalid PT1000 settings](examples/safety.write_settings.response.pt1000_invalid_settings.json), [busy](examples/safety.write_settings.response.busy.json), [flash failure with readback](examples/safety.write_settings.response.flash_failed.json), [unchanged cache](examples/safety.write_settings.response.cache_unchanged.json), [unavailable cache](examples/safety.write_settings.response.cache_unavailable.json), [invalidation failure](examples/safety.write_settings.response.cache_invalidation_failed.json), [cache publication after flash](examples/safety.write_settings.response.cache_publish_failed.json), [post-restart error](examples/safety.write_settings.response.post_restart_error.json) |
| `ocpp.read_settings` | [request](examples/ocpp.read_settings.request.json) | [response](examples/ocpp.read_settings.response.success.json) | [response](examples/ocpp.read_settings.response.error.json) |
| `ocpp.write_settings` | [request](examples/ocpp.write_settings.request.json) | [response](examples/ocpp.write_settings.response.success.json) | [response](examples/ocpp.write_settings.response.error.json) |
| `firmware.read_version` | [request](examples/firmware.read_version.request.json) | [response](examples/firmware.read_version.response.success.json) | [response](examples/firmware.read_version.response.error.json) |
| `firmware.upload_image.start` | [request](examples/firmware.upload_image.start.request.json) | [response](examples/firmware.upload_image.start.response.success.json) | [response](examples/firmware.upload_image.start.response.error.json) |
| `firmware.upload_image.chunk` | [request](examples/firmware.upload_image.chunk.request.json) | [response](examples/firmware.upload_image.chunk.response.success.json) | [response](examples/firmware.upload_image.chunk.response.error.json) |
| `firmware.upload_image.finish` | [request](examples/firmware.upload_image.finish.request.json) | [response](examples/firmware.upload_image.finish.response.success.json) | [response](examples/firmware.upload_image.finish.response.error.json) |
| `firmware.update_image` | [request](examples/firmware.update_image.request.json) | [final response](examples/firmware.update_image.response.success.json), [progress](examples/firmware.update_image.response.progress.json) | [response](examples/firmware.update_image.response.error.json) |
| `firmware.reboot` | [request](examples/firmware.reboot.request.json) | [response](examples/firmware.reboot.response.success.json) | [response](examples/firmware.reboot.response.error.json) |
| `factory_reset.read_status` | [request](examples/factory_reset.read_status.request.json) | [idle](examples/factory_reset.read_status.response.idle.json), [running](examples/factory_reset.read_status.response.running.json), [rebooting](examples/factory_reset.read_status.response.rebooting.json), [failed](examples/factory_reset.read_status.response.failed.json) | See [Factory Reset](#factory-reset). |
| `factory_reset.reset` | [request](examples/factory_reset.reset.request.json) | [acknowledgement](examples/factory_reset.reset.response.ack.json), [result](examples/factory_reset.reset.response.success.json) | [command failure](examples/factory_reset.reset.response.error.json), [confirmation](examples/factory_reset.reset.response.confirmation_required.json), [safety policy](examples/factory_reset.reset.response.not_allowed.json), [busy](examples/factory_reset.reset.response.busy.json) |
| `system_logs.read` | [request](examples/system_logs.read.request.json) | [response](examples/system_logs.read.response.success.json) | [response](examples/system_logs.read.response.error.json) |
| `system_logs.download` | [request](examples/system_logs.download.request.json) | [response](examples/system_logs.download.response.success.json) | [response](examples/system_logs.download.response.error.json) |
| `system_logs.extract` | [request](examples/system_logs.extract.request.json) | [response](examples/system_logs.extract.response.success.json) | [response](examples/system_logs.extract.response.error.json) |
| `network.read_interfaces` | [request](examples/network.read_interfaces.request.json) | [response](examples/network.read_interfaces.response.success.json) | [response](examples/network.read_interfaces.response.error.json) |
| `network.read_settings` | [request](examples/network.read_settings.request.json) | [response](examples/network.read_settings.response.success.json) | [response](examples/network.read_settings.response.error.json) |
| `network.write_settings` | [request](examples/network.write_settings.request.json) | [response](examples/network.write_settings.response.success.json) | [response](examples/network.write_settings.response.error.json) |
| `network.read_settings` (CAN) | [request](examples/network.read_settings.can.request.json) | [response](examples/network.read_settings.can.response.success.json) | [response](examples/network.read_settings.response.error.json) |
| `network.write_settings` (CAN) | [request](examples/network.write_settings.can.request.json) | [response](examples/network.write_settings.can.response.success.json) | [response](examples/network.write_settings.response.error.json) |
| `network.reset_settings` | [request](examples/network.reset_settings.request.json) | [response](examples/network.reset_settings.response.success.json) | [response](examples/network.reset_settings.response.error.json) |
| `network.cancel_reset_settings` | [request](examples/network.cancel_reset_settings.request.json) | [response](examples/network.cancel_reset_settings.response.success.json) | [response](examples/network.cancel_reset_settings.response.error.json) |
| `network.apply` | [request](examples/network.apply.request.json) | [response](examples/network.apply.response.success.json) | [response](examples/network.apply.response.error.json) |
| `system.read_app_title` | [request](examples/system.read_app_title.request.json) | [response](examples/system.read_app_title.response.success.json) | [response](examples/system.read_app_title.response.error.json) |
| `system.read_reboot_settings` | [request](examples/system.read_reboot_settings.request.json) | [response](examples/system.read_reboot_settings.response.success.json) | Settings fall back to defaults; boot ID may be empty. |
| `ssh.read` | [request](examples/ssh.read.request.json) | [helper enabled](examples/ssh.read.response.success.json), [helper disabled](examples/ssh.read.response.disabled.json) | [helper failure](examples/ssh.read.response.error.json), [invalid method](examples/ssh.read.response.config_invalid.json) |
| `ssh.enable` | [request](examples/ssh.enable.request.json) | [acknowledgement](examples/ssh.enable.response.success.json) | [helper failure](examples/ssh.enable.response.error.json), [systemd failure](examples/ssh.enable.response.systemd_failed.json), [invalid method](examples/ssh.enable.response.config_invalid.json) |
| `ssh.disable` | [request](examples/ssh.disable.request.json) | [acknowledgement](examples/ssh.disable.response.success.json) | [helper failure](examples/ssh.disable.response.error.json), [systemd failure](examples/ssh.disable.response.systemd_failed.json), [invalid method](examples/ssh.disable.response.config_invalid.json) |
| `ssh.set_password` | [request](examples/ssh.set_password.request.json) | [acknowledgement](examples/ssh.set_password.response.success.json) | [password failure](examples/ssh.set_password.response.error.json), [invalid password](examples/ssh.set_password.response.invalid_params.json) |

Systemd status examples cover [active and enabled](examples/ssh.read.response.systemd.active_enabled.json), [active only](examples/ssh.read.response.systemd.active_only.json), [enabled only](examples/ssh.read.response.systemd.enabled_only.json), and [neither](examples/ssh.read.response.systemd.disabled.json). A status read never returns a password.

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

Some UI actions use a separate internal backend-to-EVerest WebSocket RPC connection. The backend reads the effective EVerest YAML, locates the configured `rpc_api` module, derives its URL, and performs an EVerest hello handshake. This is not the browser API; see `backend/api/RpcApiClient.cpp`. After systemd accepts an EVerest restart, the backend waits for a successful Hello handshake newer than the pre-restart connection. Safety Controller writes additionally require valid `EVSE.GetStatus` responses with a boolean `error_present` from every associated EVSE, on the same fresh connection. Hello and status readiness share one monotonic deadline measured from the accepted restart; the budget is not restarted per phase, retry, or EVSE. Other restart callers retain the Hello-only readiness check.

The wait defaults to **20 seconds** and is configurable with `everest_rpc_api_ready_timeout_seconds` in `backend/api/config/backend.conf`. Platform-specific values use `everest_rpc_api_ready_timeout_seconds.<compatible>`; compatible strings are taken from the NUL-delimited `/proc/device-tree/compatible`, and the first matching platform value takes precedence over the base value. Invalid or non-positive values are skipped; if no valid configured value remains, the 20-second default is used. Existing explicitly configured values, including 15 seconds, remain authoritative and must be changed to 20 on deployed systems when desired.

During Safety Controller readiness and the following separate 10-second error observation, `rpc_api_not_connected`, `rpc_api_request_pending`, and `rpc_api_no_response` are retried within the remaining phase budget. Each status read normally allows 2 seconds per attempt and one retry, but both attempts together are capped by the remaining overall deadline. Exhausted requests abort the connection and allow reconnection; stale replies and nested status reads cannot complete another request. Explicit RPC errors and invalid status payloads remain failures. An error observed during readiness is retained for the existing post-flash diagnostic. Unresolved monitoring failures are tracked per EVSE, so a successful query for another EVSE cannot hide them.

Diagnostics stay in the backend journal. Warnings, failures, flash outcomes, and final Safety Controller operation summaries are always logged. Verbose connection, request, retry, readiness, and phase tracing is compiled out by default; enable it with `cmake -S . -B build -DEVEREST_UI_RPC_DEBUG=ON` and rebuild. This option is independent of Debug/Release build type and also applies to RPC-related test targets. Verbose traces include EVSE/RPC request IDs, remaining time, and handshake/disconnection generations; restart-readiness failures retain the selected timeout and configuration source even with tracing disabled. Set the option to `OFF` and rebuild to disable tracing again.

A pre-flash safety-query failure still blocks flashing immediately; the startup deadline applies after an accepted restart, not from the Save click. No new browser API fields or response types are introduced.

## Current limitations

- There is no negotiated protocol version, formal schema, or capability-discovery message.
- Error strings are implementation values; the list in `ProtocolSchema.hpp` is not exhaustive.
- Several payloads are open-ended because they represent EVerest YAML or controller configuration.
- The browser has a 50-second pending-request timeout, refreshed by non-final responses; backend response delivery uses a two-second acknowledgement timeout.
- Requests are serialized by the backend control path; Safety writes are rejected as busy instead of being queued behind another operation. Only one UI WebSocket session is accepted.

Source of truth: `backend/api/ProtocolSchema.hpp`, `backend/api/RequestHandler.cpp`, `backend/api/libs/ResponseBuilder.cpp`, `backend/api/modules/*.cpp`, `backend/webserver/http/StaticServer.cpp`, `backend/webserver/ws/WebSocketProxySession.cpp`, and `public/js/transport.js`.
