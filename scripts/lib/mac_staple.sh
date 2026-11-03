# shellcheck shell=bash
# Staple a notarization ticket, retrying: stapler fetches the ticket from Apple's CloudKit, which can drop the
# connection (error 68) even after notarization was Accepted. Source from release scripts; do not execute directly.

mac_staple() {
  local target="${1:?path to staple}"
  local attempt
  for attempt in 1 2 3 4 5; do
    if xcrun stapler staple "$target"; then
      return 0
    fi
    if [ "$attempt" -lt 5 ]; then
      echo "stapler failed (attempt $attempt/5), retrying in $((attempt * 15))s" >&2
      sleep $((attempt * 15))
    fi
  done
  echo "stapler failed 5 times: $target" >&2
  return 1
}
