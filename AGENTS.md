# Repository Guidelines

## Project Structure & Module Organization

This repository contains the Mote Wand firmware for the ESP32-C3-based FoloToy AI Passport.

- `components/bsp/include/`: public BSP APIs and the hardware pin/configuration source of truth (`bsp_pins.h`).
- `components/bsp/src/`: display, button, audio, battery, and shared-I2C implementations.
- `main/`: gesture capture, recognition, BLE HID, sound, and FUI application code.
- `tests/`: host-side gesture model tests.
- `sdkconfig.defaults`: reproducible target, console, LVGL, and memory defaults.
- `README.md`: wiring, known hardware traps, and the required on-device acceptance checklist.

Keep reusable hardware logic in `components/bsp`; keep board demonstration and UI behavior in `main`.

## Build, Test, and Development Commands

Use ESP-IDF 5.5.x:

```bash
get_idf553                    # Enter the repository's ESP-IDF 5.5.3 environment
idf.py set-target esp32c3     # Configure a fresh checkout
idf.py build                  # Compile firmware and validate dependencies
idf.py flash monitor          # Flash the connected board and open logs
idf.py fullclean              # Remove generated build state when configuration is stale
```

Run `tests/run_host_tests.sh` and a clean `idf.py build` as the minimum checks. Hardware-facing behavior still requires validation on a real device.

## Coding Style & Naming Conventions

Write C using four-space indentation and K&R-style braces, following nearby files. Use `snake_case` for functions and locals, `BSP_*` for public hardware constants, and `s_` for file-local state. Keep BSP APIs prefixed with `bsp_` and prefer `static` for internal symbols. UI text stays English; explanatory comments may be Chinese. Preserve comments documenting hardware-specific register values and memory constraints.

Never commit passwords or other real action sequences. Configure them through the ignored local `sdkconfig` file.

## Testing Guidelines

Before submitting, build from the repository root and inspect warnings. On hardware, verify menu navigation and the affected Display, Button, Audio, or Battery page. For pin, display-rotation, codec-clock, ADC, or DMA changes, explicitly record the observed hardware result in the PR. Do not increase LVGL buffers or audio allocations without checking ESP32-C3 internal RAM usage; the board has no PSRAM.

## Commit & Pull Request Guidelines

History follows Conventional Commit-style subjects such as `feat(bsp): ...`, `feat(demo): ...`, `fix(bsp): ...`, and `docs: ...`. Keep commits focused by subsystem. Pull requests should explain the hardware/revision tested, summarize behavior changes, list build and on-device results, and include photos or screenshots for display changes. Link related issues and call out wiring, pin-map, or compatibility impacts.
