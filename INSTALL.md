# Installing Ragnarok Gamepad

Languages: English (this file) · [Українська](INSTALL.uk.md) · [Русский](INSTALL.ru.md)

Five minutes, no installer: the module is one file, `dinput.dll`, that sits next to the game. The client itself
is never changed — delete the file and the game is exactly as it was.

## What you need

- **The uaRO client** installed (the folder with `uaRO.exe`).
- **Windows** 7 or newer.
- **A controller Windows sees as an Xbox controller (XInput):** Xbox 360, Xbox One, Xbox Series, or a
  compatible one. Wired, through the Xbox wireless adapter or Bluetooth — all work. Other controllers
  (PlayStation, Switch) work through DS4Windows or a similar tool that makes them look like an Xbox controller
  (Steam Input too, with the game added to Steam as a non-Steam game).

## 1. Download

**[uaro-pad-0.5.1.zip](https://github.com/kwebmn/ragnarok-gamepad/raw/main/release/uaro-pad-0.5.1.zip)** —
everything for a first install: `dinput.dll`, `uaro-pad.ini` and the player's guide in three languages.

Optional check, in PowerShell: `Get-FileHash uaro-pad-0.5.1.zip` must print
`A1EC426CF61652687B3E97B1B19E112AF8A50C164132EBDEB2E72E6DE4F2928B`.

## 2. Find the client folder

The folder with `uaRO.exe`, for example `C:\Games\uaRO\uaRO`. If you start the game from a desktop shortcut:
right-click the shortcut → **Open file location**.

Close the game before going on.

## 3. Put ROExt aside (if it is there)

If the client folder already has a `dinput.dll`, it is ROExt. Rename it to `dinput.dll.bak` — the module replaces
it. The module has its own mouse freedom, borderless window and CPU saving; ROExt's other options (a fixed window
size, the code page, Alt+F4) are not in it.

## 4. Copy the files

Unpack the zip and copy into the client folder, next to `uaRO.exe`:

- `dinput.dll` — the module;
- `uaro-pad.ini` — its settings (every key is explained in the file).

The `README` files are the player's guide; keep them wherever you like.

## 5. Unblock the file (only if Windows asks)

Files downloaded from the internet are sometimes marked by Windows. Right-click `dinput.dll` → **Properties**.
If there is an **Unblock** box at the bottom, tick it and press **OK**.

## 6. Plug in the controller and start the game

Connect the controller, then start `uaRO.exe` as usual and log in.

## 7. Check that it works

- A file `uaro-pad.log` appears in the client folder; its first line reads `uaRO Pad 0.5.1`.
- In the game, press **Start** on the controller: the menu opens over the game. In the **Settings** tab choose
  your language (LB / RB switch tabs, ←→ change a value, A saves).
- Push the left stick: the character walks. Press A next to a monster: it attacks.

The controls and every setting are in the player's guide:
[English](dist/README.md) · [Українська](dist/README.uk.md) · [Русский](dist/README.ru.md).

## Updating

1. Close the game.
2. Replace `dinput.dll` in the client folder with the new one:
   **[dinput.dll](https://github.com/kwebmn/ragnarok-gamepad/raw/main/release/dinput.dll)** (the latest version).
3. Keep your `uaro-pad.ini` and `uaro-pad-binds.ini`: they hold your settings and button layouts. Settings a new
   version brings in work with their defaults; the menu writes them into your file when you save.

## Removing

Delete `dinput.dll`, `uaro-pad.ini`, `uaro-pad-binds.ini` and `uaro-pad.log` from the client folder. If you
renamed ROExt in step 3, rename `dinput.dll.bak` back to `dinput.dll`.

## If something goes wrong

| What you see | What to do |
|---|---|
| No `uaro-pad.log` in the client folder | The module did not load: check the file is called exactly `dinput.dll`, sits next to `uaRO.exe`, is unblocked (step 5) and was not removed by the antivirus. |
| The antivirus removed or blocked `dinput.dll` | Add the client folder to the antivirus exceptions. The module hooks the game's input, which some antiviruses dislike; its source code is in this repository. |
| The log is there, but the pad does nothing | The game window must be active; Back switches the module off and on. Check that Windows sees the controller: Win+R → `joy.cpl`. The log line `XInput not found` means no XInput support in the system. |
| No cursor while the game window is active | `freedom = 1` must stay in the `[mouse]` section of `uaro-pad.ini`. |
| Something else | Send `uaro-pad.log` from the client folder: it says what the module saw and decided. |

More answers are in the player's guide, section "If something goes wrong".
