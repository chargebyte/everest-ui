# Changelog

## Unreleased

- WebUI: Require `application/json` for `/auth/setup`. When present, `Origin`
  must match the request host and port; direct API clients must use the JSON
  Content-Type and a matching Origin.
- WebUI: Validate the HTTP Host on `/auth/setup` against device names, IP
  literals, and configured aliases to reject arbitrary hostnames.
- WebUI: Reject malformed requests with `400`, including requests with
  `Transfer-Encoding`, invalid header names, folded header lines, or duplicate
  `Host`, `Origin`, `Content-Type`, or `Content-Length` headers.
