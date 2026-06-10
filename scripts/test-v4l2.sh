#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/v4l2-$(date +%Y%m%d-%H%M%S)"
LOG="${ARTIFACT_DIR}/test.log"

mkdir -p "${ARTIFACT_DIR}"
exec > >(tee -a "${LOG}") 2>&1

cd "${ROOT_DIR}"

echo "== Environment =="
uname -a
echo

echo "== Build =="
make clean
make
echo

echo "== Load dependencies =="
sudo modprobe -a videobuf2_vmalloc videobuf2_v4l2 videobuf2_dma_contig
echo

echo "== Reload vcam =="
sudo rmmod vcam 2>/dev/null || true
sudo insmod vcam.ko
echo

echo "== Detect video device =="
sudo ./vcam-util -l | tee "${ARTIFACT_DIR}/vcam-util-list.txt"
VIDEO_DEV="$(sudo ./vcam-util -l |
    awk '/\/dev\/video[0-9]+/ {print $NF; exit}')"

if [[ -z "${VIDEO_DEV}" ]]; then
    echo "ERROR: cannot find a vcam video device"
    exit 1
fi

echo "VIDEO_DEV=${VIDEO_DEV}" | tee "${ARTIFACT_DIR}/device.env"
echo

echo "== v4l2-compliance =="
sudo v4l2-compliance -d "${VIDEO_DEV}" -f |
    tee "${ARTIFACT_DIR}/v4l2-compliance.txt"
echo

echo "== v4l2-ctl --all =="
sudo v4l2-ctl -d "${VIDEO_DEV}" --all |
    tee "${ARTIFACT_DIR}/v4l2-ctl-all.txt"
echo

echo "== v4l2-ctl --list-formats-ext =="
sudo v4l2-ctl -d "${VIDEO_DEV}" --list-formats-ext |
    tee "${ARTIFACT_DIR}/v4l2-formats-ext.txt"
echo

echo "== dmesg tail =="
sudo dmesg | tail -n 100 |
    tee "${ARTIFACT_DIR}/dmesg-tail.txt" || true
echo

echo "Artifacts saved to: ${ARTIFACT_DIR}"
