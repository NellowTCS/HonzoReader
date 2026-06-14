#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo " Build Rust lexepub -> liblexepub.a"
"$SCRIPT_DIR/build_lexepub.sh"

echo ""
echo " Build PlatformIO firmware (OTA_APP)"
pio run -e OTA_APP

echo ""
echo " Package firmware.bin → EpubReader.tar"
BIN="$SCRIPT_DIR/.pio/build/OTA_APP/firmware.bin"
TAR="$SCRIPT_DIR/EpubReader.tar"
if [ -f "$BIN" ]; then
    cp "$BIN" /tmp/EpubReader.bin
    tar cf "$TAR" -C /tmp EpubReader.bin
    rm -f /tmp/EpubReader.bin
    ls -lh "$TAR"
    echo "Done"
else
    echo "Error: $BIN not found" >&2
    exit 1
fi
