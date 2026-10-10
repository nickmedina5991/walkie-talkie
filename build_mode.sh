#!/usr/bin/env bash
# Build (and optionally flash) one app mode in its own build folder, so switching
# modes does not need menuconfig and does not rebuild everything.
#   ./build_mode.sh hwtest                 build only
#   ./build_mode.sh walkie /dev/ttyUSB0    build, flash and open the serial monitor
# Modes: hwtest, loopback, radiotest, walkie
set -euo pipefail
mode="${1:-}"
port="${2:-}"
if [[ ! -f "modes/${mode}.cfg" ]]; then
    echo "usage: $0 {hwtest|loopback|radiotest|walkie} [serial-port]" >&2
    exit 1
fi
build="build_${mode}"
args=(-B "$build" -D SDKCONFIG="$build/sdkconfig" -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;modes/${mode}.cfg")
if [[ -n "$port" ]]; then
    idf.py "${args[@]}" -p "$port" build flash monitor
else
    idf.py "${args[@]}" build
fi
