# tender-firmware

Firmware and on-device software for **TENDER TND-01** — a self-contained wearable unit for restaurant floor work. FIELD SERVICE UNIT.

Concept, functions, controls and the UX rules live in [tender-vault](https://github.com/milvalen/tender-vault). This README covers only building and running the code.

## Status

`ui-unit/` — stage 0 table board for the M5Stack StickS3. Builds, passes native tests and runs in the desk-test simulator. Not run on a real board yet.

## ui-unit: table board

What it does:

- the selected table and what to do now: `TBL-07` / `BRING CHECK`;
- stage timers with approved thresholds: take order 3 min, offer dessert 15 min, bring check 3 min, guest request 2 min;
- 2 alert levels: high alerts (guest waits) flash once and move the selection to the table, then return after 3 min if not handled; low alerts (dessert) are only marked, and ACK removes them;
- table stages `SEAT` → `ORD` → `MAIN` → `BILL` → `FREE`, with no dessert stage; the STEP key label names the next stage: `ORDER`, `SERVED`, `CHECK`, `CLEAR`;
- a 1 s confirmation after every press; one-step undo, and for 5 s after each STEP the status line shows `HOLD STEP = UNDO`;
- STEP means "done": with an open guest request it reads `DONE` and closes the request; TALK only opens a request;
- during an alert the label of the key that ends it is orange: the STEP key (`DONE`) for a guest request or a stage alert, `ACK` for the dessert hint;
- a table strip shows all tables by number in fixed places, `1 2 3 [4] 5 6 7 8`; NEXT moves the box to the right, hold NEXT (4-key mode) to the left; tables that need an action are orange.

Keys:

| Mode | STEP (key A, front) | NEXT (key B, top) | ACK (red key) | TALK (blue key) |
|---|---|---|---|---|
| 4 keys, Dual Button unit on Grove (`kHasDualButton = true`) | STEP or DONE, hold UNDO | NEXT, hold PREV | ACK, hold MUTE | open a request |
| 2 keys, StickS3 alone | STEP or DONE, hold UNDO | NEXT, hold: ACK (alert shown), else open a request | — | — |

The screen names keys by function (`NEXT`, `ACK`, `TALK`), the same words as engraved on enclosure rev 0.4. In stage 0, without the voice unit, `TALK` opens a guest request. The STEP key shows the stage it records next. The StickS3 has no vibration motor: set `kVibroPin` when one is added.

Serial console (115200 baud) uses the simulator keys: `a` `s` `d` `f` = STEP, NEXT, ACK, TALK; uppercase = hold; `?` = state.

## Build

Native tests of the core (any C++17 compiler):

```bash
make -C ui-unit test
```

Desk test: a terminal simulator runs the same core and interface logic, plays a 32 min scripted shift on 8 tables and scores the player (wrong-table actions, missed high alerts, questions):

```bash
make -C ui-unit sim                    # 4-key mode
make -C ui-unit sim ARGS=--two-keys    # 2-key mode
```

`make test` also runs the shift with a scripted good player (must pass) and a sloppy one (must fail). Each interactive run writes a log to `ui-unit/build/desk-test-*.log`.

Firmware ([PlatformIO](https://platformio.org/)):

```bash
cd ui-unit
pio run                 # build
pio run -t upload       # flash over USB-C
pio device monitor      # serial console
```

Library versions are pinned to commits in `ui-unit/platformio.ini`.

## Layout

| Path | Contents |
|---|---|
| `ui-unit/lib/tableboard/` | Hardware-independent core: table board (`tableboard.h`) and interface logic (`app.h`: key mapping, alerts, screen content) |
| `ui-unit/src/main.cpp` | StickS3 glue: reads keys, draws the screen, serial console |
| `ui-unit/sim/` | Desk-test simulator and shift script |
| `ui-unit/test/` | Native tests of the core |
| `voice-unit/` | Planned: Raspberry Pi Zero 2 W voice unit |
| `tools/` | Planned: menu import, shift log export |

## Branches

`dev` is the default branch. `main` is the release branch and is never ahead of `dev`. Agents work in `ai/<area>` branches.
