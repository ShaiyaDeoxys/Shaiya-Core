# Client Module

This module contains `Game.exe` hooks and client-side quality-of-life patches.

## Environment

- Windows 10 or newer
- Visual Studio 2022
- C++23
- Microsoft DirectX SDK (June 2010)
- x86 build target
- Optional runtime `D3DX9_43.dll` beside `Game.exe` or installed system-wide only for the `/font` command (D3DX font picker). All other texture loading (PNG, DDS) uses built-in decoders and does not require D3DX.

## Build

```powershell
..\build-client.cmd
```

The root `build-client.cmd` and `build-client.ps1` wrappers build `sdev-client` as Release|x86. The PowerShell wrapper also accepts `-Configuration Debug|Release` and `-Target Build|Rebuild|Clean`.

## CONFIG.INI

The client reads several user-facing options from `CONFIG.INI`.

```ini
[ADVANCED]
; 1 skips the updater token check and lets Game.exe launch directly.
; 0 keeps the stock updater-required behavior.
SKIPUPDATER=0

; 1 auto-selects the only available server.
; 0 keeps the stock server-selection screen.
SKIPSERVERSELECTION=1

; 1 skips character mode selection and forces Ultimate Mode.
; 0 keeps the stock mode-selection screen.
SKIPMODESELECTION=1

; Visual title rendering from cloak data.
TITLES=ON

; Visual colored-name rendering from helmet data.
COLOUR=ON

; Optional visual/performance toggles controlled by chat commands and the ImGui settings panel.
COSTUMES=TRUE
PETS=TRUE
WINGS=TRUE
EFFECTS=TRUE
FPS_BOOST=FALSE

; 1 enables the ImGui overlay layer.
; 0 disables ImGui overlay rendering and its interactive helpers.
IMGUIOVERLAY=1

; Custom native-anchored chat replacement.
; 1 hides stock chat text and renders the Core chat at the native chat coordinates.
; 0 disables the replacement and lets the stock chat appear normally.
CUSTOMCHAT=1

; Master toggle for visual chat tokens.
; The picker also exposes runtime ON/OFF controls for emojis and GIFs.
EMOJIS=1

[FONT]
; Written by /font. Defaults to Arial 13 normal.
HEIGHT=13
WEIGHT=400
ITALIC=0
FACENAME=Arial

[IMGUI]
; Runtime ImGui overlay state. Most values are written by dragging buttons/panels in game.
ID_VIEW_ENABLED=TRUE
REWARD_AUTOCLAIM=FALSE
```

## Commands

- `/font` opens the Windows font picker and persists the selected in-game font. It uses the 32-bit `D3DX9_43.dll` runtime when available to `Game.exe`.
- `/effects on` and `/effects off` toggle player and mob effect rendering.
- `/pets on` and `/pets off` toggle pet rendering. This also controls mob effects in the current hook.
- `/wings on` and `/wings off` toggle wing rendering.
- `/costumes on` and `/costumes off` toggle costume rendering.
- `/fpsboost on` and `/fpsboost off` toggle the client FPS boost path.
- `/titles on` and `/titles off` toggle visual item titles at runtime.
- `/colour on` and `/colour off` toggle visual colored names at runtime.
- `/color on` and `/color off` are accepted aliases for `/colour`.

## Client Features

Repository scope reminder: `sdev` is the game server module, and `sdev-client` is the client patch module.

This section is the client-side feature map. Every entry is installed from `Main()` in `src/main.cpp`.

### Internal Client Infrastructure

- **Internal archive reader**: `src/game_data_archive.cpp` centralizes `data.sah` scanning and `data.saf` byte reads for client-side assets. Feature modules still own their file naming and folder rules, but they no longer duplicate archive parsing.
- **Native address map**: `src/include/game_addresses.h` groups shared Game.exe addresses used by Unicode, text hooks, and visual chat tokens.
- **Custom chat module**: `src/custom_chat.cpp` owns the native-anchored Core chat replacement. The GM debug panel only exposes tuning controls through `Chat options`.

