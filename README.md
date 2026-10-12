# Ragnarok Gamepad

Gamepad (Xbox / XInput) support for the classic Ragnarok Online client — a drop-in `dinput.dll` made for the
[uaRO](https://uaro.kiev.ua) client (`uaRO.exe`, a 2007 Sakexe, packet version 22) and its uAthena server.
Keyboard and mouse keep working; the pad plays alongside them.

**Players:** the guide is in [dist/README.md](dist/README.md) (English), [dist/README.uk.md](dist/README.uk.md)
(українська) and [dist/README.ru.md](dist/README.ru.md) (русский).

## What it does

- **Walking** with the left stick; let go and the character stops on the spot.
- **Hotkey bar on the buttons:** F1–F9 of all three rows, read from the client's own bar. Skills are aimed by the
  module: at the target, on the ground, traps two cells towards the target, buffs on yourself.
- **Targets:** A attacks the nearest monster in reach; the right stick picks one in its direction; hold A to go
  on to the next one. A mark the client draws (a poison tint by default) shows the target to you only.
- **Camera on the right stick:** R3 switches it between picking targets and turning / zooming the camera.
- **Start menu** over the game: each character's button layout and the settings, in English, Ukrainian or
  Russian.
- **Mouse freedom:** the mouse is never captured and the game cursor follows the Windows cursor exactly; clicks
  and the wheel reach the game only over its window; side mouse buttons can press F1–F9.
- **Window:** optional borderless window and cursor lock; the client sleeps while it is in the background.

## How it works

The proxy `dinput.dll` loads the system one and wraps the mouse device. It reads the gamepad through XInput,
follows the map server's packets to know the monsters around (uAthena, `PACKETVER` 7), and sends the same
requests the client would (walk, attack, skills, pick up). A few things the client keeps to itself — the hotkey
bar, the game cursor, the camera — are read or set in its memory, only after the code at those addresses has been
checked byte for byte; another client build simply goes without them.

## Building

On Linux with CMake and MinGW-w64 (i686):

```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win            # build-win/dinput.dll
tools/package.sh                   # dist/out/uaro-pad-<version>.zip: the DLL, uaro-pad.ini and the guides
```

The portable core (protocol, world, controller, menu, settings) has unit tests that run on the host:

```sh
cmake -S . -B build-host && cmake --build build-host && build-host/uapad_tests
```

Layout: `src/core` — no Windows headers, tested; `src/win` — the DLL glue (hooks, DirectInput wrappers, XInput,
the menu window, client memory); `tools` — packet table generator, DLL check, packaging; `docs` — design notes.

## License

MIT, see [LICENSE](LICENSE).
