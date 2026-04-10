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

It also installs the runtime configuration files into `config/` and the browser assets into `public/`.

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
[project-root]/install
```

## Configure the UI

### EVerest configuration paths

The EVerest paths are configured in [`backend.conf`](./backend/api/config/backend.conf).

The following three configuration entries define the paths of the EVerest configuration files:

- `everest_config_path`
  Points to the configuration file that EVerest uses.

- `everest_base_config_path`
  Points to the location where the base configuration created by the UI is saved. This base configuration is then used by EVerest when it is restarted by the UI.

- `everest_config_overlay_path`
  Points to the location of the configuration file that holds the parameters set with the UI.

### UI ports

Two relevant ports can be specified.

The port for the UI websocket connection is configured in [`backend.conf`](./backend/api/config/backend.conf).

In `frontend.conf`, it needs to be ensured that the same backend port is used in the entry `backend_ws`.

The template for that file is [`backend/webserver/config/frontend.conf.in`](./backend/webserver/config/frontend.conf.in). The installed file is created during `cmake`/`make install`.

The entry `port` in `frontend.conf` defines on which port the UI is accessible from the browser.

## Start the UI

The UI is started by running the `webui` binary.

Example:

```bash
[install path]/bin/webui
```

If you use the default install prefix, that is:

```bash
./install/bin/webui
```

## Access the UI

The UI is accessed via the browser using the IP address of the device together with the port specified in `frontend.conf`.

Example:

```text
http://<device-ip>:8081/
```
