# Configuration Layers

## Locations and Precedence

The launcher, API, and webserver use the same generic configuration loader. Each logical file has two layers:

| Purpose | Distribution defaults | Local overrides |
| --- | --- | --- |
| API and launcher | `${prefix}/share/everest-ui/config/backend.conf` | `/etc/everest-ui/backend.conf` |
| Webserver | `${prefix}/share/everest-ui/config/frontend.conf` | `/etc/everest-ui/frontend.conf` |

The defaults directory follows `CMAKE_INSTALL_DATADIR`; `${prefix}/share` is the normal configuration.
Distribution builds with `CMAKE_INSTALL_PREFIX=/usr` therefore use `/usr/share/everest-ui/config/`.
The default CMake prefix remains `/usr/local`. Staging with `DESTDIR` does not change compiled runtime paths.

Defaults are read first, then local directives override or remove individual keys. Missing files are allowed;
existing required-setting validation still applies to the merged result. A complete legacy file in `/etc`
can therefore work without the distribution file, while a comment-only `/etc` file needs the defaults.
When installed defaults are absent, executable-relative development configurations remain a fallback.

The distributed backend setting values and frontend template values have not changed. EVerest YAML,
controller caches, authentication data, SSH passwords, and networkd drop-ins are separate operational files;
this layering does not relocate or reinterpret them.

## Syntax

Files are UTF-8 plain text. Blank lines and full-line `#` comments are ignored. Leading/trailing whitespace
around keys and values is trimmed. Values may contain `=` and `#`: only the first `=` is a separator, and
inline comments, quoting, environment expansion, and interpolation are not supported.

```ini
# Replace one setting, leaving other defaults inherited.
backend_port=9123

# An explicit empty list: this is not an absent key.
network_iso_high_level_comms_drivers=

# Remove this inherited key from the effective configuration.
!network_device_whitelist.chargebyte,imx93-charge-control-y
```

Within either file, the last directive for a key wins. Across files, the local overlay wins. This resolves
the old inconsistency where some readers used the first duplicate and others the last. Lists are replaced
as complete values per key; different platform-specific keys remain independent.

Removing a local assignment or mask restores the distribution value. A mask (`!key`) instead removes the key
from the effective configuration, even if the distribution provides it. Neither operation guarantees that
the consumer disables a feature: an absent or empty value may activate a compiled fallback.

The merger has no registry of known keys. Prefix queries operate on merged keys and retain explicit empties,
so future module/platform keys need no changes to the merger. Consumers retain their own validation;
the webserver, for example, still rejects unsupported effective keys and invalid required settings.

Frontend `root` and `auth_file` relative paths resolve against the directory of the file supplying each value,
not against the directory of the last layer. Absolute paths behave as before.

The default `auth_file` is `/var/lib/everest-ui/auth.json`, created on first authentication setup.
An explicit `auth_file` override remains authoritative. Existing installations with a different path
in `/etc/everest-ui/frontend.conf` must remove or update that override to use the default location;
authentication data is not migrated automatically.

## Reloads and Errors

Launcher and webserver settings take effect after restart. Backend reads reload the layers, allowing existing
request-time settings to change without restart; settings consumed only at startup, such as the listening port,
still require restart. Configure both backend `backend_port` and frontend `backend_ws` consistently.

Malformed directives and unreadable existing files produce path/line or file diagnostics. Initial loading
fails rather than silently treating a broken overlay as missing. After a successful backend load, a later
syntax/read failure retains the complete last-good snapshot and logs a warning; a repaired file is picked up
on the next read. Typed setting validation and existing built-in fallbacks remain the consumer's responsibility.

Missing defaults at startup remain supported for legacy installations with complete local configuration.
Once the backend store has accepted a defaults file, its later disappearance also retains the entire
last-good snapshot. Once accepted defaults contain directives, replacing them with an empty, whitespace-only,
or comment-only file does the same. Repeated identical failures warn only once until recovery. Overlay
deletion is still accepted and restores inheritance. Explicit empty assignments and masks count as directives.

This guard does not detect syntactically valid partial defaults files. Producers should replace defaults
atomically rather than truncate and rewrite them in place. The standalone loader remains stateless;
these runtime protections apply to the shared configuration store.

