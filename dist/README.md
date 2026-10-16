# uaRO Pad — a gamepad for the uaRO client

A module for the legacy uaRO client (`uaRO.exe`): walking with the stick, hotkey bar slots right on the buttons, picking targets with the right stick. Keyboard and mouse keep working. It also improves the mouse: the cursor is not captured, follows the Windows cursor exactly, and the game window gets a few handy options.

Languages: English (this file), [українська](README.uk.md), [русский](README.ru.md).

## Install

1. Close the client.
2. Copy `dinput.dll` and `uaro-pad.ini` into the client folder, next to `uaRO.exe`. If a `dinput.dll` is already there (ROExt), replace it. The module takes over ROExt's mouse freedom and, as options, its borderless window and CPU saving; ROExt's other options (a fixed window size, the code page, Alt+F4) are not in the module.
3. Plug in an Xbox gamepad (or one Windows sees as XInput) or a PlayStation one (DualShock 4, DualSense) and start
   the client.

## Remove

Delete `dinput.dll`, `uaro-pad.ini` and `uaro-pad-binds.ini` from the client folder. The module never changes the client itself.

## Controls (version 0.5)

| Button | Action |
|---|---|
| Left stick | walk: the character goes where the stick points; let go to stop |
| Right stick | pick the monster in the direction of the stick: a "!" appears over it and the pad gives a short buzz |
| R3 (press the right stick) | switch the right stick between picking targets and turning the camera (two short buzzes: camera; one longer: targets) |
| L3 (press the left stick) | clear the target, stop the attack and the walk up to a monster |
| A | attack the nearest monster. A monster the pad is fighting right now, or one you picked with the right stick, stays the target. The attack goes on while the target lives and continues after skills. Hold A: once the target dies the next one is taken at once |
| X, Y, B | slots F1, F2, F3 |
| RB, RT | slots F4, F5 |
| LB | slot F6 — handy for a potion |
| D-pad ↑, →, ↓ | slots F7, F8, F9 |
| LT | pick up the nearest item (hold it to collect everything around one by one; what cannot be taken within a second, such as someone else's loot, is skipped) |
| D-pad ← | sit / stand |
| Back | switch the module off / on |
| Start | the settings menu (see below) |

A held slot button (and LT) repeats 4 times a second (`repeat_ms` in `uaro-pad.ini`).

The attack stops when you walk with the left stick, press L3 or click with the real mouse; press A again afterwards (or hold A: the attack then goes on by itself as soon as you stop).

A and the slot buttons pick the nearest monster at the moment you press them; a monster you fought earlier and walked away from does not count. The log tells what the pad saw: a line like `attack: nearest of 3 within reach: …; we are at (x,y)`. "Nearest" means nearest in a straight line, the way you see it on the screen, and no further than 15 cells — about the edge of the screen (the server shows the client monsters much further away). A target that gets more than 15 cells away (you walked off, or it ran) is dropped, so the next A goes for the nearest monster. A hunter shooting from 9 cells keeps the target.

Skills and items go onto the hotkey bar as usual, with the mouse. By default the buttons use the first row (F1–F9); the second and the third rows can be assigned in the Start menu. The module reads the bar from the client itself, nothing needs to be dragged again; what it saw is in the log, line `hotkey bar from the client`. The module casts a slot's skill on the target by itself — no need to pick the monster with the mouse as you would after pressing F1 on the keyboard.

### Where skills go

- **At an enemy** (Double Strafe, Blitz Beat…) — at the target; with no target, at the nearest monster, which becomes the target. If the monster is beyond the skill's range the character walks up — but not while you walk with the left stick: then hold the button and the skill fires as soon as the monster is in range.
- **On the ground** (Arrow Shower, Detect…) — on the target's cell or the nearest monster's; with no monsters around, under yourself.
- **Traps** — 2 cells from you towards the target (no monsters around: towards your last step). The server refuses a trap with anyone within 1 cell of it — a monster or you — so place it while the monster is more than 3 cells away.
- **Buffs and heals** — on yourself. Safety Wall, Pneuma, Warp Portal and Sanctuary — under yourself (the list is in `uaro-pad.ini`).

### A hunter's bar, for example

| Slot | Button | Skill |
|---|---|---|
| F1 | X | Double Strafe |
| F2 | Y | Arrow Shower |
| F3 | B | Blitz Beat |
| F4 | RB | Ankle Snare |
| F5 | RT | Claymore Trap |
| F6 | LB | a potion |
| F7 | D-pad ↑ | Improve Concentration |
| F8, F9 | D-pad →, ↓ | as you like: fly wings, a second potion |

### The camera on the right stick

R3 switches the right stick to the camera: left / right turn it the way the right mouse button does (freely outdoors, 20 degrees either way indoors, as the client allows), up / down zoom (up: closer). R3 again — the right stick picks targets again.

The module reads the camera's turn from the client. After the first turn, walk a few steps with the left stick: the module learns which way the client counts the turn (the log says `camera: follows the client's turn`), and from then on walking and the right stick follow any turn at once, also one made with the mouse.

## The Start menu

Start opens the menu "uaRO Pad — character name" over the game window. The game keeps running; the character stands while the menu is open. The menu has two tabs: **Buttons** and **Settings**.

- LB / RB — switch tabs (or click a tab).
- ↑↓ (or the left stick) — pick a row, ←→ — change it. Held, they repeat quickly.
- A or Start — save and close, B — close without saving, Y — the selected row back to its default. The last row of a tab with A resets the whole tab.
- With the mouse: a click picks a row, ◄ / ► change it, "Save" and "Cancel" are at the bottom. A click on the last row resets the tab at once ("Cancel" brings it back).

### Buttons

A button can do: attack, any hotkey bar slot — F1–F9 of all three rows (you can see what is in the slot), pick up items, sit / stand, clear the target, switch the right stick (camera / target), or nothing. Start, Back and the sticks cannot be reassigned.

Layouts saved before 0.5 are moved by themselves: R3 switches the right stick to the camera there too, and an unused L3 clears the target.

Each character has a layout of its own: the module recognises the character when you enter the game. A new character starts with the default layout; if you open the menu before entering a map, that default is what you edit. Layouts are kept in `uaro-pad-binds.ini` in the client folder; the module writes it.

### Settings

| Setting | What it does | Key in `uaro-pad.ini` |
|---|---|---|
| Language | English, Українська, Русский. The menu changes at once | `[general] language` |
| Keep the cursor in the window | while the game is active the cursor cannot leave its window; Alt+Tab, the Win key or minimizing let it go | `[mouse] lock` |
| Click that activates the window | the click you return to the game with only activates the window (default), or also acts in the game | `[mouse] focus_click` |
| Middle mouse button, mouse buttons 4 and 5 | press F1–F9 of the hotkey bar; the skill is aimed with the mouse, as from the keyboard | `[mouse] middle`, `button4`, `button5` |
| Borderless window | no title bar and borders; position in `[window] position` | `[window] borderless` |
| Save CPU in the background | while the game is behind other windows it does not keep a whole CPU core busy | `[window] background_sleep_ms` |
| Vibration | the gamepad's rumble | `[rumble] enabled` |
| Walk radius | where the left stick "clicks" to walk: a share of the window's shorter side (10–44%) | `[stick] walk_radius` |
| Target mark | how the target shows for as long as it is the target (only you see it): poison's tint (the default), deadly poison, curse (red), stun (stars), sleep (zzz), silence, Sight's fireball or Ruwach's light — states the client draws, some with their own short sound; or "!" once when it is chosen; or nothing | `[aim] mark` |

Settings work right after "Save" and are written into `uaro-pad.ini` — only the changed keys; comments and everything else in the file stay as they were.

## Mouse

The client normally captures the mouse; the module leaves it free (Mouse Freedom), and the game cursor follows the Windows cursor:

- With `uaRO.exe` the module reads where the game's own cursor is, so the two cursors match — also when the picture is stretched over a bigger window or screen. If the picture keeps black bars at the sides (dgVoodoo with `KeepWindowAspectRatio`), the cursor can be off; send the log.
- Clicks and the wheel reach the game only over its window. Scrolling a browser on another monitor no longer zooms the camera, and a click on the title bar no longer sends the character walking. A drag started in the game goes on until you let go, wherever the cursor is.
- Over the title bar and the borders you see the normal Windows cursors (the client used to hide them).

## Window

- **Borderless window** — the game picture without a title bar and borders. `position = center` puts it in the middle of its monitor (a picture as large as the monitor covers it all), `keep` leaves it where it was, `X,Y` puts its top-left corner there. The game's resolution is set in the client's own setup, as before.
- **Save CPU in the background** — the client spins at full speed even when you are in another window; now each frame sleeps 10 ms while it is in the background (`background_sleep_ms`). The game keeps receiving everything from the server.

## Settings file

`uaro-pad.ini` explains every key. The menu changes the keys of the Settings tab by itself; after editing the file by hand, restart the client.

## If something goes wrong

- No cursor while the game window is active: check `freedom = 1` in `uaro-pad.ini`.
- The game cursor lags behind the Windows cursor or drifts: send `uaro-pad.log` — the `cursor:` lines show where they part (`exact` means the module read the game's own cursor).
- Clicks do not reach the game at all: send `uaro-pad.log`; until it is fixed, version 0.3 works the old way.
- No mark on the chosen monster: the log should have the line `target marks reach the client`. Some marks may not show on monsters in this client — try another one in the menu. If the client crashes or disconnects after a target is picked, set `mark = off` in `[aim]` and send the log.
- The right stick picks a monster in the wrong direction: walk with the left stick in different directions (see above); if that does not help, send the log.
- A held slot repeats too often or should not repeat: `repeat_ms` in `[skills]` (0: no repeat; also for LT).
- The log is `uaro-pad.log` in the client folder, rewritten at every start. A detailed log: `log_level = debug`.
- The character attacks or talks to an NPC instead of stepping: try `press = center` in `[stick]`.
- A slot button does nothing: pressing it puts a `denied: …` line with the reason into the log (empty slot, no such skill learned, item used up); the line `hotkey bar from the client` shows what the module sees on the bar.
- The module does nothing: check that the client window is active and the module was not switched off with Back.
- An antivirus removed or blocked `dinput.dll`: add the client folder to its exceptions.

## Limitations of version 0.5

- The hotkey bar and the exact cursor are read from the memory of `uaRO.exe`. With another client build the log says `hotkey bar: unknown client build` (slot buttons do not work) and `game cursor: unknown client build` (the cursor position is estimated, as in 0.3).
- Equipment on the bar (quick weapon swap) does not work from the gamepad.
- Buffs and heals — only on yourself.
- An NPC with a monster sprite can become a target: when none of your hits land on it for 4 seconds, the module drops it.
- Menus, NPC dialogues and chat — with the mouse and keyboard.
- Mouse buttons 4 and 5 press only F1–F9 (not rows 2–3 or gamepad actions).
- The camera on the right stick works with `uaRO.exe` only (the log says `camera: turned in the client's memory`); with another build the right stick only zooms.
- Zooming goes through the mouse wheel: with the cursor over a game window (inventory, chat) the client scrolls that window instead.
