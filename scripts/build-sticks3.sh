#!/bin/bash
# Build (and optionally flash) the AICW Stick firmware for the M5Stack StickS3.
#   scripts/build-sticks3.sh            build
#   scripts/build-sticks3.sh flash      build + flash the first /dev/cu.usbmodem* (macOS) or $PORT
# Needs ESP-IDF 6.0 (export.sh at $IDF_PATH or ~/esp/esp-idf-v6).
set -e
cd "$(dirname "$0")/../esp32"
. "${IDF_PATH:-$HOME/esp/esp-idf-v6}/export.sh" >/dev/null 2>&1
[ -f dev_signing_key.pem ] || idf.py secure-generate-signing-key --version 2 --scheme rsa3072 dev_signing_key.pem
ACT=build
if [ "$1" = "flash" ]; then
  PORT=${PORT:-$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)}
  [ -n "$PORT" ] || { echo "plug in the stick"; exit 2; }
  ACT="-p $PORT flash"
fi
idf.py -B build-aicw-sticks3 -DIDF_TARGET=esp32s3 -DSDKCONFIG=build-aicw-sticks3/sdkconfig \
  "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;devices/sdkconfig.dou;devices/sdkconfig.dou-m5stack-sticks3;devices/sdkconfig.aicw" \
  $ACT