### Startup, Login, And Windowing

- **Updater bypass**: `ADVANCED/SKIPUPDATER=1` injects the same command-line token normally provided by `Updater.exe`, allowing direct `Game.exe` launch without changing the normal startup state machine.
- **Login server override**: `ADVANCED/IP=` rewrites the stock login server string. Empty or missing values keep the default `127.0.0.1`.
- **Login splash skip**: removes the Nexon/copyright splash and shortens the startup waits while preserving required login resource initialization.
- **Single-server skip**: `ADVANCED/SKIPSERVERSELECTION=1` hides the server panel and selects the only available server through the safe delayed stock path.
- **Unicode main HWND**: upgrades the GAME window to a Unicode window and keeps the visible title as `Shaiya`, preventing the legacy title truncation issue.
- **UTF-8 chat input**: accepts composed Unicode/IME text, stores UTF-8 bytes in the stock textbox, fixes multibyte wrapping/rendering branches, and removes the forced byte-127 send terminator.
- **SData string UTF-8 support**: after the native data loaders finish, normalizes legacy high-byte SData strings to UTF-8 for visible item, skill, NPC skill, cash product, monster, and quest text. Already-valid UTF-8 is left untouched.
- **Client UI dispatch**: subclasses the game window so ImGui helpers can safely post roulette requests and chat-token inserts back through the game UI thread.
- **Welcome message**: posts the existing `SysMsg` welcome entry after the client UI is ready. The message text remains owned by the normal `sysmsg.txt` data.
- **Visual chat tokens**: draws an ImGui emoji button anchored to the native lower chat controls during gameplay, so it follows resolution changes and native chat resizing. The picker opens relative to that button, scans `emojiN.png` entries from `Assets/Emojis` and `gifN.gif` entries from `Assets/Gifs` in `data.sah/saf`, then inserts plain chat tokens such as `:emoji1:` or `:gif2:` into the stock textbox through the game UI thread. The picker exposes separate ON/OFF toggles for emojis and GIFs; disabled token families are hidden from native text without drawing an overlay. GIF picker entries use lightweight static previews with a bounded resident cache, while full animation is loaded only when a GIF is rendered in chat/floating text. Packets and server handling remain plain text.

### Character Creation And Selection

- **Mode selection skip**: `ADVANCED/SKIPMODESELECTION=1` bypasses the mode-selection UI and forces Ultimate Mode during character creation.
- **Name gate bypass**: skips local ASCII-only name format checks, legacy blocked-substring checks, the local "name already verified" gate, and the UI availability request. Real validation must be enforced server/dbAgent side.
- **Busy-as-success fallback**: treats one legacy character-creation busy result path as success for compatibility with UTF-8/special-name creation flows.
- **Character select adjustments**: patches select-screen texture/text positions used by the current interface pack.
- **Extended character allocation**: grows `CCharacter` allocation from `0x43C` to `0x444`, initializes custom members, and resets them on character reset.

### Interface And Assets

- **Clock text format**: overrides the map clock text format.
- **Background render arguments**: adjusts startup/login background draw arguments used by the current UI setup.
- **Level-up message suppression**: keeps the stock level-up texture creation flow, but forces the render size to zero so the splash is hidden.
- **GM H-key HP viewer removal**: disables the vanilla redundant GM HP viewer opened by `H`; the custom target viewer remains available.
- **Stats window color patch**: adjusts patched stats-window colors to fit the current interface.
- **Dungeon map visibility**: allows dungeon maps to be shown by the client.
- **PvP rank icon alignment**: adjusts UI image coordinates so PvP rank icons render correctly in the current interface layout.

### Battleground Button

- Draws `main_stats_pvp_button.png` inside the main stats UI without rewriting the whole `CStatusMiniBar` layout.
- Reads `BattleFieldMoveInfo_Client.ini` from the client root or `Data` folder to display the correct battlefield name for the player level.
- Sends packet `0x233` only after the click starts and ends inside the button, avoiding accidental activation during window maximize/minimize.
- Uses a native confirmation dialog before sending the move request.

