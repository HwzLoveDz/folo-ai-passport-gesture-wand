#!/usr/bin/env bash
set -euo pipefail

SIM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${MOTE_WAND_UI_BUILD_DIR:-/tmp/mote-wand-ui-simulator-build}"
OUTPUT_DIR="${MOTE_WAND_UI_OUTPUT_DIR:-${SIM_DIR}/out}"

SIMULATOR="${BUILD_DIR}/mote-wand-ui-sim"
if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cmake -S "${SIM_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
fi

needs_build=false
if [[ ! -x "${SIMULATOR}" ]]; then
    needs_build=true
else
    for source in \
        "${SIM_DIR}/CMakeLists.txt" \
        "${SIM_DIR}/lv_conf.h" \
        "${SIM_DIR}/main.c" \
        "${SIM_DIR}/png_writer.c" \
        "${SIM_DIR}/png_writer.h" \
        "${SIM_DIR}/../main/ui_fui.c" \
        "${SIM_DIR}/../main/ui_fui.h" \
        "${SIM_DIR}/../main/fonts/kode_mono/ui_font_kode_regular_11.c" \
        "${SIM_DIR}/../main/fonts/kode_mono/ui_font_kode_regular_13.c" \
        "${SIM_DIR}/../main/fonts/kode_mono/ui_font_kode_bold_13.c" \
        "${SIM_DIR}/../main/fonts/kode_mono/ui_font_kode_bold_15.c" \
        "${SIM_DIR}/../main/fonts/kode_mono/ui_font_kode_bold_21.c"; do
        if [[ "${source}" -nt "${SIMULATOR}" ]]; then
            needs_build=true
            break
        fi
    done
fi
if [[ "${needs_build}" == true ]]; then
    cmake --build "${BUILD_DIR}" --parallel "$(nproc)"
fi
mkdir -p "${OUTPUT_DIR}"

"${SIMULATOR}" --state home --font current \
    --output "${OUTPUT_DIR}/current-home.png"
for state in pin menu detail clear-confirm clear-done clear-error recording success; do
    "${SIMULATOR}" --state "${state}" --font current \
        --output "${OUTPUT_DIR}/current-${state}.png"
done

if [[ -f "${SIM_DIR}/../managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf" &&
      -f "${SIM_DIR}/../managed_components/lvgl__lvgl/tests/src/test_files/fonts/Montserrat-Bold.ttf" ]]; then
    "${SIMULATOR}" --state home --font weighted \
        --output "${OUTPUT_DIR}/weighted-home.png"
fi

for state in home pin menu; do
    "${SIMULATOR}" --state "${state}" --font kode \
        --output "${OUTPUT_DIR}/kode-${state}.png"
done

for font in space plex; do
    case "${font}" in
        space)
            regular_font="${SIM_DIR}/font-cache/SpaceMono-Regular.ttf"
            bold_font="${SIM_DIR}/font-cache/SpaceMono-Bold.ttf"
            ;;
        plex)
            regular_font="${SIM_DIR}/font-cache/IBMPlexMono-Regular.ttf"
            bold_font="${SIM_DIR}/font-cache/IBMPlexMono-Bold.ttf"
            ;;
    esac
    if [[ ! -f "${regular_font}" || ! -f "${bold_font}" ]]; then
        continue
    fi
    for state in home pin menu; do
        "${SIMULATOR}" --state "${state}" --font "${font}" \
            --output "${OUTPUT_DIR}/${font}-${state}.png"
    done
done

printf 'LVGL previews: %s\n' "${OUTPUT_DIR}"
