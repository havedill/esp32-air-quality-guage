# ESP32 Air Quality Gauge

Analog-style CO₂ gauge built on an ESP32: a stepper motor drives a needle across a printed dial, a TM1637 shows live readings, and a Sensirion SCD41 measures CO₂, temperature, and humidity.

## Hardware

| Component | Role |
|-----------|------|
| ESP32-DevKitC (or similar) | Main controller |
| 28BYJ-48 stepper + ULN2003 driver | Needle motion |
| TM1637 4-digit display | Rotating ppm / temp / humidity readout |
| Sensirion SCD41 | CO₂ + temp + RH sensor (I²C) |
| Printed dial | 400–2200 ppm scale, 260° sweep |

GPIO map (see `firmware/air_quality_gauge/main/main.c`):

| Function | GPIO |
|----------|------|
| Stepper IN1–IN4 | 32, 33, 26, 27 |
| TM1637 CLK / DIO | 17, 18 |
| SCD41 SDA / SCL | 21, 22 |

Wiring diagram: [docs/co2-gauge-breadboard.html](docs/co2-gauge-breadboard.html)

Dial CAD: [models/dial.py](models/dial.py) → `models/dial.step`

## Quick start

**Prerequisites:** Python 3.12 or 3.13 (3.14 is not supported by ESP-IDF tooling). [uv](https://docs.astral.sh/uv/) is optional but recommended for Python installs. USB serial drivers for your ESP32 board.

```bash
# One-time: project venv + PlatformIO + ESP-IDF toolchain
./scripts/setup-build-env.sh

# Build and flash (default /dev/ttyUSB0; use COM3 on Windows, /dev/cu.* on macOS)
./scripts/flash-air-quality-gauge.sh

# Serial monitor (115200 baud)
.venv/bin/pio device monitor -p /dev/ttyUSB0 -b 115200
```

On boot the display briefly shows `8888`, then rotates **CO₂ ppm** → **temperature (°F)** → **humidity** every 4 seconds.

## Serial calibration

Connect at 115200 baud. Type `help` for the full command list.

Typical first-time calibration:

1. `follow off` — pause auto needle tracking
2. `goto 400` then `jog` as needed; `cal min`
3. `goto 2200` then `jog` as needed; `cal max`
4. `cal save` — persist to NVS
5. `follow on` — resume tracking

Other useful commands: `sweep`, `home`, `sensor`, `log off`, `temp offset`.

After a full chip erase, firmware falls back to baked-in defaults in `gauge_cal_defaults.h` until you re-calibrate.

## Recovery

If flash is corrupted or the board won't boot:

```bash
./scripts/recover-esp32-flash.sh /dev/ttyUSB0
```

This erases the entire flash chip (including NVS calibration) and reflashes firmware.
