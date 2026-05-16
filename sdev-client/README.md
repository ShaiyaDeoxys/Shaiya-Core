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

The root build script restores the required DirectX SDK package and builds `sdev-client` as Release|x86.

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

; 0 uses data/interface (default EP4 HUD layout).
; 1 redirects the interface folder to data/intf_epi6.
; 2 redirects the interface folder to data/intf_epi8.
; Non-zero values disable the EP4 HUD layout package.
UI=0

; GM/admin ID view. Always enabled in the current build.
IDVIEW=ON

; Visual title rendering from cloak data.
TITLES=ON

; Visual colored-name rendering from helmet data.
COLOUR=ON

; Optional visual/performance toggles controlled by chat commands and F7.
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

### Startup, Login, And Windowing

- **Updater bypass**: `ADVANCED/SKIPUPDATER=1` injects the same command-line token normally provided by `Updater.exe`, allowing direct `Game.exe` launch without changing the normal startup state machine.
- **Login server override**: `ADVANCED/IP=` rewrites the stock login server string. Empty or missing values keep the default `127.0.0.1`.
- **Login splash skip**: removes the Nexon/copyright splash and shortens the startup waits while preserving required login resource initialization.
- **Single-server skip**: `ADVANCED/SKIPSERVERSELECTION=1` hides the server panel and selects the only available server through the safe delayed stock path.
- **Unicode main HWND**: upgrades the GAME window to a Unicode window and keeps the visible title as `Shaiya`, preventing the legacy title truncation issue.
- **UTF-8 chat input**: accepts composed Unicode/IME text, stores UTF-8 bytes in the stock textbox, fixes multibyte wrapping/rendering branches, and removes the forced byte-127 send terminator.
- **System message dispatch**: provides a private window message used by client code to safely post system messages back through the game UI thread.
- **Welcome message**: posts the existing `SysMsg` welcome entry after the client UI is ready. The message text remains owned by the normal `sysmsg.txt` data.
- **Visual chat tokens**: draws a movable ImGui emoji button near the chat input during gameplay. The button position can be relocated from the picker panel (Move/Reset) and is persisted to `CONFIG.INI`. The picker scans `emojiN.png` entries from `Assets/Emojis` and `gifN.gif` entries from `Assets/Gifs` in `data.sah/saf`, then inserts plain chat tokens such as `:emoji1:` or `:gif2:` into the stock textbox through the game UI thread. The picker exposes separate ON/OFF toggles for emojis and GIFs; disabled token families are hidden from native text without drawing an overlay. GIF picker entries use lightweight static previews with a bounded resident cache, while full animation is loaded only when a GIF is rendered in chat/floating text. Packets and server handling remain plain text.

### Character Creation And Selection

- **Mode selection skip**: `ADVANCED/SKIPMODESELECTION=1` bypasses the mode-selection UI and forces Ultimate Mode during character creation.
- **Name gate bypass**: skips local ASCII-only name format checks, legacy blocked-substring checks, the local "name already verified" gate, and the UI availability request. Real validation must be enforced server/dbAgent side.
- **Busy-as-success fallback**: treats one legacy character-creation busy result path as success for compatibility with UTF-8/special-name creation flows.
- **Character select adjustments**: patches select-screen texture/text positions used by the current interface pack.
- **Extended character allocation**: grows `CCharacter` allocation from `0x43C` to `0x444`, initializes custom members, and resets them on character reset.

### Interface And Assets

- **PNG interface redirect**: rewrites known interface `.tga`/`.jpg` references to `.png` at runtime. The redirect is intentionally limited to known UI paths and avoids broad icon conversion.
- **Custom UI folder**: `ADVANCED/UI=1` redirects stock `data/interface` references to `data/intf_epi6`; `UI=2` redirects to `data/intf_epi8`. `UI=0` or a missing setting keeps `data/interface`.
- **PNG screenshots**: rewrites screenshot filename templates from `.jpg/.JPG` to `.png`.
- **EP4 HUD package**: ports selected EP4 HUD pieces: main stats frame/bars/level, target bar, target buffs/debuffs, map/minimap buttons/background/clock/server time, map arrows, bottom button strips, option main button, and load bar. Includes 15 EXP/Bless bar hooks (position, length, width, text, and glow) across three resolution variants (800, 640, 1024) to fit bars inside the EP4 ornamental frame. Inventory and stock EXP/Bless bars are intentionally not replaced. This package is disabled when `ADVANCED/UI` is non-zero so custom interface layouts remain coherent.
- **Background render arguments**: adjusts startup/login background draw arguments used by the current UI setup.
- **Level-up message suppression**: keeps the stock level-up texture creation flow, but forces the render size to zero so the splash is hidden.
- **GM H-key HP viewer removal**: disables the vanilla redundant GM HP viewer opened by `H`; the custom target viewer remains available.
- **Stats window color patch**: adjusts patched stats-window colors to fit the current interface.
- **Dungeon map visibility**: allows dungeon maps to be shown by the client.
- **PvP rank icon alignment**: adjusts UI image coordinates so PvP rank icons render correctly in both the standard and EP6 interface layouts.

### Battleground Button

- Draws `main_stats_pvp_button.png` inside the main stats UI without rewriting the whole `CStatusMiniBar` layout.
- When `ADVANCED/UI` is non-zero, the button render position and click hitbox are shifted upward to match the custom main stats frame.
- Reads `BattleFieldMoveInfo_Client.ini` from the client root or `Data` folder to display the correct battlefield name for the player level.
- Sends packet `0x233` only after the click starts and ends inside the button, avoiding accidental activation during window maximize/minimize.
- Uses a native confirmation dialog before sending the move request.

