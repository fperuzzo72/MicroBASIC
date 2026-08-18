# TinyBasic patch set

Brings [Stefan Lenz's IoT BASIC](https://github.com/slviajero/tinybasic) into
this firmware as the BASIC interpreter, replacing My-Basic.

The source is **not vendored**. `fetch.sh` clones upstream at a pinned commit,
copies the portable interpreter core into `editor/lib/TinyBasic/` (gitignored)
and applies the patches here. Two reasons:

- Upstream's licensing is ambiguous — the root `LICENSE` (2025) is
  BSD-3-Clause, but every source file header still carries a GPL v3 notice.
  Fetching at build time means no third-party source lives in this repository,
  so the question never has to be answered. GPL obligations trigger on
  distribution; a personal build distributes nothing.
- It's the same pattern already used for the readers in `patches/cpr-vcodex/`.

A full-history mirror lives at `../../_backups/stefan-tinybasic.bundle` in case
upstream disappears. To build from it:

```bash
UPSTREAM=/Users/fperuzzo/github/_backups/stefan-tinybasic.bundle ./patches/tinybasic/fetch.sh
```

## Usage

```bash
./patches/tinybasic/fetch.sh     # from the repo root
cd editor && pio run -e xteink_x4
```

Re-run `fetch.sh` after changing any patch — it wipes and repopulates
`editor/lib/TinyBasic/` from scratch, so the patches always apply to pristine
upstream source and can never stack on themselves.

## Which upstream files, and why

Upstream ships the same interpreter twice: `Basic2/Posix/basic.c` and
`Basic2/IoTBasic/IoTBasic.ino`. They differ by 82 lines. This patch set takes
the **Posix** one, because it is plain portable C with no Arduino dependencies
(verified: it compiles standalone on the host), which makes it a library rather
than a sketch. Its companion `runtime.c` is *not* taken — that's just one
implementation of the runtime contract, and ours is `editor/src/tb_runtime.cpp`.

## The patches

| Script | What |
|---|---|
| `01_configure_language.py` | Explicit language feature set instead of upstream's board-size heuristics. Keeps `HASMSSTRINGS` (`LEFT$`/`RIGHT$`/`MID$`) and `HASDARTMOUTH` (`DEF FN`, `ON..GOTO`, `READ`/`DATA`); drops Wi-Fi/MQTT/sensors/GPIO/tone/camera, which this device doesn't expose to BASIC and which only widen the runtime contract. |
| `02_rename_entry_points.py` | `setup()`/`loop()` → `basicSetup()`/`basicLoop()` (they'd collide with the firmware's own), and removes upstream's `main()`. |
| `03_c_linkage_and_config.py` | `extern "C"` guards (interpreter is C, our runtime is C++), real `stdint.h` instead of upstream's hand-rolled Arduino-compat typedefs, and a fixed `MEMSIZE`. |
| `04_library_build_flags.py` | `library.json` isolating this project's warnings-as-errors from third-party code. |

Each script fails loudly (`assert`) rather than applying a partial patch.

## The runtime contract

Determined empirically rather than from documentation: compile `basic.c` alone,
diff its undefined symbols against what upstream's runtime defines. That yields
**76 names** — about half real work (character I/O, filesystem, timing,
scheduling) and about half peripherals this device doesn't have (GPIO, MQTT,
RTC, printer, EEPROM), which still have to exist because they're referenced
from tables compiled in regardless of the feature flags, but are never reached.

Reproduce it with:

```bash
cc -c editor/lib/TinyBasic/basic.c -o /tmp/basic.o
nm -u /tmp/basic.o
```

`editor/src/tb_runtime.cpp` implements all of it. Verified by forcing the
linker to pull the interpreter in (a `volatile`-guarded call, since
`--gc-sections` drops an unreferenced one): links clean, no undefined
references, costing ~21KB RAM and ~40KB flash.

## Status

The interpreter compiles, links, and its runtime contract is fully satisfied —
but **nothing calls it yet**. `mb_bridge.cpp` (My-Basic) is still the
interpreter behind the SCREEN_EDITOR. The remaining work is the switchover
itself; see `docs/DEVELOPMENT_LOG.md`.
