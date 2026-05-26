# Game Service Module

`sdev` is the `ps_game.exe` server patch module. It installs server-side hooks, packet handlers, configuration loaders, and data compatibility fixes for the Shaiya-Core client.

## Environment

- Windows 10 or newer
- Visual Studio 2022
- C++23
- x86 build target
- Microsoft Visual C++ Redistributable: https://aka.ms/vs/17/release/vc_redist.x86.exe

## Runtime Files

The server resolves paths from the game service executable directory, then the `Data` folder. This avoids broken relative paths when the DLL is imported or injected from a different working directory.

- `Data/ServerConfig.ini`: global limits such as enchant cap and level cap.
- `Data/BattleFieldMoveInfo.ini`: battleground level ranges and destination coordinates.
- `Data/Teleport.ini`: named remote-teleport destinations, level gates, and faction-specific coordinates.
- `Data/SetItem.SData`: decrypted server-side item set synergy data.
- `Data/ChaoticSquare.ini`: item synthesis and chaotic square recipes.
- `Data/RewardItem.ini`: timed reward item event list.
- `Data/Roulette.ini`: roulette token and reward table.
- `Data/EtainShield.ini`: anticheat module configuration (per-feature toggles and tuning constants).

## Installed Hooks

All active server features are installed from `src/main.cpp`.

```cpp
Configuration::Init();
Configuration::LoadServerConfig();
Configuration::LoadBattlefieldMoveData();
Configuration::LoadTeleportDestinations();
Configuration::LoadEtainShield();
hook::etain_shield();
hook::item_effect();
hook::utilities();
hook::packet_character();
hook::packet_exchange();
hook::packet_gem();
hook::packet_main_interface();
hook::packet_market();
hook::packet_myshop();
hook::packet_party();
hook::packet_pc();
hook::packet_quest();
hook::raid_150();
if (Configuration::RewardBarEnabled)
    hook::packet_reward_item();
hook::on_enter_world();
hook::packet_shop();
hook::user_equipment();
hook::user_shape();
hook::user_skill();
hook::user_status();
hook::world_thread();
Configuration::LoadItemSetData();
Configuration::LoadItemSynthesis();
if (Configuration::RewardBarEnabled)
    Configuration::LoadRewardItemEvent();
if (Configuration::RouletteEnabled)
    Configuration::LoadRoulette();
```

## Feature Catalog

### Bootstrap And User State

- Expands `CUser` allocation from `0x62A0` to `0x631C` for custom runtime state and EtainShield per-user fields.
- Initializes custom exchange, skill ability, item quality, and synergy fields on user creation and reset.
- Clears item set synergy state when a user leaves the world.
- Keeps custom file loading independent from the process current directory.

### Configuration

- `ServerConfig.ini` is created with defaults when missing.
- `EnchantCap` defaults to `20`, is clamped to `1..49`, and patches lapisian/enchant cap comparisons.
- `LevelCap` defaults to `70`, is clamped to `1..254`, and patches known server level-cap comparisons.
- `RewardBar` defaults to `1`. When `0`, the reward item event system is disabled.
- `Roulette` defaults to `1`. When `0`, the roulette system is disabled.
- `BattleFieldMoveInfo.ini` maps a level range to one destination per family: Human, Elf, Death Eater, and Vail.
- `Teleport.ini` maps named remote-teleport destinations with optional level gates and separate Light/Fury coordinates.
- `SetItem.SData` loads decrypted set bonuses and refreshes server-side synergy tables.
- `ChaoticSquare.ini` loads item synthesis recipes and chaotic-square result tables.
- `RewardItem.ini` loads the timed account reward list.
- `Roulette.ini` loads the roulette token and up to twenty rewards, then normalizes chance values to a 10000-point table.

### Security And Stability

- Sanitizes command, character, and guild strings before DB-bound paths.
- Validates packet lengths, fixed-string lengths, inventory coordinates, and user/item pointers in custom handlers.
- Guards outgoing packet sends through helper checks.
- Rejects malformed equipment and inventory actions before stock code can read invalid memory.
- Keeps unsupported mailbox handling disabled; no mailbox hook is installed.

