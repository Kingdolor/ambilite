#!/bin/bash
# Baut Ambilite (Dienst + Capture-Backends + App) und packt alles in eine IPK.
# Voraussetzung: webOS-Toolchain (openlgtv/buildroot-nc4), siehe README.
set -euo pipefail
cd "$(dirname "$0")"

TOOLCHAIN_DIR=${TOOLCHAIN_DIR:-/opt/arm-webos-linux-gnueabi_sdk-buildroot}
TOOLCHAIN_FILE=${TOOLCHAIN_FILE:-$TOOLCHAIN_DIR/share/buildroot/toolchainfile.cmake}
STRIP=$TOOLCHAIN_DIR/bin/arm-webos-linux-gnueabi-strip

mkdir -p build dist
(cd build && cmake -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DCMAKE_BUILD_TYPE=Release .. >/dev/null \
  && make -j"$(nproc)" ambilite gm_backend halgal_backend dile_vt_backend vtcapture_backend)

STAGE=build/stage
rm -rf "$STAGE" && mkdir -p "$STAGE/app" "$STAGE/service"
cp frontend/index.html frontend/app.js frontend/style.css frontend/appinfo.json frontend/icon.png frontend/largeIcon.png "$STAGE/app/"
cp servicefiles/services.json servicefiles/ambiliteautostart servicefiles/leds.json servicefiles/config.default.json "$STAGE/service/"
cp -r www "$STAGE/service/www"
cp build/ambilite build/libvtcapture_backend.so build/libdile_vt_backend.so build/libgm_backend.so build/libhalgal_backend.so "$STAGE/service/"
"$STRIP" "$STAGE/service/ambilite" "$STAGE"/service/*.so
chmod 755 "$STAGE/service/ambilite" "$STAGE/service/ambiliteautostart"

python3 tools/make_ipk.py "$STAGE/app" "$STAGE/service" dist
