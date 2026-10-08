# EVerest-UI

## Prerequisites

EVerest must be configured with the `RpcApi` module. The UI backend connects to EVerest through that module.

The project also requires:

- CMake 3.20 or newer
- A C++17 compiler
- Qt 5 or Qt 6 with the components `Core`, `Network`, `WebSockets`, and `DBus`
- `yaml-cpp`

## Build and Install

The project installs three binaries:

- `everest-ui-api`
- `everest-ui-webserver`
- `everest-ui`

It installs the runtime configuration files into `/etc/everest-ui/`, the browser assets into `/usr/share/everest-ui/public/`, and the auth store is created at `/var/lib/everest-ui/auth.json`.

Example build and install flow:

```bash
mkdir -p build
cd build
cmake .. -DEVEREST_UI_QT_VERSION=6
make
make install
```

If you want to use Qt 5 instead, configure with:

```bash
cmake .. -DEVEREST_UI_QT_VERSION=5
```

If `CMAKE_INSTALL_PREFIX` is not set explicitly, the default install path is:

```text
/usr/local
```

For a cross-built target image, set the target prefix explicitly and use
`DESTDIR` as the staging root. For example:

```bash
cmake -S . -B build-cross \
  -DCMAKE_TOOLCHAIN_FILE=/tooling/EVerest_toolchain/toolchain.cmake \
  -DCMAKE_SYSROOT=/sysroot \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DOE_QMAKE_PATH_EXTERNAL_HOST_BINS:STRING=/usr/bin \
  -DEVEREST_UI_BUILD_TESTS=ON
cmake --build build-cross -j4
DESTDIR="$PWD/build-cross/install" cmake --install build-cross
```

This produces a complete target filesystem tree below
`build-cross/install`, including `usr/bin`, `usr/libexec`, and
`usr/share`, as well as the absolute system locations `etc` and `lib`.
`CMAKE_INSTALL_PREFIX` is cached, so an existing build directory must be
reconfigured when changing it.

## Configure the UI

### UI configuration files

Distribution defaults are installed under `${prefix}/share/everest-ui/config/`
(normally `/usr/share/everest-ui/config/` for a distribution build):

- `backend.conf`
- `frontend.conf`

The launcher, API, and webserver merge these defaults with local, key-by-key overrides:

- `/etc/everest-ui/backend.conf`
- `/etc/everest-ui/frontend.conf`

Missing local files are allowed. New installations create comment-only overlays; upgrades and repeated
`cmake --install` calls preserve existing local files. Existing complete `/etc` configurations remain valid,
but their explicit entries keep overriding future distribution defaults until removed.

Use `key=value` to override, `key=` to set an empty value, and a standalone `!key` to remove an inherited
key. Removing the local directive restores inheritance. Lists are replaced per key, not concatenated.
See [Configuration Layers](docs/configuration.md) for syntax, migration, save behavior, and the distinction
between clearing a value and disabling functionality. All shipped setting values remain unchanged.

The browser assets are installed to `${prefix}/share/everest-ui/public/`.

The authentication database is created on first setup at:

- `/var/lib/everest-ui/auth.json`

### EVerest configuration paths

The EVerest paths are configured in [`backend.conf`](./backend/api/config/backend.conf).

The `everest_rpc_api_ready_timeout_seconds` backend setting controls how long the UI backend waits for the EVerest
JSON-RPC API to become ready after restarting EVerest. It defaults to 20 seconds. For Safety Controller writes,
one shared budget covers a fresh Hello handshake and valid status responses from every associated EVSE,
measured from the accepted restart. A separate 10-second error observation follows; errors already reported
during readiness are retained. Temporary disconnects and missing status responses are retried within the
remaining budget. Optional platform overrides use the exact `/proc/device-tree/compatible` value as a suffix;
the first matching compatible entry takes precedence. Explicitly configured values remain authoritative:
update an installed `everest_rpc_api_ready_timeout_seconds=15` to `20` to use the new default duration.

