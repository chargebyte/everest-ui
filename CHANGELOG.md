# Changelog

## Unreleased

- WebUI: Reject malformed HTTP requests with `400`, including requests with
  `Transfer-Encoding`, invalid header names, folded header lines, or duplicate
  `Host`, `Origin`, `Content-Type`, or `Content-Length` headers.