### Target, Names, Titles, And Text

- **Target HP viewer**: draws current/max HP inside the native target frame for monsters and users, using the configured game font.
- **Item titles**: renders visual titles from cloak data and can be toggled with `TITLES` or `/titles`.
- **Name colors**: renders colored names from helmet data, including rainbow color mode, and can be toggled with `COLOUR` or `/colour`.
- **Font picker**: `/font` updates the GDI font and all known D3DX camera font slots so labels, counters, chat-adjacent overlays, and native text helpers stay consistent.
- **Chat balloon height**: increases chat balloon height from `1.5` to `1.75` for better title/name layouts.
- **Mob/player effect toggles**: command hooks can hide player effects, mob effects, costumes, pets, wings, and related visual objects.

### Items, Equipment, And Packets

- **Custom recreation runes in blacksmith**: allows item effects `220..240` to be placed in the NPC recreation window.
- **Buy/sell quantity limit**: raises the client-side quantity cap from `10` to `255`.
- **Disguise removal fix**: stabilizes the `0x303` packet handler path.
- **Appearance/sex change fix**: stabilizes the `0x226` packet handler path.
- **System message 509 support**: handles the `0x229` message path.
- **Javelin attack fix**: adjusts the `0x502` handler stack/argument layout.
- **Item icon quantities**: draws inventory and quickslot item counts while skipping lapis/firework-style items that should not show counts.
- **Elemental icon overlay**: draws a small element badge (fire, water, earth, wind) in the bottom-right corner of eligible item icons in both inventory and quickslot bars. Detection checks inserted lapis gems first, then falls back to the base item attribute. Textures are indexed through `data.sah` and loaded once from `Assets/General/{fire,water,earth,wind}.png` inside `data.saf`.
- **Two-hand/off-hand logic**: fixes `CPlayerData::IsTwoHandWeapon` behavior so custom off-hand support can coexist with one-hand weapons.
- **Weapon step display**: patches the client weapon-step path used by lapisian/enchant display.
- **Item description augmentation**: a background thread runs once after item data is loaded and appends OJ reroll info (maximum OJs and highest OJ value, or "cannot be rerolled") and grade value (admin-only) to each item's description pointer in-place. Zero per-frame overhead.
- **Remote NPC panel**: `NpcIcon.png` from `Assets/General` opens a compact ImGui panel for Market, Blacksmith, RR Blacksmith, Vet Manager, Bank, and Guild Manager. The button and panel positions persist in the ImGui settings.
- **Vehicle packet/display support**: supports vehicle-related EP6.4 shape/list packet fields and client display paths where implemented.

### Quick Slots And Input

- Adds the third quick-slot bar allocation/free/save behavior and routes item, skill, and basic-action movement between quick-slot bags.
- Patches quick-slot plus buttons and interaction paths so the extended bars can be opened and used.
- Removes selected client-side quick-slot delays: skill delay, basic action delay, and the extra `+1000ms` delay gate. The revolver delay patch is intentionally left disabled in code for release stability.
- Holding left click while assigning status points applies `x10` when enough points are available; otherwise it falls back to `+1`.

### Buffs And Movement

- **Movable buff row**: hold `F6` plus left click to save the current buff position to `CONFIG.ini` under `[BUFF] LOCATION_X/LOCATION_Y`.
- **Persistent buff layout**: detours the buff X/Y instructions so the saved location is used every render.
- **Buff spacing/count**: adjusts row spacing and count per row to match the current interface.
- **Fast transition**: shortens selected transition waits to `100ms`.

### Visual Timers And Flow

