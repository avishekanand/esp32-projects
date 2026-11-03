#!/usr/bin/env bash
# Helper for the LCDWIKI ES3C35P (ESP32-S3 + ES8311) board.
# Usage:
#   ./esp.sh build  <sketch_dir>
#   ./esp.sh flash  <sketch_dir>        (compile + upload)
#   ./esp.sh monitor                    (serial @115200)
#   ./esp.sh run    <sketch_dir>        (flash + monitor)
#   ./esp.sh port                       (autodetect current port)
set -euo pipefail

FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi"
BAUD=115200

detect_port() {
  # pick the first usbmodem cu device
  ls /dev/cu.usbmodem* 2>/dev/null | head -1
}

PORT="${ESP_PORT:-$(detect_port)}"

cmd="${1:-}"; shift || true
case "$cmd" in
  port)
    echo "${PORT:-<none found>}" ;;
  build)
    arduino-cli compile --fqbn "$FQBN" "$1" ;;
  flash)
    [ -n "$PORT" ] || { echo "No port found"; exit 1; }
    arduino-cli compile --fqbn "$FQBN" "$1"
    arduino-cli upload   --fqbn "$FQBN" -p "$PORT" "$1" ;;
  monitor)
    [ -n "$PORT" ] || { echo "No port found"; exit 1; }
    arduino-cli monitor -p "$PORT" -c baudrate=$BAUD ;;
  run)
    [ -n "$PORT" ] || { echo "No port found"; exit 1; }
    arduino-cli compile --fqbn "$FQBN" "$1"
    arduino-cli upload   --fqbn "$FQBN" -p "$PORT" "$1"
    arduino-cli monitor  -p "$PORT" -c baudrate=$BAUD ;;
  *)
    echo "Usage: ./esp.sh {build|flash|monitor|run|port} [sketch_dir]"; exit 1 ;;
esac
