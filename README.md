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

- `api`
- `webserver`
- `webui`

It installs the runtime configuration files into `/etc/everest-ui/`, the browser assets into `/usr/share/everest-ui/public/`, and the auth store is created at `/var/lib/everest/everest-ui/auth.json`.

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

The UI installs its own configuration files to `/etc/everest-ui/`:

- `/etc/everest-ui/backend.conf`
- `/etc/everest-ui/frontend.conf`

The browser assets are installed to `/usr/share/everest-ui/public/`.

The authentication database is created on first setup at:

- `/var/lib/everest/everest-ui/auth.json`

### EVerest configuration paths

The EVerest paths are configured in [`backend.conf`](./backend/api/config/backend.conf).

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

## Network Configuration

The Network Configuration page reads the effective systemd-networkd file reported by `networkctl status`.
It never modifies distribution files under `/lib/systemd/network/`. Saving creates or updates the corresponding
user configuration at `/etc/systemd/network/<interface>.network`, preserving unrelated settings where possible.

Save and Apply are separate actions. Apply reloads systemd-networkd, which applies changed or removed network
files to all affected interfaces. The page exposes IPv4 DHCP and IPv6 DHCP independently; an explicit
“Also use static IPv4 settings” option preserves the valid systemd-networkd mixed DHCP/static mode.
Changing the interface used to access the Web UI can disconnect the browser and may require reconnecting with
the new address.

Reset to factory defaults stages removal of `/etc/systemd/network/<interface>.network`; the file is removed only
when Apply succeeds. Apply uses a same-filesystem backup and restores all staged files if networkd reload fails.
Cancel reset abandons the staged removal. Reset state is retained by the backend across page navigation and reloads,
and it does not apply the change until Apply is pressed.

The page edits simple `[Network]` IPv4 settings and a narrow structured `[Address]` form with exactly one IPv4
address. Structured address metadata such as `Label=` and `DuplicateAddressDetection=` is preserved. Files with
multiple or ambiguous address entries, structured route properties, or networkd drop-ins under `/run/systemd/network/`, `/etc/systemd/network/`,
`/lib/systemd/network/`, `/usr/lib/systemd/network/`, or `/usr/local/lib/systemd/network/`, are rejected rather than
partially rewritten. The Web UI currently uses the interface-specific filename under `/etc`; installations whose
distribution file has an earlier matching filename may therefore require an administrator to adjust networkd file
precedence manually. In the API, `ipv4_addresses[0]` is the primary address and `ipv4_addresses[1]` is the fallback;
a fallback-only configuration is represented with an empty first element.

### UI ports

Two relevant ports can be specified.

The port for the UI websocket connection is configured in [`backend.conf`](./backend/api/config/backend.conf).

In `frontend.conf`, it needs to be ensured that the same backend port is used in the entry `backend_ws`.

The template for that file is [`backend/webserver/config/frontend.conf.in`](./backend/webserver/config/frontend.conf.in). The installed file is created during `cmake`/`make install` and placed in `/etc/everest-ui/frontend.conf`.

The entry `port` in `frontend.conf` defines on which port the UI is accessible from the browser.

## Start the UI

The UI is started by running the `webui` binary.

Example:

```bash
${prefix}/bin/webui
```

If you use the default install prefix, that is:

```bash
/usr/local/bin/webui
```

The launcher `webui` starts the helper binaries from `${prefix}/libexec/everest-ui/`.

If the systemd service was installed and is not started automatically, it can be started with:

```bash
systemctl start webui.service
```

## Access the UI

The UI is accessed via the browser using the IP address of the device together with the port specified in `/etc/everest-ui/frontend.conf`.

Example:

```text
http://<device-ip>:8081/
```