### EtainShield (Anticheat)

Server-side anticheat module configured via `Data/EtainShield.ini`. Each protection has an independent enable/disable toggle and tunable parameters. A global master switch disables all protections at once.

- **AntiSpeedHack**: patches four native timing constants and validates every `0x501` movement packet using calibrated speed ceilings measured server-side (12.5 units/s on foot, 13.5 mounted). Mounted state is detected via `CUser::vehicleStatus`. Consecutive violations are forgiven up to the configured threshold to absorb latency spikes; repeated violations trigger a position correction. Reliably detects speed hacks at 1.1x and above.
- **AntiRangeHack**: computes the real 2D euclidean distance from server-side positions for every attack. Basic attack packets (`0x502`/`0x503`) are intercepted in the opcode dispatch; skill attacks are caught by detours on the native PVE (`0x458000`) and PVP (`0x457F50`) range-check functions. Out-of-range attacks are rejected immediately.
- **AntiCutting**: freezes player movement for a configurable duration (default 500 ms) after each attack. Every `0x501` movement packet received during the lock window is silently dropped — the server simply does not update the player's position. The lock renews on each subsequent attack in a chain. Configurable skill-ID skip-list exempts dash-type skills from the lock.

### Cross-Faction And Social

- Enables cross-faction whisper.
- Enables cross-faction trade.
- Enables cross-faction inspect.
- Enables cross-faction login and world-enter behavior.
- Sends GM chat to both factions.
- Allows summon and move behavior in Exiel room.
- Allows union leader summon to affect the full raid.
- Sends party boss and sub-boss map-position broadcast packet `0xB1C`.

### Guilds

- Removes the original seven-officer limit.
- Removes guild creation and join penalty timer checks.
- Allows repeated GRB entry.
- Allows guild creation with two players.

### Character Growth And Timers

- Uses Ultimate Mode stat and skill-point growth paths for every mode.
- Gives five skill points per level through the patched growth branches.
- Prevents stat/skill reset items from clearing potential skills when reset is disabled.
- Shortens the leader resurrection gameplay timer from 30 seconds to 5 seconds.
- **UM chars can ress leader**: allows Ultimate Mode/Grow 3 characters to use party-leader resurrection while preserving the stock party, leader-alive, same-zone, and map validation checks.
- Shortens logout/game-over timing to match the client countdown.
- Revives players with max HP, MP, and SP.
- Allows running while stealthed.

### Raid 150

- Expands `CParty` from 30 users to 150 users.
- Patches party-user array offsets, raid/party flags, critical-section offsets, allocation size, and loop/count comparisons.
- Validates expected machine-code bytes before writing raid detours.
- Aborts the raid patch on unsupported `ps_game.exe` layouts instead of writing blind patches.
- Matches the client five-page raid UI, where each page represents 30 users.

### EP6.4 Equipment And Shape

- Enables EP6.4 equipment slots: pet, costume, and wings.
- Rebuilds equipment validation using item type and slot rules.
- Supports one-hand weapon plus shield/off-hand combinations.
- Sends inspect packet `0x307` with up to 17 visible equipment entries.
- Sends EP6.4 user-shape packets with extended visual fields and vehicle data.
- Expands clone-user allocation for extended shape data.
- Extends equipment initialization, equipment movement, and equipment clearing to the EP6.4 item-list count.
- Stores extended item quality outside the original compact quality array.

### Item Drops, Inventory, And Gold

- Sends solo gold and solo item drops directly to inventory where appropriate.
- Supports optional gold bonus settings in the direct gold path.
- Stacks Fortune Bag drops in inventory.
- Enables helmet and mantle drops.
- Fixes mantle drops from bag-item drop paths.
- Fixes mantle merchant spawn at server startup.
- Routes invalid mantle enhancement attempts to the normal failure path.
- Adds infinite consumable behavior for explicitly listed unsellable item IDs.
- Fixes lapisia operation flux behavior.

### Item Effects

