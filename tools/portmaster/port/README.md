# Crusader: No Remorse

Reimplementation of the engine of *Crusader: No Remorse* (Origin, 1995) in C++/SDL2.
Source: https://github.com/sebsebseb1982/crusader-no-remorse-decomp

Many thanks to Origin Systems for the original game.

## Game data

The port contains **no game files**. Copy your own copy of the game to `ports/crusader/data`:
the installed files (STATIC, USECODE, SOUND, FLICS, CRUSADER.EXE...) or the CD `.iso`
(extracted on first launch). Supported: French 1.08 and US 1.23.

## Controls

| Input | Action |
|---|---|
| Left stick | Move / rotate |
| Right stick up / down | Aim |
| Right stick left / right | Roll left / right |
| RT | Fire |
| A / B / X / Y | Select / Jump / Confirm / Next weapon |
| LB / RB | Step left / right |
| L3 / R3 | Shift / Crouch or stand |
| D-pad | Use item / medkit / previous item / next item |
| Start | Menu |
| Hotkey + Start | Quit |

Buttons can be remapped in Options > Enhancements > Gamepad.

## Compiling

`python tools/portmaster/build.py` (Zig cross-compiler), see the repository.
