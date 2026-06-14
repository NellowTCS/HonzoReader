#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LEXEPUB_DIR="$(cd "$SCRIPT_DIR/../PocketMage_V3_EpubReader/third_party/lexepub" && pwd)"
LIB_DIR="$SCRIPT_DIR/lib/lexepub_c"
REL_DIR="$LIB_DIR/xtensa-esp32s3-espidf/release"

detect_esp_toolchain() {
    if command -v xtensa-esp32s3-elf-gcc &>/dev/null; then return 0; fi
    for try in "$HOME/export-esp.sh" "$HOME/esp/esp-idf/export.sh" \
                /opt/esp/export.sh /opt/esp-idf/export.sh; do
        if [ -f "$try" ]; then source "$try" 2>/dev/null; break; fi
    done
    command -v xtensa-esp32s3-elf-gcc &>/dev/null || {
        echo "Error: xtensa-esp32s3-elf-gcc not found. Source your esp toolchain first." >&2
        exit 1
    }
}

detect_rust_esp() {
    if ! command -v cargo &>/dev/null; then
        for try in "$HOME/.cargo/env" "$HOME/.rustup/env"; do
            if [ -f "$try" ]; then source "$try" 2>/dev/null; break; fi
        done
    fi
    if ! command -v cargo &>/dev/null; then
        echo "Error: cargo not found. Install Rust: https://rustup.rs" >&2
        exit 1
    fi
    if ! cargo +esp build --help &>/dev/null 2>&1; then
        echo "Error: Rust ESP toolchain not available. Run: espup install --toolchain-version X.Y.Z.Z" >&2
        exit 1
    fi
}

detect_esp_toolchain
detect_rust_esp

cd "$LEXEPUB_DIR"
echo "=== Building lexepub ==="
cargo +esp build --release --features "c-ffi,lowmem" -p lexepub --lib \
  --target xtensa-esp32s3-espidf -Z build-std=std,panic_abort

TARGET_DIR="$LEXEPUB_DIR/target/xtensa-esp32s3-espidf/release"

mkdir -p "$REL_DIR/deps"
cp "$TARGET_DIR/liblexepub.rlib" "$REL_DIR/"
rm -f "$REL_DIR"/liblexepub.a

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for rlib in "$TARGET_DIR"/deps/*.rlib; do
    crate_name="$(basename "$rlib" | sed 's/lib\(.*\)-[0-9a-f]*\.rlib$/\1/')"
    [ -z "$crate_name" ] && continue
    sub="$WORK/$crate_name"
    mkdir -p "$sub"
    (cd "$sub" && ar x "$rlib" 2>/dev/null || true)
    for o in "$sub"/*.o; do
        [ -f "$o" ] || continue
        base="$(basename "$o")"
        dest="$WORK/${crate_name}_${base}"
        [ -f "$dest" ] || mv "$o" "$dest"
    done
    rmdir "$sub" 2>/dev/null || true
done

TMP_RDIR="$(mktemp -d)"
(cd "$TMP_RDIR" && ar x "$TARGET_DIR/liblexepub.rlib")
for o in "$TMP_RDIR"/*.o; do
    [ -f "$o" ] || continue
    base="$(basename "$o")"
    dest="$WORK/lexepub_${base}"
    [ -f "$dest" ] || mv "$o" "$dest"
done
rm -rf "$TMP_RDIR"
cd "$WORK"

nobj=$(ls *.o 2>/dev/null | wc -l)
echo "=== Objects: $nobj ==="

xtensa-esp32s3-elf-ar rcs "$REL_DIR/liblexepub.a" *.o 2>/dev/null
cp "$TARGET_DIR"/deps/*.rlib "$REL_DIR/deps/" 2>/dev/null || true
cp "$TARGET_DIR"/deps/*.rmeta "$REL_DIR/deps/" 2>/dev/null || true

echo "=== $(basename "$REL_DIR")/liblexepub.a ==="
ls -lh "$REL_DIR/liblexepub.a"
