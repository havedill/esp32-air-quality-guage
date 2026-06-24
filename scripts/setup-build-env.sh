#!/usr/bin/env bash
# Prepare a working PlatformIO + ESP-IDF build environment for this project.
#
# Cursor's integrated shell can shadow `python` with the Cursor binary. This
# script creates the project venv in a clean environment so symlinks are correct.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV="${ROOT}/.venv"
PIO_HOME="${PLATFORMIO_HOME:-${HOME}/.platformio}"
PENV="${PIO_HOME}/penv"

pick_python() {
	if command -v uv >/dev/null 2>&1; then
		local uv_py=""
		uv_py="$(uv python find 3.13 2>/dev/null || true)"
		if [[ -z "${uv_py}" ]]; then
			uv_py="$(uv python find 3.12 2>/dev/null || true)"
		fi
		if [[ -n "${uv_py}" && -x "${uv_py}" ]]; then
			echo "${uv_py}"
			return
		fi
	fi
	for candidate in python3.13 python3.12 python3; do
		if command -v "${candidate}" >/dev/null 2>&1; then
			local major minor
			read -r major minor < <("${candidate}" -c 'import sys; print(sys.version_info.major, sys.version_info.minor)')
			if [[ "${major}" -eq 3 && "${minor}" -le 13 ]]; then
				command -v "${candidate}"
				return
			fi
		fi
	done
	echo "No suitable Python (need 3.12 or 3.13; ESP-IDF tooling does not support 3.14+)." >&2
	echo "Install with: uv python install 3.13" >&2
	echo "Or use your OS package manager (e.g. python3.13, python3.12)." >&2
	exit 1
}

discover_idf_venv() {
	local d
	shopt -s nullglob
	local dirs=("${PENV}"/.espidf-*)
	shopt -u nullglob
	if [[ ${#dirs[@]} -eq 0 ]]; then
		echo ""
		return
	fi
	printf '%s\n' "${dirs[@]}" | sort -V | tail -1
}

PY="$(pick_python)"
echo "Using Python: ${PY} ($("${PY}" --version))"

rm -rf "${VENV}"
# Avoid Cursor PATH shadowing python -> Cursor AppImage/binary.
env -i HOME="${HOME}" PATH="/usr/bin:/bin" TERM="${TERM:-xterm}" \
	"${PY}" -m venv "${VENV}"

"${VENV}/bin/pip" install -U pip platformio esptool pyserial

# PlatformIO's ESP-IDF helper venv (.espidf-*) is sometimes created from a
# broken project python (Cursor shadow / 3.14) and ends up missing kconfgen etc.
IDF_VENV="$(discover_idf_venv)"
IDF_PY="${IDF_VENV}/bin/python"
IDF_REQ="${PIO_HOME}/packages/framework-espidf/tools/requirements/requirements.core.txt"
PIO_PY="${PENV}/bin/python"
need_idf_venv=0
if [[ -z "${IDF_VENV}" || ! -x "${IDF_PY}" ]]; then
	need_idf_venv=1
elif ! "${IDF_PY}" -c 'import kconfgen, idf_component_manager' 2>/dev/null; then
	need_idf_venv=1
elif ! "${IDF_PY}" -c 'import sys; exit(0 if sys.version_info[:2] <= (3, 13) else 1)' 2>/dev/null; then
	need_idf_venv=1
fi

if [[ "${need_idf_venv}" -eq 1 ]]; then
	if [[ ! -x "${PIO_PY}" ]]; then
		echo "PlatformIO core venv missing at ${PIO_PY}." >&2
		echo "Run once: ${VENV}/bin/pio --version" >&2
		exit 1
	fi
	if [[ ! -f "${IDF_REQ}" ]]; then
		echo "ESP-IDF requirements not found at ${IDF_REQ}." >&2
		echo "Run a PlatformIO build once to fetch framework-espidf:" >&2
		echo "  ${VENV}/bin/pio run -d ${ROOT}/firmware/air_quality_gauge" >&2
		exit 1
	fi
	if [[ -z "${IDF_VENV}" ]]; then
		idf_ver=""
		if [[ -f "${PIO_HOME}/packages/framework-espidf/idf_version.txt" ]]; then
			idf_ver="$(tr -d '[:space:]' < "${PIO_HOME}/packages/framework-espidf/idf_version.txt")"
		fi
		if [[ -n "${idf_ver}" ]]; then
			IDF_VENV="${PENV}/.espidf-${idf_ver}"
		else
			echo "Cannot determine ESP-IDF venv path (no ${PENV}/.espidf-* yet)." >&2
			echo "Run a PlatformIO build once, then re-run this script." >&2
			exit 1
		fi
	fi
	echo "Recreating PlatformIO ESP-IDF Python env at ${IDF_VENV}..."
	rm -rf "${IDF_VENV}"
	env -i HOME="${HOME}" PATH="/usr/bin:/bin" TERM="${TERM:-xterm}" \
		"${PIO_PY}" -m venv "${IDF_VENV}"
	"${IDF_VENV}/bin/pip" install -U pip
	"${IDF_VENV}/bin/pip" install -r "${IDF_REQ}"
fi

echo ""
echo "Build env ready: $("${VENV}/bin/pio" --version)"
echo "Next: ${ROOT}/scripts/flash-air-quality-gauge.sh"
