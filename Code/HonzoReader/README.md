# HonzoReader

ePub reader firmware for the PocketMage clamshell device. Reads `.hzo` files
(See the [Docs](https://nisoku.org/Honzo) for more details on the format.)
Supports TOC, bookmarks, chapter/page navigation, and jump-to-page.

## Build

Requires PlatformIO, Rust with the esp Xtensa toolchain, and `just`.

```sh
just build
```

This runs three scripts in sequence:

1. `scripts/build_honzo.py` -- cross-compile `honzo-c` (Rust) to `xtensa-esp32s3-espidf` and copy the resulting static library into `lib/honzo_c/`.
2. `scripts/build_firmware.py` -- run `pio run -e OTA_APP`.
3. `scripts/package_ota.py` -- wrap `firmware.bin` into `HonzoReader.tar` for OTA distribution.

The default environment is `OTA_APP`. Set `env` in the `Justfile` or pass `ENV=<name>` to build a different PlatformIO environment.

## Testing

PlatformIO tests:

```sh
pio test -e OTA_APP
```

Rust crate tests (honzo submodule):

```sh
just -f third_party/honzo/Justfile test
```

## Key bindings

| Input         | Action                           |
|---------------|----------------------------------|
| LEFT / RIGHT  | Previous / next page             |
| UP / DOWN     | Previous / next chapter          |
| SPACE / ENTER | Next page                        |
| t             | Table of contents                |
| j             | Jump to page number              |
| ?             | Help overlay                     |
| h             | Return to library, save bookmark |
| FN+LEFT       | Exit reader / go up one level    |

TOC navigation uses LEFT/RIGHT to scroll and SPACE/ENTER to select.

## Dependencies

- **PlatformIO** (`espressif32` platform, Arduino framework)
- **Rust** with `cargo +esp` (Xtensa ESP32-S3 target) for the `honzo-c` crate
- **`just`** command runner
- **GxEPD2** (fork:
  [`ashtf8/GxEPD2_Editable_useFastFullUpdate`](https://github.com/ashtf8/GxEPD2_Editable_useFastFullUpdate))
- Adafruit GFX fonts (FreeSerif, FreeMono, etc.)

## Project layout

```text
src/             firmware source
include/         public headers
scripts/         build helpers (Python)
third_party/     honzo submodule
lib/
   third_party/  html_parser, md4c submodules
   honzo_c/      compiled honzo-c static library (generated)
```

## License

Apache-2.0. Same as the PocketMage project.
