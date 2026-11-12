# tender-firmware

Firmware and on-device software for **TENDER TND-01** — a self-contained wearable unit for restaurant floor work. FIELD SERVICE UNIT.

Concept, functions and design language live in [tender-vault](https://github.com/milvalen/tender-vault) (Russian). This README covers only building and running the code.

## Status

Planned. Stage 0 targets a ready-made ESP32-S3 board with a round display, a microphone and a battery (no soldering). See `20-device/device.md` in the vault.

## Planned layout

| Path | Contents |
|---|---|
| `ui-unit/` | ESP32-S3 interface unit: table board, timers, haptics, menu trainer (PlatformIO, LVGL) |
| `voice-unit/` | Raspberry Pi Zero 2 W voice unit: offline Russian speech recognition and order parsing |
| `tools/` | Desktop scripts: menu import, shift log export |

## Branches

`dev` is the default branch. `main` is the release branch and is never ahead of `dev`. Agents work in `ai/<area>` branches.
