#!/usr/bin/env bash
# Build/Flash helper for the ESP32 weather display (uses ESP-IDF via Docker).
set -e
cd "$(dirname "$0")"

IDF_IMG="espressif/idf:v5.5.5"
PORT="${PORT:-/dev/ttyACM0}"

run() {
    docker run --rm \
        -u "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -v "$PWD:/project" \
        -w /project \
        "$IDF_IMG" "$@"
}

case "${1:-build}" in
    build)
        run idf.py build
        ;;
    flash)
        docker run --rm \
            --device "/dev/ttyACM0" \
            -v "$PWD:/project" \
            -w /project \
            "$IDF_IMG" idf.py -p "$PORT" flash
        ;;
    monitor)
        docker run --rm \
            --device "/dev/ttyACM0" \
            -v "$PWD:/project" \
            -w /project \
            "$IDF_IMG" idf.py -p "$PORT" monitor
        ;;
    *)
        echo "usage: $0 [build|flash|monitor]" >&2
        exit 1
        ;;
esac