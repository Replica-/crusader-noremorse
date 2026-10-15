# Crusader: No Remorse — SDL port

A C++20 / SDL2 reimplementation of the *Crusader: No Remorse* (Origin, 1995) engine, for Windows and Linux. The project is **work in progress**: the game starts, you can walk, fight, and hear sounds, music and cutscenes, but the first mission cannot yet be played from start to finish.

This repository contains **no game files**: you need your own copy of the original game. The target is **at first** the **French version** (`CRUSADER.EXE` dated 12/10/1995).

## How the engine was rebuilt

1. **Decompilation**: the DOS executable (Phar Lap extender) and the audio DLL `ASYLUM.DLL` are analysed with Ghidra to get pseudo-C and a disassembly of every function. The original modules (about 90) are identified by segment, and functions are named from library symbols, the intrinsics known from ScummVM, and reading the code.
2. **Rewrite**: each function is rewritten in modern C++, with no DOS dependency: hardware access (VGA, keyboard, mouse, Sound Blaster) goes through a thin SDL2 layer. Every function carries an `@orig` tag naming the original routine it replaces.
3. **Verification**: file formats are tested against all the game data, and differential checks run the original routine in an x86 emulator to compare it with the C++ version on the same inputs.

The game scripts ("usecode", 402 classes) are not rewritten: an interpreter runs them as they are, and the engine provides the native functions ("intrinsics") they call.

## Running the game

### 1. Download

Get the archive of the latest version from the [Releases](../../releases) page:

- Windows: `crusader-X.Y.Z-windows-x64.zip` (SDL2 is built into the executable)
- Linux: `crusader-X.Y.Z-linux-x64.tar.gz` — requires SDL2: `sudo apt install libsdl2-2.0-0` (or your distribution's equivalent)
- aarch64 handhelds (PortMaster: muOS, Knulli, ROCKNIX, ArkOS…): `crusader-X.Y.Z-portmaster-aarch64.zip` — see [`tools/portmaster/README.md`](tools/portmaster/README.md)

Checksums are in `SHA256SUMS`.

### 2. Copy the game data

Copy the **whole contents of the game's install folder** (or of the CD) into a `data` folder next to the executable:

```
crusader(.exe)
data/
├── CRUSADER.CFG        settings
├── STATIC/             shapes, palettes, fonts, maps, tables  (SHAPES.FLX, FIXED.DAT, GLOB.FLX, TYPEFLAG.DAT, ANIM.DAT, GUMPS.FLX, FONTS.FLX, *.PAL, …)
├── USECODE/            game scripts  (EUSECODE.FLX, UNKCOFF.DAT, UNKDS.DAT)
├── SOUND/              sound effects and music  (SOUND.FLX, M01.AMF … M16C.AMF, MENU.AMF, CRED.AMF)
├── FLICS/              cutscenes  (*.AVI and their subtitles *.TXT)
└── SAVEGAME/           saved games (optional for a new game)
```

Notes:

- The original executables (`CRUSADER.EXE`, `ASYLUM.DLL`…) are not used, but copying them is harmless.
- Only the **French version** is supported. The US version (v1.23) has different files (`EUSECODE.FLX`, `UNKCOFF.DAT`) and will not work.
- File names are case-sensitive on Linux: keep the original **upper-case** names (`STATIC/SHAPES.FLX`, etc.).
- `FLICS/` is only needed for cutscenes; everything else is required.

### 3. Launch

```sh
./crusader data --new-game        # Windows: crusader.exe data --new-game
./crusader data --save 1          # resumes SAVEGAME.1 from the data folder
./crusader --version
```

`data` is the data folder (and the default value). Escape opens the game menu (resume, load, options, credits, quit).

Main controls (those of the original): arrow keys or numeric keypad to move (Shift: walk/run, Ctrl and Alt: variants), Space or Insert to fire, Q/W previous/next weapon, I/O previous/next item, U use, M medkit, Tab or J jump, S/G selection mode, R target designation, F1 help, F2 options. The mouse works in game and in menus.

## Building from source

Requirements: CMake ≥ 3.21 and a C++20 compiler (Visual Studio 2022, GCC 11+ or Clang 14+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # Windows: -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

SDL2 is taken from the system if installed (`libsdl2-dev` on Linux), otherwise it is downloaded and built.

The tests in `tests/` need no game data. The end-to-end tests in `e2e/` run on the original game data and are registered only when it is found (`-DCRUSADER_DATA_DIR=…`, `data/` by default); those that load saved games read them from `-DCRUSADER_TEST_SAVES_DIR=…` and skip themselves when it is absent.

Besides `crusader`, the build produces a few tools: `shapeview` (shape viewer), `mapview` (map viewer), `ucsmoke` and `useprobe` (script execution), `diffcheck` (comparison with the original).

## Versioning

Versions follow [SemVer](https://semver.org) and are computed automatically from [Conventional Commits](https://www.conventionalcommits.org): `fix:` → patch, `feat:` → minor, `BREAKING CHANGE` → major (minor while on `0.x`). Every published version is built and tested on Linux and Windows, and cross-built for PortMaster. Details in [`docs/RELEASING.md`](docs/RELEASING.md).

## To do

- **First mission playable from start to finish**: using scenery objects (doors, switches, elevators), computers and keypads, surveillance cameras and controllable robots, theft and related alarms, on-screen messages, and the last native functions the scripts call. A real playthrough will settle the remaining differences.
- **Saving and loading** compatible with original saved games (there is no save screen yet).
- **Later missions**: only mission 1 is used as a reference; the other 15 rely on the same mechanisms but have not been reviewed.
- **Fidelity**: the original random number generator, gravity behaviour (bounces, slides), draw order on a few maps, restoring saved processes, systematic comparison with the original under DOSBox.
- **Comfort**: start-up menu, resolution choice (beyond 640×480), gamepad, language option (US version), other game revisions.
- **Decompilation**: name the ~1,800 functions still anonymous (most are Borland library and Phar Lap code, to be replaced by the standard library).
- **Out of scope for now**: the original's cheats and debug commands.

## License and rights

The port's code and its documentation are released under the [GNU GPL v3](LICENSE) or any later version, at your option: you may use, study, modify and redistribute them, provided that any redistributed version (modified or not) stays under the same license and comes with its source code.

This license covers this repository only. *Crusader: No Remorse* remains the property of its rights holders: this project distributes none of its code, data or screenshots. You need to own the game to play it.