### Target, Names, Titles, And Text

- **Target HP viewer**: draws current/max HP inside the native target frame for monsters and users, using the configured game font.
- When `ADVANCED/UI` is non-zero, the target HP viewer is shifted upward to match the custom target frame.
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
- **Elemental icon overlay**: draws a small element badge (fire, water, earth, wind) in the bottom-right corner of eligible item icons in both inventory and quickslot bars. Detection checks inserted lapis gems first, then falls back to the base item attribute. Textures are loaded once from `Assets/General/{fire,water,earth,wind}.png` inside `data.saf`.
- **Two-hand/off-hand logic**: fixes `CPlayerData::IsTwoHandWeapon` behavior so custom off-hand support can coexist with one-hand weapons.
- **Weapon step display**: patches the client weapon-step path used by lapisian/enchant display.
- **Item description augmentation**: a background thread runs once after item data is loaded and appends OJ reroll info (maximum OJs and highest OJ value, or "cannot be rerolled") and grade value (admin-only) to each item's description pointer in-place. Zero per-frame overhead.
- **NPC shortcut buttons**: draws a vertical strip of icon buttons along the right edge of the inventory panel. Each button loads its texture from `Assets/General/inven_new_buton{1..6}.png` in `data.saf` as a four-frame sprite strip (normal, hover, pressed, disabled).
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

- **Ress leader timer**: aligns the client popup countdown with the server-side 5 second timer.
- **Logout/game-over timer**: aligns the visible logout countdown with the shortened server logout delay of 2000ms.
- **Subaction sysmsg cooldown**: rate-limits sysmsgs `5228..5237` to one visible message every 20 seconds per player name instead of removing them completely.
- **Experience view fix**: prevents the client from displaying experience values multiplied by ten and ignores locale-specific EXP multiplication where applicable.

### Resolution And Graphics

- Adds `1366x768`, `1400x900`, `2560x1080`, `2560x1440`, `3840x1080`, `3840x2160`, and `3440x1440`.
- Expands option-screen resolution rendering, selection, saving, and apply behavior.
- Reuses stock large-layout positioning branches so UI placement remains stable at wider resolutions.
- Caches gamma/distance/float option values to prevent the expanded resolution table from corrupting option sliders.
- Applies camera limit from the global `g_cameraLimit` value.
- Applies costume, pet, wing, and dungeon shadow/lag workarounds used by the current visual setup.

### Discord And ImGui Panel

- Discord RPC initializes with the static application id/message defined in `src/discord.cpp`.
- `F8` toggles the ImGui roulette panel. The panel is visible to all players, requests the server reward list, displays real item names/icons from the configured server rewards, and sends the server roulette roll packet. Hovering a reward on the wheel shows the item name and description tooltip.
- `F9` toggles the GM debug panel (requires `IsAdmin`). Shows a chat type monitor that records incoming chat types with preview text to help identify upper-bar exclusions. The monitor is a per-session toggle activated from the panel. The panel code lives in `src/debug_panel.cpp`.
- `F7` remains an external realtime/performance toggle and is not part of the panel system.
- The welcome system message is a lifecycle behavior rather than a user-controlled panel feature.
- The emoji/GIF picker is an in-world chat helper, not a panel module. Its ON/OFF controls live inside the picker.
- DirectInput mouse hook suppresses game click-to-move input when ImGui owns the cursor, preventing character movement behind panels.
- Mouse coordinates are scaled from window-client space to DX9 backbuffer space each frame so ImGui input stays accurate at all resolutions.

## Asset Notes

- Interface assets used by the PNG redirect must exist in the client `Data/interface` tree.
- Custom interface assets for `ADVANCED/UI=1` must exist in `Data/intf_epi6`; for `UI=2` in `Data/intf_epi8`. Non-icon `.tga/.jpg` files should be converted to `.png`; the broad PNG redirect intentionally leaves `icon` assets alone.
- Raid button assets are expected as PNG.
- Battleground uses `main_stats_pvp_button.png`.
- Visual chat token assets are read from the internal data archive: `Assets/Emojis/emojiN.png` and `Assets/Gifs/gifN.gif`.
- Roulette background is read from `Assets/General/Roulette.png` inside `data.sah/saf`.
- Roulette item icons use embedded DDS atlas resources in `resources/item_icons_atlas`. These resources mirror the client item icon atlases needed to draw server-defined rewards inside the ImGui panel. DDS textures (DXT1/DXT3/DXT5) are decoded by a built-in parser; PNG textures are decoded by stb_image. Neither path requires the D3DX runtime.
- NPC shortcut button textures are read from `Assets/General/inven_new_buton{1..6}.png` inside `data.sah/saf`. Each PNG is a 4-frame vertical sprite strip (normal, hover, pressed, disabled).
- Visual title images are read from `Assets/Titles/emojiN.png` (static) and `Assets/TitlesAnimated/gifN.gif` (animated) inside `data.sah/saf`.
- Elemental icon badges are read from `Assets/General/{fire,water,earth,wind}.png` inside `data.sah/saf`. The SAH index is parsed once to locate the files and each PNG is decoded by stb_image into a D3D9 managed texture.
- The client intentionally keeps icon assets outside the broad PNG redirect unless a feature explicitly handles them.
- Custom recreation rune UI acceptance is only client-side placement. Server behavior is implemented in `sdev`.
