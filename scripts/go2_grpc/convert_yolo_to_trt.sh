#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

: "${GO2_MODEL_FILE:=}"
: "${GO2_MODEL_OUT:=}"
: "${GO2_YOLO_IMG_SIZE:=640}"
: "${GO2_YOLO_VENV:=}"
: "${GO2_YOLO_UV_PYTHON:=python3}"
: "${GO2_YOLO_INSTALL_APT:=1}"
: "${GO2_YOLO_SUDO:=1}"
: "${GO2_YOLO_VERIFY_IMAGE:=}"
: "${GO2_YOLO_EXPORT_DEVICE:=0}"
: "${GO2_YOLO_PRECISION:=fp16}"

die() {
  echo "[convert_yolo_to_trt] $*" >&2
  exit 2
}

run_apt_install() {
  if [[ "${GO2_YOLO_INSTALL_APT}" != "1" ]]; then
    return 0
  fi
  local apt_cmd="apt-get update && apt-get install -y python3 python3-venv python3-pip curl"
  if [[ "${GO2_YOLO_SUDO}" == "1" ]]; then
    sudo bash -lc "${apt_cmd}"
  else
    bash -lc "${apt_cmd}"
  fi
}

ensure_uv() {
  if command -v uv >/dev/null 2>&1; then
    return 0
  fi
  run_apt_install || true
  if command -v uv >/dev/null 2>&1; then
    return 0
  fi
  curl -LsSf https://astral.sh/uv/install.sh | sh
  export PATH="${HOME}/.local/bin:${PATH}"
  command -v uv >/dev/null 2>&1 || die "uv installation failed"
}

resolve_trtexec() {
  if command -v trtexec >/dev/null 2>&1; then
    command -v trtexec
    return 0
  fi
  if [[ -x "/usr/src/tensorrt/bin/trtexec" ]]; then
    echo "/usr/src/tensorrt/bin/trtexec"
    return 0
  fi
  die "trtexec not found. Install TensorRT runtime/tools on this machine first."
}

ensure_inputs() {
  [[ -n "${GO2_MODEL_FILE}" ]] || die "GO2_MODEL_FILE is required"
  [[ -f "${GO2_MODEL_FILE}" ]] || die "model file not found: ${GO2_MODEL_FILE}"

  local ext="${GO2_MODEL_FILE##*.}"
  ext="${ext,,}"
  case "${ext}" in
    pt|onnx|engine) ;;
    *) die "unsupported model extension: .${ext} (expected .pt/.onnx/.engine)" ;;
  esac
}

main() {
  ensure_inputs
  ensure_uv

  local model_abs out_abs venv_dir ext base out_dir onnx_path trtexec_bin
  model_abs="$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "${GO2_MODEL_FILE}")"
  ext="${model_abs##*.}"
  ext="${ext,,}"
  base="$(basename "${model_abs}")"
  base="${base%.*}"
  out_dir="$(dirname "${model_abs}")"

  if [[ -n "${GO2_MODEL_OUT}" ]]; then
    out_abs="$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "${GO2_MODEL_OUT}")"
  else
    out_abs="${out_dir}/${base}.engine"
  fi

  if [[ "${ext}" == "engine" ]]; then
    echo "[convert_yolo_to_trt] input already .engine, skip convert: ${model_abs}"
    echo "${model_abs}"
    exit 0
  fi

  if [[ -n "${GO2_YOLO_VENV}" ]]; then
    venv_dir="${GO2_YOLO_VENV}"
  else
    venv_dir="${ROOT_DIR}/.venv-yolo-convert"
  fi

  uv venv "${venv_dir}" --python "${GO2_YOLO_UV_PYTHON}"
  uv pip install --python "${venv_dir}/bin/python" -U pip ultralytics onnx

  if [[ "${ext}" == "pt" ]]; then
    onnx_path="${out_dir}/${base}.onnx"
    "${venv_dir}/bin/python" - <<'PY' "${model_abs}" "${GO2_YOLO_IMG_SIZE}" "${GO2_YOLO_EXPORT_DEVICE}"
import sys
from ultralytics import YOLO

model_path = sys.argv[1]
imgsz = int(sys.argv[2])
device = sys.argv[3]
YOLO(model_path).export(format="onnx", imgsz=imgsz, opset=17, device=device)
PY
  else
    onnx_path="${model_abs}"
  fi

  [[ -f "${onnx_path}" ]] || die "ONNX export failed: ${onnx_path}"

  trtexec_bin="$(resolve_trtexec)"
  local trt_flags=()
  if [[ "${GO2_YOLO_PRECISION}" == "fp16" ]]; then
    trt_flags+=(--fp16)
  fi
  "${trtexec_bin}" --onnx="${onnx_path}" --saveEngine="${out_abs}" "${trt_flags[@]}"
  [[ -f "${out_abs}" ]] || die "TensorRT engine generation failed: ${out_abs}"

  if [[ -n "${GO2_YOLO_VERIFY_IMAGE}" ]]; then
    [[ -f "${GO2_YOLO_VERIFY_IMAGE}" ]] || die "verify image not found: ${GO2_YOLO_VERIFY_IMAGE}"
    "${venv_dir}/bin/python" - <<'PY' "${model_abs}" "${GO2_YOLO_VERIFY_IMAGE}"
import sys
from ultralytics import YOLO

model_path = sys.argv[1]
image_path = sys.argv[2]

model = YOLO(model_path)
result = model(image_path, verbose=False)[0]
if result.boxes is None or len(result.boxes) == 0:
    print("[convert_yolo_to_trt] verify: no detections")
else:
    names = result.names
    cls = result.boxes.cls.tolist()
    conf = result.boxes.conf.tolist()
    for i, c in enumerate(cls):
        name = names.get(int(c), str(int(c)))
        print(f"[convert_yolo_to_trt] verify: {name} conf={conf[i]:.4f}")
PY
  fi

  echo "[convert_yolo_to_trt] engine ready: ${out_abs}"
  echo "${out_abs}"
}

main "$@"
