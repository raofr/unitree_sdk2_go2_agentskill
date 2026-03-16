#!/usr/bin/env bash
set -euo pipefail

: "${GO2_GRPC_ENDPOINT:=192.168.51.213:50051}"
: "${GO2_OWNER:=openclaw}"
: "${GO2_PARALLEL:=false}"
: "${PYTHON_BIN:=python3}"

if [[ $# -lt 1 ]]; then
  echo "usage: $0 ACTION [extra args to go2-grpc action]" >&2
  exit 2
fi

ACTION="$1"
shift

PY_TOOL="${PYTHON_BIN} -m go2_grpc_tool.cli"

if [[ "${GO2_PARALLEL}" != "true" ]]; then
  ${PY_TOOL} --endpoint "${GO2_GRPC_ENDPOINT}" force-close-owner --owner "${GO2_OWNER}" --keep-parallel-sessions
fi

SESSION_JSON="$(${PY_TOOL} --endpoint "${GO2_GRPC_ENDPOINT}" open-session --owner "${GO2_OWNER}" --session-name default)"
SESSION_ID="$(echo "${SESSION_JSON}" | "${PYTHON_BIN}" -c 'import json,sys; print(json.load(sys.stdin)["session_id"])')"

cleanup() {
  ${PY_TOOL} --endpoint "${GO2_GRPC_ENDPOINT}" close-session --session-id "${SESSION_ID}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

${PY_TOOL} --endpoint "${GO2_GRPC_ENDPOINT}" action --session-id "${SESSION_ID}" --action "${ACTION}" "$@"