Safety Controller settings are read from `/run/ra-utils/<device>.yaml`. Saves preserve the complete YAML
document, including settings not exposed by the UI. The cache is the write baseline, not a live read:
its producer must publish atomically for the correct controller and refresh it after external changes.
External tools must not access that controller concurrently with a UI flash or recovery.

PT1000 channels appear as rows under **Overtemperature Protection**. Disabling a channel greys out its
temperature and resistance-offset inputs without clearing their values; Save still sends those values.
They are retained only while the page is open, not across reloads: a disabled hardware channel has no
recoverable temperature threshold, so a fresh read leaves unknown values empty. Both values must be entered
as finite numbers before an enabled channel can be saved; explicitly entered zero is valid. Disabled channels
may remain empty without blocking other settings. Emergency Stops have one dropdown
per row, offering `active-low`, `active-high`, or `disabled`. Contactors retain `disabled` and all three
feedback modes. The configured settings allowlist still determines which groups are shown.

Before flashing, the backend quarantines the cache as `<device>.yaml.invalid`. A verified successful flash
publishes the updated YAML before EVerest restarts. If a started flash fails, the backend attempts one bounded
device-specific parameter dump and YAML conversion while EVerest is still stopped. Valid readback replaces
the cache; failed readback leaves settings unavailable and editing disabled until a producer publishes a
fresh cache. A definite failure to start restores the unchanged cache. Neither a backend restart nor a
failed recovery restores uncertain cached values automatically. Recovery never retries the flash.
The flash still reports failure even when readback succeeds; reload the page before further editing.

Safety Controller operation summaries and warnings remain available in the backend journal. Verbose RPC
connection, request, retry, readiness, and Safety Controller phase tracing is compiled out by default.
Enable it explicitly with the CMake option below (independent of Debug/Release build type):

```bash
cmake -S . -B build -DEVEREST_UI_RPC_DEBUG=ON
cmake --build build --parallel 4
```

Use `-DEVEREST_UI_RPC_DEBUG=OFF` and rebuild to disable tracing again. The option also applies to the
RPC-related test targets. Verbose messages include EVSE/request IDs, remaining budget, and connection
generations. To collect one complete Save cycle, run:

```bash
journalctl -b -u everest-ui.service -u everest.service -o short-monotonic
```

PCAP captures are bounded by these backend settings:

- `pcap_max_size_bytes`
  Maximum capture file size. The default is 100 MiB.

- `pcap_max_duration_seconds`
  Maximum capture duration. The default is 15 minutes.

When a limit is reached, the partial capture remains available for download.
The size limit is checked periodically and can be exceeded slightly while tcpdump is being stopped.

The following two configuration entries define the paths of the EVerest configuration files:

- `everest_config_path`
  Points to the configuration file that EVerest uses.

- `everest_base_config_path`
  Points to the location where the base configuration created by the UI is saved. This base configuration is then used by EVerest when it is restarted by the UI.

The UI overlay path is derived using the same rule as everest-core: resolve the canonical target of
`everest_config_path`, then use `<target directory>/user-config/<target filename>`. For example,
`/etc/everest/config.yaml` linked to `/etc/everest/everest-ui-config.yaml` uses
`/etc/everest/user-config/everest-ui-config.yaml`.

The optional `available_features` entry in `backend.conf` is a comma-separated list of enabled UI features.
If it is omitted, all features are enabled. The Network Configuration page is enabled when `network` is
listed; for example, `available_features=network`. Other feature names are reserved for future use.

## SSH Configuration

The SSH Configuration page enables or disables SSH and changes the Linux **root account password**.
This password is separate from the Web UI login password. When enabling SSH with filled password fields,
the page changes the password first and enables SSH only after that operation succeeds. SSH can also be
enabled without changing the password.

Select the SSH control method in `/etc/everest-ui/backend.conf`:

