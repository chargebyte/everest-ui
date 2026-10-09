# Changelog

## Unreleased

- WebUI: Add one-time credential recovery within a configurable window after a
  hard/cold boot, followed by the existing unlimited first-time setup flow.
  Requires the revised BBNSM kernel interface and a systemd-provided runtime
  directory. Regular software reboots do not enable recovery.
- WebUI: Require `application/json` for `/auth/reset` and `/auth/setup`. When
  present, `Origin` must match the request host and port; direct API clients
  must use the JSON Content-Type and a matching Origin.
- WebUI: Validate the HTTP Host on `/auth/reset` and `/auth/setup` against
  device names, IP literals, and configured aliases to reject arbitrary hostnames.
- WebUI: Reject malformed requests with `400`, including requests with
  `Transfer-Encoding`, invalid header names, folded header lines, or duplicate
  `Host`, `Origin`, `Content-Type`, or `Content-Length` headers.
