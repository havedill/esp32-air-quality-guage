#!/usr/bin/env bash
# Build and flash firmware/air_quality_gauge (stepper + TM1637 + SCD41).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-/dev/ttyUSB0}"
PIO="${ROOT}/.venv/bin/pio"
if [[ ! -x "${PIO}" ]]; then
	PIO="${PLATFORMIO_HOME:-$HOME/.platformio}/penv/bin/pio"
fi

if [[ ! -x "${PIO}" ]]; then
	echo "PlatformIO not found. Run: ${ROOT}/scripts/setup-build-env.sh" >&2
	exit 1
fi

cd "${ROOT}/firmware/air_quality_gauge"
echo "Building air_quality_gauge ..."
"${PIO}" run
echo "Flashing to ${PORT} (NVS cal partition preserved) ..."
"${PIO}" run -t upload --upload-port "${PORT}"
echo "Done. Monitor:"
echo "  ${PIO} device monitor -p ${PORT} -b 115200"