- Effect `103`: safety charms for gem/lapisian flows.
- Effect `104`: town move scrolls, using item `ReqVg` as the gatekeeper `NpcTypeID` on map `2`.
- Effect `105`: item ability transfer cube. Inventory slot 1 is the source, slot 2 is the cube, and slot 3 is the target. `CraftName` and `Gems` move from source to target.
- Effect `62`: vanilla random recreation.
- Effects `86..91`: random STR, DEX, INT, WIS, REC, and LUC recreation.
- Effects `220..222`: random HP, MP, and SP recreation.
- Effects `223..231`: perfect STR, DEX, INT, WIS, REC, LUC, HP, MP, and SP recreation.
- Effects `232..240`: removal runes for STR, DEX, INT, WIS, REC, LUC, HP, MP, and SP.
- Chaotic-square and synthesis effects are handled through configured recipe tables.

### Recreation Runes

- HP, MP, and SP recreation is restricted to non-weapon, non-accessory armor-style items.
- `ReqWis` is reused as the maximum single craft-stat value.
- MP and SP handling uses the correct `maxMana` and `maxStamina` item fields.
- The client accepts effects `220..240` in the blacksmith recreation window; the server performs the actual item mutation.

### Lapisian And Enchanting

- Perfect lapisian support uses item `ReqRec` as a custom scaled success rate.
- If `ReqRec` is zero, the server falls back to `g_LapisianEnchantSuccessRate`.
- Safety charm behavior is integrated into lapisian success and failure paths.
- Packet `0x116` sends server weapon/enchant step values to the client.
- The enchant cap comes from `ServerConfig.ini`.

### Packets And EP6.4 Protocol

- `0x101`: converts DB character list rows to the EP6.4 client structure, including cloak info and deletion state.
- `0x119` and DB `0x40D`: validates character-name availability and returns the availability result.
- `0x711`: sends EP6.4 warehouse item units in chunks under the 2048-byte packet limit.
- `0x407` and `0x1F00`: tracks reward item event progress and sends reward availability/result packets.
- `0x834` and `0x835`: sends roulette configuration and handles roulette spins.
- `0x838` and `0x839`: sends configured remote-teleport destinations and handles movement requests.
- `0x233`: handles battleground movement requests from the client.
- `0x55A`: handles town move scroll requests with gate validation.
- `0x230B`: converts personal shop item lists to EP6.4 units.
- `0x1C01`: maps the EP6.4 vehicle market type around unsupported stock market categories.
- `0xA05`, `0xA09`, `0xA0A`, `0x240A`, and `0x240D`: support EP6.4 exchange item units and PVP exchange compatibility.
- `0x2602`, `0x2603`, and `0xE06`: support item mall purchase/gift point flows and point persistence.

### Battleground Movement

- Packet `0x233` selects the valid battlefield for the player level.
- Destination coordinates are selected by family.
- Dead players, logging-out players, players already in the target map, and missing destination zones are rejected.
- Movement uses the same 5 second cast/update path as town move scrolls.
- The request does not consume an item; saved item-use bag, slot, and index are cleared before scheduling movement.
- MyShop is ended and current actions are cancelled before the cast starts.

### Remote Teleport

- Packet `0x838` sends the configured destination list to the client.
- Packet `0x839` validates the selected destination, level range, faction coordinates, current map, death state, and busy state.
- Valid movement requests reuse the same 5 second cast/update path as town move scrolls and battleground movement.
- The request does not consume an item; saved item-use bag, slot, and index are cleared before scheduling movement.
- MyShop is ended and current actions are cancelled before the cast starts.

### Quests And Rewards

- Supports EP6 quest-end packet `0x903`.
- Supports six quest result choices, each with up to three item rewards.
- Sends game-log quest-end entries for awarded items.
- Adds EXP and money through stock server helpers.
- Runs reward item event checks every three seconds from `CWorldThread::Update`.
- Sends reward item and roulette feature data when the user enters the world, when those systems are enabled.
- Tracks reward event progress by account user id.

### Roulette