- **Leader resurrection timer**: aligns the client popup countdown with the server-side 5 second timer.
- **UM chars can ress leader**: routes the Grow 3 death dialog through the same leader-resurrection window used by Grow 2. The server still authorizes the actual resurrection.
- **Logout/game-over timer**: aligns the visible logout countdown with the shortened server logout delay of 2000ms.
- **Experience view fix**: prevents the client from displaying experience values multiplied by ten and ignores locale-specific EXP multiplication where applicable.

### Resolution And Graphics

- Adds `1366x768`, `1400x900`, `2560x1080`, `2560x1440`, `3840x1080`, `3840x2160`, and `3440x1440`.
- Expands option-screen resolution rendering, selection, saving, and apply behavior.
- Reuses stock large-layout positioning branches so UI placement remains stable at wider resolutions.
- Caches gamma/distance/float option values to prevent the expanded resolution table from corrupting option sliders.
- Applies camera limit from the global `g_cameraLimit` value.
- Applies costume, pet, wing, and dungeon shadow/lag workarounds used by the current visual setup.

### Discord And ImGui Overlay

- Discord RPC initializes with the static application id/message defined in `src/discord.cpp`.
- The roulette panel is opened from `RouletteIcon.png`. It is visible to all players, requests the server reward list, displays real item names/icons from the configured server rewards, and sends the server roulette roll packet. Hovering a reward on the wheel shows the item name and description tooltip.
- The settings button opens a compact `Quick Settings` panel for visual toggles such as wings, pets, costumes, titles, colours, FPS boost, and effects. The button and panel positions persist in `CONFIG.INI`.
- `F9` toggles the minimal GM debug panel (requires `IsAdmin`). It keeps the ID View toggle and the `Chat options` controls for custom chat wrap/color tuning.
- The Core chat replacement is enabled by default through `ADVANCED/CUSTOMCHAT=1`. It hides stock chat text, records the same incoming chat stream, and renders upper/lower messages at the native CChat coordinates so resolution changes, native vertical resize, and native scroll state remain aligned. Set `CUSTOMCHAT=0` to return to the stock chat renderer.
- The welcome system message is a lifecycle behavior rather than a user-controlled panel feature.
- The emoji/GIF picker is an in-world chat helper, not a panel module. Its ON/OFF controls live inside the picker.
- DirectInput mouse hook suppresses game click-to-move input when ImGui owns the cursor, preventing character movement behind panels.
- Mouse coordinates are scaled from window-client space to DX9 backbuffer space each frame so ImGui input stays accurate at all resolutions.

## Asset Notes

- Interface assets must exist in the client `Data/interface` tree in their native format (.tga/.jpg).
- Raid button assets are expected as PNG.
- Battleground uses `main_stats_pvp_button.png`.
- Visual chat token assets are read from the internal data archive: `Assets/Emojis/emojiN.png` and `Assets/Gifs/gifN.gif`.
- Roulette background is read from `Assets/General/Roulette.png` inside `data.sah/saf`.
- Roulette item icons read the native DDS atlases from `Data/interface/icon` inside `data.sah/saf`, including special atlases such as `icon_somo3.dds`. The SAH index is parsed once and DDS textures (DXT1/DXT3/DXT5) are decoded by a built-in parser, so the ImGui panel follows the client's data without embedding atlas copies in `sdev-client.dll`.
- Remote NPC, settings, reward, and roulette buttons load their PNGs from `Assets/General` inside `data.sah/saf`.
- Visual title images are read from `Assets/Titles/titleN.png` (static) and `Assets/TitlesAnimated/titleN.gif` (animated) inside `data.sah/saf`.
- Elemental icon badges are read from `Assets/General/{fire,water,earth,wind}.png` inside `data.sah/saf`. The SAH index is parsed once to locate the files and each PNG is decoded by stb_image into a D3D9 managed texture.
- Internal archive access is centralized in `src/game_data_archive.cpp`; features still decide which folders/files they need, but they share the same `data.sah` scanner and `data.saf` reader.
- Custom recreation rune UI acceptance is only client-side placement. Server behavior is implemented in `sdev`.
