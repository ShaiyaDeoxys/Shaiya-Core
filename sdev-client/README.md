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

; Interface folder to load. Determines which UI assets and layout the client uses.
;   EP4 (default) — loads data/interface + applies EP4 layout patches
;   EP6           — loads data/Intf_epi6 (no layout patches)
;   EP7           — loads data/Intf_epi7 (no layout patches)
UI=EP4

; Custom window title. Defaults to "Shaiya" when omitted.
; When a character is in-game the title becomes "WINDOWTITLE - Playing as CharName".
WINDOWTITLE=Shaiya

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

[FONT]
; Written by /font. Defaults to Arial 13 normal.
HEIGHT=13
WEIGHT=400
ITALIC=0
FACENAME=Arial

[SOUND]
; Sound levels use 0 as off and 1..6 as increasing volume.
VOL_BGM=6
VOL_EFFECT=6
VOL_WORLD=6
VOICE_CHAR=TRUE

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
- `/mute <PlayerName>` permanently blocks all messages from that player in the Core chat.
- `/unmute <PlayerName>` removes a player from the mute list.

## Client Features

Repository scope reminder: `sdev` is the game server module, and `sdev-client` is the client patch module.

This section is the client-side feature map. Every entry is installed from `Main()` in `src/main.cpp`.

### Internal Client Infrastructure

- **Internal archive reader**: `src/game_data_archive.cpp` centralizes `data.sah` scanning and `data.saf` byte reads for client-side assets. Feature modules still own their file naming and folder rules, but they no longer duplicate archive parsing.
- **Native address map**: `src/include/game_addresses.h` groups shared Game.exe addresses used by text hooks and visual chat tokens.
- **Custom chat module**: `src/custom_chat.cpp` owns the native-anchored Core chat replacement. The GM debug panel only exposes tuning controls through `Chat options`.

### Startup, Login, And Windowing

- **Updater bypass**: `ADVANCED/SKIPUPDATER=1` injects the same command-line token normally provided by `Updater.exe`, allowing direct `Game.exe` launch without changing the normal startup state machine.
- **Login splash skip**: removes the Nexon/copyright splash and shortens the startup waits while preserving required login resource initialization.
- **Client UI dispatch**: subclasses the game window so ImGui helpers can safely post roulette requests and chat-token inserts back through the game UI thread.
- **Welcome message**: posts the existing `SysMsg` welcome entry after the client UI is ready. The message text remains owned by the normal `sysmsg.txt` data.
- **Visual chat tokens**: draws an ImGui emoji button anchored to the native lower chat controls during gameplay, so it follows resolution changes and native chat resizing. The picker opens relative to that button, scans `emojiN.png` entries from `Assets/Emojis` and `gifN.gif` entries from `Assets/Gifs` in `data.sah/saf`, then inserts plain chat tokens such as `:emoji1:` or `:gif2:` into the stock textbox through the game UI thread. The picker exposes ON/OFF toggles for emojis and GIFs. GIF picker entries use lightweight static previews with a bounded resident cache, while full animation is loaded only when a GIF is rendered in chat/floating text. Packets and server handling remain plain text.

### Character Creation And Selection

- **Name gate bypass**: skips local ASCII-only name format checks, legacy blocked-substring checks, the local "name already verified" gate, and the UI availability request. Real validation must be enforced server/dbAgent side.
- **Busy-as-success fallback**: treats one legacy character-creation busy result path as success for compatibility with UTF-8/special-name creation flows.
- **Character select adjustments**: patches select-screen texture/text positions (applied in all UI modes).
- **Extended character allocation**: grows `CCharacter` allocation from `0x43C` to `0x444`, initializes custom members, and resets them on character reset.

### Interface Folder Selection

- **UI mode** (`ADVANCED/UI`): selects which interface asset folder the client loads from `data.sah/saf`. `EP4` (default) loads `data/interface` and applies the full set of EP4 HUD layout patches (stats bars, map, bottom bar, clock position, arrows, loadbar, stats colors, PvP icons). `EP6` loads `data/Intf_epi6` and `EP7` loads `data/Intf_epi7` — both skip layout patches since those folders contain their own complete UI. The setting is read once at startup from `CONFIG.INI`. All texture loading paths (both the `CTexture::CreateFromFile` hook and the `D3DXCreateTextureFromFileEx` hook) rewrite `data/interface` references to the active folder.
- **Shared patches** (all UI modes): clock text format, clock X offset (adjusted per mode to fit the date string), character select screen layout, and target HP viewer position (Y offset adjusted per mode).
- **EP4-only patches**: HUD layout (stats bars, map button/background, enemy bars), bottom bar layouts (experience bars, bless glow), arrow size/loading, loadbar, stats window colors, PvP rank icon alignment, clock position coordinates.

### Interface And Assets