- Sends roulette list packet `0x834` with configured token and reward data.
- Spin packet `0x835` validates configuration, token count, pending spin state, and inventory capacity.
- Consumes one configured token at spin start.
- Stores a pending reward with a 4.5 second completion time.
- Delivers the selected reward through the normal item creation path when the spin is complete.
- Prevents duplicate rewards from one token by guarding pending spin state.

### Skill Abilities

- Supports ability `35` EXP stones using the EP6 ability value as a multiplier source.
- Supports ability `70` with delayed cleanup after the skill is stopped.
- Supports ability `87` quest/EXP-style multiplier behavior.
- Fixes ability type `19` cooldown handling so the server uses the real skill cooldown instead of the old fixed 500 second fallback.
- Clears custom skill state on death, skill clear, and end-time handling.

### Status, Stats, And Combat

- Recalculates attack and equipment stats through patched equipment add/remove/reset paths.
- Includes cloak slot/type in recalculation so cloaks contribute defense and resistance.
- Fixes recover-add sends for HP, MP, and SP with the custom status layout.
- Supports off-hand one-hand weapon recognition.
- Applies death-skill behavior through the patched/default death skill path.
- Fixes selected movement and combat edge cases inherited from the original patch set.

### Item Set Synergy

- Loads decrypted server `SetItem.SData`.
- Tracks equipped item ids per character.
- Applies synergy bonuses for strength, dexterity, intelligence, wisdom, reaction, luck, health, mana, stamina, melee attack, ranged attack, and magic attack.
- Removes stale synergy bonuses when equipment changes or the user leaves the world.

### Bosses, Obelisks, And World Thread

- Sends server-wide boss death and spawn notices.
- Stabilizes `Obelisk.ini` spawn timing by removing the stock one-hour randomizer.
- Runs reward item event update checks from the world thread.

## Configuration Examples

### EtainShield.ini

```ini
[General]
Enabled=1

[AntiSpeedHack]
Enabled=1
MaxSpeedOnFoot=12.5
MaxSpeedMounted=13.5
ViolationLimit=5

[AntiRangeHack]
Enabled=1
Margin=4
MovingGrace=5

[AntiCutting]
Enabled=1
LockMs=500
SkipSkillIds=56
```

### ServerConfig.ini

```ini
[Server]
EnchantCap=20
LevelCap=70
RewardBar=1
Roulette=1
```

### Roulette.ini

```ini
[Info]
TokenItemID=100200
TokenCount=1

[Reward_1]
ItemID=100001
Count=1
Chance=5000

[Reward_2]
ItemID=100002
Count=1
Chance=3000

[Reward_3]
ItemID=100003
Count=1
Chance=2000
```

### RewardItem.ini

```ini
[RewardItem_1]
Delay=5
Type=100
TypeID=1
Count=1

[RewardItem_2]
Delay=10
Type=100
TypeID=2
Count=1
```

### Teleport.ini

```ini
[TELEPORT_INFO]
DESTINATION_COUNT=1

[DESTINATION_1]
NAME=Auction House
LEVEL_MIN=1
LEVEL_MAX=70
MAP_LIGHT=42
POSX_LIGHT=50
POSY_LIGHT=0
POSZ_LIGHT=50
MAP_FURY=42
POSX_FURY=60
POSY_FURY=0
POSZ_FURY=60
```

### ChaoticSquare.ini

```ini
[ChaoticSquare_1]
ItemID=102073
SuccessRate=80
MaterialType=30,30,30,30
MaterialTypeID=5,5,5,5
MaterialCount=1,1,1,1
NewItemType=30
NewItemTypeID=6
NewItemCount=1
```

## Client Compatibility

- `sdev-client` provides the ImGui roulette module that sends packets `0x834` and `0x835`.
- `sdev-client` provides the battleground button that sends packet `0x233`.
- `sdev-client` provides the remote teleport UI that sends packets `0x838` and `0x839`.
- `sdev-client` provides the five-page raid UI for the server raid-150 patch.
- `sdev-client` accepts recreation rune effects `220..240` in the blacksmith window; `sdev` applies the server mutation.
- Emoji and GIF chat tokens are client-rendered only. The server receives and forwards normal chat text.
- EtainShield operates entirely server-side and requires no client modifications.