```ini
ssh_control_method=ssh-autostart
```

`ssh-autostart` is the default, including when the setting is missing or empty. It requires
`/usr/sbin/ssh-autostart`, which the backend calls with `--status`, `--enable`, or `--disable`.
The helper determines what happens to listening sockets and existing SSH sessions. The UI does not run
additional service-stop or session-termination commands in this mode, so a successful disable operation
does not independently confirm that existing SSH connections were closed.

Set `ssh_control_method=systemd` to use the alternative systemd method. Enable prefers `sshd.socket`,
falling back to `sshd.service` if the socket unit is unavailable. Disable stops and disables the available
SSH units and explicitly terminates SSH session processes through `pkill`. There is no automatic fallback
between control methods when an operation fails.

The API reports `enabled` (enablement) and `active` (current runtime activity). In helper mode, `active` is
`null` because the helper reports only enablement. In systemd mode, these are separate booleans: SSH can be
active without being enabled for automatic startup, or enabled while currently stopped. The page shows On
when either value is true; this is not a connection test.

Password changes use `chpasswd`, supplying the password through stdin rather than command-line arguments,
and work independently of the configured SSH control method. The target must provide `chpasswd`; systemd
mode also requires system-bus access and `pkill`. The backend must have permission to execute the selected
helper or manage the SSH units and to change the root password, normally by running as root.

