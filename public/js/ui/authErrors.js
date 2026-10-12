// SPDX-License-Identifier: MIT
// Copyright 2026 chargebyte GmbH

const messages = {
  invalid_credentials: 'Username or password is incorrect.',
  missing_credentials: 'Enter a username and password.',
  invalid_json: 'The login request could not be processed.',
  setup_not_required: 'A WebUI user already exists.',
  setup_required: 'Create the WebUI user before logging in.',
  host_not_allowed: 'Open this page via the device IP address or add the hostname to allowed_hosts.',
  reset_temporarily_unavailable: 'Password reset is temporarily busy. Please try again.',
  reset_failed: 'Password reset failed or is no longer available.',
  reset_unavailable: 'Password reset failed or is no longer available.',
  'Invalid username': 'Use only letters, numbers, dots, dashes, or underscores for the username.',
  'Invalid password': 'Use a password with at least 8 characters.'
};

export function formatAuthError(error) {
  return messages[error] || error || 'Authentication failed.';
}