- **Background render arguments**: adjusts startup/login background draw arguments used by the current UI setup.
- **Level-up message suppression**: keeps the stock level-up texture creation flow, but forces the render size to zero so the splash is hidden.
- **GM H-key HP viewer removal**: disables the vanilla redundant GM HP viewer opened by `H`; the custom target viewer remains available.
- **Dungeon map visibility**: allows dungeon maps to be shown by the client.

### Target, Names, Titles, And Text

- **Target HP viewer**: draws current/max HP inside the native target frame for monsters and users, using the configured game font. The Y offset is adjusted per UI mode so the text aligns with EP4 and EP6/EP7 target bar layouts.
- **Item titles**: renders visual titles from cloak data and can be toggled with `TITLES` or `/titles`.
- **Name colors**: renders colored names from helmet data, including rainbow color mode, and can be toggled with `COLOUR` or `/colour`.
- **Font picker**: `/font` updates the GDI font and all known D3DX camera font slots so labels, counters, chat-adjacent overlays, and native text helpers stay consistent.
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
- **Remote NPC panel**: `NpcIcon.png` from `Assets/General` opens a compact ImGui panel for Market, Blacksmith, RR Blacksmith, Vet Manager, Bank, and Guild Manager. The button and panel positions persist in the ImGui settings.
- **Vehicle packet/display support**: supports vehicle-related EP6.4 shape/list packet fields and client display paths where implemented.

### Quick Slots And Input

- Adds the third quick-slot bar allocation/free/save behavior and routes item, skill, and basic-action movement between quick-slot bags.
- Patches quick-slot plus buttons and interaction paths so the extended bars can be opened and used.
- Removes selected client-side quick-slot delays: skill delay, basic action delay, and the extra `+1000ms` delay gate.
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

### Graphics

- Applies camera limit from the global `g_cameraLimit` value.
- Applies costume, pet, wing, and dungeon shadow/lag workarounds used by the current visual setup.

> The custom-resolution table feature has been removed from the client. Its
> design and runtime addresses are preserved in [`docs/resolutions.md`](docs/resolutions.md)
> in case it needs to be reintroduced.

### Discord And ImGui Overlay

- Discord RPC initializes with the static application id/message defined in `src/discord.cpp`.
- The roulette panel is opened from `RouletteIcon.png`. It is visible to all players, requests the server reward list, displays real item names/icons from the configured server rewards, and sends the server roulette roll packet. Hovering a reward on the wheel shows the item name and description tooltip.
- The settings button opens a compact `Quick Settings` panel for visual toggles such as wings, pets, costumes, titles, colours, FPS boost, and effects. The button and panel positions persist in `CONFIG.INI`.
- `F9` toggles the GM debug panel (requires `IsAdmin`). It exposes tabs for `ID View`, `Speed Monitor`, `Chat options` (custom chat wrap/color tuning), `Mounts`, `Effects`, and `Scene`.
- The Core chat replacement is always enabled. It hides stock chat text, records the same incoming chat stream, and renders upper/lower messages at the native CChat coordinates so resolution changes, native vertical resize, and native scroll state remain aligned.
- The welcome system message is a lifecycle behavior rather than a user-controlled panel feature.
- The emoji/GIF picker is an in-world chat helper, not a panel module.
- DirectInput mouse hook suppresses game click-to-move input when ImGui owns the cursor, preventing character movement behind panels.
- Mouse coordinates are scaled from window-client space to DX9 backbuffer space each frame so ImGui input stays accurate at all resolutions.

## Asset Notes

- Interface assets packed in `data.sah/saf` can replace native `.tga`/`.jpg` files with matching `.png` files. If no PNG exists in the archive, the native file is used as fallback.
- Raid button assets are expected as PNG.
- Visual chat token assets are read from the internal data archive: `Assets/Emojis/emojiN.png` and `Assets/Gifs/gifN.gif`.
- Roulette background is read from `Assets/General/Roulette.png` inside `data.sah/saf`.
- Roulette item icons read the native DDS atlases from `data/interface/icon` inside `data.sah/saf` (always this path, not affected by the `UI` mode setting), including special atlases such as `icon_somo3.dds`. The SAH index is parsed once and DDS textures (DXT1/DXT3/DXT5) are decoded by a built-in parser, so the ImGui panel follows the client's data without embedding atlas copies in `sdev-client.dll`.
- Remote NPC, settings, reward, and roulette buttons load their PNGs from `Assets/General` inside `data.sah/saf`.
- Visual title images are read from `Assets/Titles/titleN.png` (static) and `Assets/TitlesAnimated/titleN.gif` (animated) inside `data.sah/saf`.
- Elemental icon badges are read from `Assets/General/{fire,water,earth,wind}.png` inside `data.sah/saf`. The SAH index is parsed once to locate the files and each PNG is decoded by stb_image into a D3D9 managed texture.
- Internal archive access is centralized in `src/game_data_archive.cpp`; features still decide which folders/files they need, but they share the same `data.sah` scanner and `data.saf` reader.