See the [SSH API reference](docs/api/API.md#ssh-configuration) for command deadlines, password validation,
errors, and [JSON request/response examples](docs/api/API.md#example-templates).

## Recover forgotten WebUI credentials

After a hard/cold boot, the login page shows **Forgotten password?** and, while
reset is available, a **Reset password** button for the configured window
(60 seconds by default) from the first webserver start. Confirming the reset
removes the existing username and password and ends all authenticated sessions,
including open WebSocket connections. The existing first-time setup screen
then lets you choose a new username and password **without a time limit**, even
after a reload or restart. Successful setup logs you in automatically.

One reset is allowed per boot. Restarting the WebUI service does not reopen or
extend the window. A regular software reboot does not enable reset; the login
page instead asks you to switch the device off and on. An expired or already used
window shows the same instruction. Unknown boot information leaves reset disabled.
The countdown runs locally in the browser without polling; the server checks the
deadline when the reset request arrives. A temporary lock conflict can be retried
manually while the original window remains open. See [`docs/api/API.md`](docs/api/API.md)
for the HTTP endpoints and their request and response formats.

### Configuration and kernel requirements

The authentication server reads these optional settings from `frontend.conf`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `password_reset_window_seconds` | `60` | Window from first webserver start; `0` disables reset. |
| `password_reset_boot_status_path` | `/sys/bus/nvmem/devices/44440000.bbnsm:nvmem0/nvmem` | Writable BBNSM NVMEM device. |
| `allowed_hosts` | empty | Additional trusted hostnames or IP literals, comma-separated; no schemes, ports, or wildcards. |

**The revised kernel must retain the boot marker until the WebUI reads it.**
The original development patch which cleared the marker during driver probe is
not compatible. The server reads a little-endian 32-bit value at byte offset 28:
zero permits reset; `0x5741524d` (WARM) denies it. Unmarked unclean resets, such as
some watchdog resets, also fall into the zero-marker category. After recording
the original classification, the server clears only those four bytes, without
truncating NVMEM. Failed clearing is retried on the next webserver start while
reset remains disabled for that boot.

The systemd unit creates `/run/everest-ui` with mode `0700` and uses
`RuntimeDirectoryPreserve=yes` to preserve it across restart and separate
stop/start operations. The launcher passes `RUNTIME_DIRECTORY` to the webserver;
its actual value determines the directory for `password-reset-state.json` and
`password-reset.lock`, both mode `0600`. A non-blocking `flock` serializes
initialization and reset. Missing or unusable runtime environment disables reset
but leaves ordinary login available. Manual starts do not create a fallback
directory. Tests can supply an existing temporary directory through the variable.
Do not remove the state or lock file during a boot. `/run` is cleared on device
reboot; the stored Linux boot ID is also checked.

### Validation

Build with `-DEVEREST_UI_BUILD_TESTS=ON`, then run `ctest --test-dir <build> --output-on-failure`.
The reset tests use temporary NVMEM/state/auth files and loopback HTTP/WebSocket
connections. Frontend tests run with:

```bash
node --experimental-default-type=module --test tests/password-reset-ui-test.mjs
```

Before firmware delivery, record `systemctl --version` on the target and confirm
that `RUNTIME_DIRECTORY` reaches the webserver. Verify the revised kernel's
marker retention and four-byte write permissions, power-cycle versus regular
reboot, service restart and separate stop/start, and a representative watchdog
reset. These hardware checks are additional to the host-side automated tests.

## Network Configuration

The Network Configuration page reads the selected systemd-networkd file reported by `networkctl status` and
applicable drop-ins. Saving writes changes to `/etc/systemd/network/<selected-network-file-basename>.d/50-everest-ui.conf`;
it does not copy or modify the selected main file. IPv4 settings are editable for `networkctl` kinds `ether`
and `bridge`; `can` interfaces expose CAN bitrate configuration only.

Save and Apply are separate actions. Apply reloads systemd-networkd, which applies changed or removed network
files to all affected interfaces. The IPv4 form offers DHCP or static mode. Static mode edits one primary IPv4
address and prefix, gateway, and DNS; DHCP mode removes those static IPv4 settings. The fallback IPv4 address
is not exposed and is preserved by the backend when it resets address directives. IPv6 DHCP state is retained.
Changing the interface used to access the Web UI can disconnect the browser and may require reconnecting with
the new address.

Reset to factory defaults stages removal of the managed drop-in; it is removed only when Apply succeeds. Apply
uses a same-filesystem backup and restores all staged files if networkd reload fails.
Cancel reset abandons the staged removal. Reset state is retained by the backend across page navigation and reloads,
and it does not apply the change until Apply is pressed.

The API uses `ipv4_address` (bare IPv4 address) and `ipv4_prefix_length` (integer from 0 to 32); it does not expose
the fallback address or the removed mixed DHCP/static flag. Existing unsupported address or route settings are
rejected rather than partially rewritten. See [`docs/api/API.md`](docs/api/API.md) for the wire format and examples.

### UI ports

Two relevant ports can be specified.

`backend_port` in [`backend.conf`](./backend/api/config/backend.conf) is the internal API WebSocket port.
The API binds only to IPv4 loopback (`127.0.0.1`), so external clients must use the authenticated webserver.

In `frontend.conf`, it needs to be ensured that the same backend port is used in the entry `backend_ws`.

The template for the distribution defaults is [`backend/webserver/config/frontend.conf.in`](./backend/webserver/config/frontend.conf.in).
Its installed location is `${prefix}/share/everest-ui/config/frontend.conf`; put local overrides in
`/etc/everest-ui/frontend.conf`.

The entry `port` in `frontend.conf` defines on which port the UI is accessible from the browser.

## Start the UI

The UI is started by running the `everest-ui` binary.

Example:

```bash
${prefix}/bin/everest-ui
```

If you use the default install prefix, that is:

```bash
/usr/local/bin/everest-ui
```

The launcher `everest-ui` starts `everest-ui-api` and `everest-ui-webserver` from
`${prefix}/libexec/everest-ui/`.

If the systemd service was installed and is not started automatically, it can be started with:

```bash
systemctl start everest-ui.service
```

## Access the UI

The UI is accessed via the browser using the IP address of the device together with the effective frontend
`port` (distribution default or `/etc/everest-ui/frontend.conf` override).

Example:

```text
http://<device-ip>:8081/
```