## Saving and Updates

Only local overlays are writable configuration targets. The shared C++ API in `backend/config/LayeredConfig.hpp`
provides `writeOverride(Kind, key, Update, value, error)` for application callers. Its `Set`, `Mask`, and `Inherit`
operations respectively write an assignment, write `!key`, or remove local directives for that key. Empty
`Set` values are supported. `writeOverlay(Paths, ...)` is the corresponding explicit-path operation for tests
and integrations; it writes only `Paths.overlay`, never a merged snapshot or `Paths.defaults`.

Saving preserves unrelated entries and comments, serializes concurrent writers with a lock, and commits via
an atomic replacement. Writers reject symlink targets/parent directories, identical default/overlay targets,
invalid directives, and malformed existing overlays rather than following redirects or overwriting bad input.
No network configuration-editor API is introduced by this change.

Installation creates documented comment-only local files only when absent, including preserving existing
dangling symlinks. Distribution defaults remain ordinary package-owned files that can change on upgrade.
Both distribution files end with a comment-only **Local Overrides** guide, separate from active settings.
It remains available under `/usr/share/everest-ui/config/` in `/usr` installations even if a factory reset
removes `/etc/everest-ui/`. Recreate missing local directories/files with only the desired overrides;
do not copy the complete defaults. The existing documented `/etc` templates remain unchanged, and the
programs do not automatically recreate deleted overlays at runtime.
Distributions must mark `/etc/everest-ui/backend.conf` and `/etc/everest-ui/frontend.conf` as configuration files
with their package manager's preserve-local-changes policy. Image-based updates must likewise preserve the
local `/etc` layer. CMake installation alone cannot control a package manager's update policy.

Legacy full local files are not automatically minimized: an entry equal to today's default might be an
intentional pin. Administrators can back up and review them, then remove entries that should inherit future
defaults. New distribution keys are inherited automatically unless explicitly assigned or masked locally.

## Empty and Removed Values

The following existing consumer semantics remain unchanged:

| Setting | Important distinction |
| --- | --- |
| `network_device_whitelist.<compatible>` | Empty means no devices for that match. Masking removes that platform entry; other matching platform entries can still restrict devices. |
| `available_features` | Absent permits all currently supported optional features. Empty or `none` permits none. |
| `safety_controller_available_settings` | Empty or absent selects compiled defaults (all sections); use `none` to disable sections. |
| `network_iso_high_level_comms_drivers` | Empty or absent makes the configured driver list empty. |
| `pcap_max_size_bytes`, `pcap_max_duration_seconds` | Empty, invalid, or non-positive values restore built-in limits; they do not disable limits. |
| `firmware_reboot_command` | Empty or absent falls back to `reboot`; clearing it does not disable rebooting. |
| `factory_reset_command` | Empty or absent restores `/usr/sbin/factory-reset --force --reboot`; clearing it does not disable reset. Existing local overrides without `--reboot` must be updated explicitly. |
| `factory_reset_timeout_seconds` | Empty, absent, invalid, or non-positive restores the 20-second execution timeout. |
| `factory_reset_require_unplugged` | Only case-insensitive `false` bypasses RPC/EVSE discovery and the Unplugged guard; empty, absent, masked, or invalid enforces it. |
| `reboot_wait_timeout_seconds` | First valid matching platform override, then base setting, then 30 seconds. Empty/invalid/non-positive values are skipped. Fetched through the API, not a frontend setting. After this wait, unconfirmed requests may return to a valid session only following a fresh login-allowed status probe started at or after the deadline. Confirmed operations remain blocked until backend permission and session invalidation are observed. Reload changes presentation only; probes continue and backend restrictions are never cleared. |
| `ssh_control_method` | Empty or absent selects `ssh-autostart`; this chooses a control mechanism, not SSH enablement. |
| `app_title` | Empty or absent restores automatic title lookup, not a blank title. |
| `allow_origin` | Empty or absent disables the additional configured-origin check; authentication is unchanged. |
| Required ports, paths, and connection settings | Clearing/removing these can fail validation or make their dependent operation unavailable. In particular, clearing `auth_file` cannot disable authentication. |

Masking a key removes a distribution-provided value generically; it does not bypass these application rules.
