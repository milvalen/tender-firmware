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
- table stages `SEAT` → `ORD` → `MAIN` → `BILL` → `PAY` → `FREE`, with no dessert stage; the STEP key label names the next stage: `ORDER`, `SERVED`, `CHECK`, `BROUGHT`, `CLEAR`. `PAY` (check brought, the guests pay) has no timer, so a slow payment gives no `BRING CHECK` alert;
- a 1 s confirmation after every press; one-step undo, and for 5 s after each STEP the status line shows `HOLD STEP = UNDO`;
- STEP means "done": with an open guest request it reads `DONE` and closes the request; TALK only opens a request;
- during an alert the label of the key that ends it is orange: the STEP key (`DONE`) for a guest request or a stage alert, `ACK` for the dessert hint;
- hold ACK turns `MUTED` (quiet floor) on or off; it stays on until the next hold. In `MUTED` alerts come by vibration only, with no screen burst, and the screen turns off when idle; the first key press only turns it on. Outside `MUTED` the idle screen dims;
- a table strip shows all tables by number in fixed places, `1 2 3 [4] 5 6 7 8`; NEXT moves the box to the right, hold NEXT (4-key mode) to the left; tables that need an action are orange.

Keys:

| Mode | STEP (key A, front) | NEXT (key B, top) | ACK (red key) | TALK (blue key) |
|---|---|---|---|---|
| 4 keys, Dual Button unit on Grove (`kHasDualButton = true`) | STEP or DONE, hold UNDO | NEXT, hold PREV | ACK, hold MUTE | open a request |
| 2 keys, StickS3 alone | STEP or DONE, hold UNDO | NEXT, hold: ACK (alert shown), else open a request | — | — |

The screen names keys by function (`NEXT`, `ACK`, `TALK`), the same words as engraved on enclosure rev 0.4. In stage 0, without the voice unit, `TALK` opens a guest request. The STEP key shows the stage it records next. The StickS3 has no vibration motor: set `kVibroPin` when one is added.

Serial console (115200 baud) uses the simulator keys: `a` `s` `d` `f` = STEP, NEXT, ACK, TALK; uppercase = hold; `?` = state; `!` = board self-test.

### Board self-test

For the day the boards arrive (part E of the measurement sheet and the vibrator desk test in the vault). In `pio device monitor`, type `!`. The table board stops; the screen shows the key counts, the level of each Dual Button pin and what a press reads, the motor state, the battery and the USB voltage, and the reset reason (`BROWNOUT` after a restart when the motor starts). Commands, one per line:

| Command | Does |
|---|---|
| `keys` | key counts; after a press of each Dual Button key, the value for `kDualActiveHigh` |
| `pull up` / `pull down` | input pull on the Dual Button pins |
| `rot 1` / `rot 3` | display rotation; the bottom line `< HAT2 END` must be at the Hat2 end |
| `vibro <pin>` / `vibro off` | motor GPIO: 1-3, 5-8 (Hat2) or 9, 10 (Grove). Not 4 (Boot) or 43, 44 (UART0) |
| `duty <0-128>` | motor PWM duty of 255; capped at 50 % |
| `pulse`, `on`, `off` | one alert pattern (2 s), motor on, motor off |
| `power`, `log` | battery line now; a battery line every 10 s on or off |
| `exit` | restart into the table board |

The StickS3 power chip does not measure the battery current: the screen shows `I N/A`; use a multimeter. Write the results into the constants at the top of `src/main.cpp`.

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
| `ui-unit/lib/tableboard/` | Hardware-independent core: table board (`tableboard.h`), interface logic (`app.h`: key mapping, alerts, screen content) and the board self-test (`selftest.h`) |
| `ui-unit/src/main.cpp` | StickS3 glue: reads keys, draws the screen, serial console |
| `ui-unit/sim/` | Desk-test simulator and shift script |
| `ui-unit/test/` | Native tests of the core |
| `voice-unit/` | Planned: Raspberry Pi Zero 2 W voice unit |
| `tools/` | Planned: menu import, shift log export |

## Branches

`dev` is the default branch. `main` is the release branch and is never ahead of `dev`. Agents work in `ai/<area>` branches and merge them into `feature/<area>`. The owner reviews and accepts the work on `feature/<area>`; then it is merged into `dev`.
