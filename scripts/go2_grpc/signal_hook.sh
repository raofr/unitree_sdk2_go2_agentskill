#!/usr/bin/env bash
set -euo pipefail

# Minimal signaling hook for AudioWebRtcManager.
# Called by server as:
#   signal_hook.sh offer  <go2_host_ip> <payload_file>
#   signal_hook.sh ice    <go2_host_ip> <payload_file>
#   signal_hook.sh answer <go2_host_ip> <payload_file>
#
# Environment variables to customize:
#   GO2_SIGNAL_BASE_URL        (required)
#   GO2_SIGNAL_TOKEN           (optional bearer token)
#   GO2_SIGNAL_TIMEOUT_SEC     (optional, default 8)
#
# Default API shape (customize as needed):
#   POST ${BASE_URL}/offer   -> returns answer SDP in response body
#   POST ${BASE_URL}/ice     -> no body required
#   GET  ${BASE_URL}/answer  -> returns answer SDP in response body

event="${1:-}"
go2_host_ip="${2:-}"
payload_file="${3:-}"

if [[ -z "${event}" || -z "${go2_host_ip}" || -z "${payload_file}" ]]; then
  echo "usage: $0 <offer|ice|answer> <go2_host_ip> <payload_file>" >&2
  exit 2
fi

if [[ ! -f "${payload_file}" ]]; then
  echo "payload file not found: ${payload_file}" >&2
  exit 3
fi

: "${GO2_SIGNAL_BASE_URL:=}"
: "${GO2_SIGNAL_TOKEN:=}"
: "${GO2_SIGNAL_TIMEOUT_SEC:=8}"

if [[ -z "${GO2_SIGNAL_BASE_URL}" ]]; then
  echo "GO2_SIGNAL_BASE_URL is required" >&2
  exit 4
fi

curl_common=(
  --silent --show-error --fail
  --max-time "${GO2_SIGNAL_TIMEOUT_SEC}"
  -H "Content-Type: text/plain"
  -H "X-GO2-Host-IP: ${go2_host_ip}"
)

if [[ -n "${GO2_SIGNAL_TOKEN}" ]]; then
  curl_common+=(-H "Authorization: Bearer ${GO2_SIGNAL_TOKEN}")
fi

case "${event}" in
  offer)
    # Input: local SDP offer in payload_file
    # Output: print remote answer SDP to stdout
    curl "${curl_common[@]}" \
      --data-binary "@${payload_file}" \
      "${GO2_SIGNAL_BASE_URL%/}/offer"
    ;;

  ice)
    # Input: "<mline_index>\n<candidate>"
    # Output: optional (ignored by server)
    curl "${curl_common[@]}" \
      --data-binary "@${payload_file}" \
      "${GO2_SIGNAL_BASE_URL%/}/ice" >/dev/null
    ;;

  answer)
    # Optional pull-mode fallback: fetch latest answer SDP.
    # If your signaling system is push-on-offer only, you can return empty output.
    curl "${curl_common[@]}" \
      "${GO2_SIGNAL_BASE_URL%/}/answer"
    ;;

  *)
    echo "unknown event: ${event}" >&2
    exit 5
    ;;
esac
