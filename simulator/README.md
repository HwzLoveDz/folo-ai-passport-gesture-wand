# Mote Wand LVGL host preview

This renderer compiles the real `main/ui_fui.c` against the same managed LVGL
source as the firmware. It produces deterministic 240 x 320 PNG screenshots
without ESP-IDF, SDL, a serial port, or a connected device.

```bash
bash simulator/preview.sh
```

The first host build is cached under `/tmp/mote-wand-ui-simulator-build` for
speed. Later UI-only edits rebuild and render locally without touching the
firmware build directory. PNG files are written to `simulator/out/`.

- `current-*.png`: selected embedded Kode Mono hierarchy in every UI state.
- `weighted-home.png`: proposed Medium/Bold hierarchy.
- `kode-*.png`: Kode Mono, a futuristic terminal direction.
- `space-*.png`: Space Mono, a retro-space direction.
- `plex-*.png`: IBM Plex Mono, an industrial instrument direction.

Space Mono and IBM Plex Mono comparison fonts live in the ignored `font-cache/`
directory and are skipped when their files are absent. The selected Kode Mono
source fonts are versioned with the firmware; the `current-*` previews use the
same compact embedded C fonts that the ESP32 firmware uses.
