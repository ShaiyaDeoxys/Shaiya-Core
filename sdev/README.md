# Documentation

This library is for the game service. Please read the features section to learn more.

## Environment

Windows 10

Visual Studio 2022

C++ 23

## Prerequisites

[Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x86.exe)

## Binaries

https://github.com/kurtekat/shaiya-episode-6/tree/main/sdev/bin/

| NpcQuest | Max Level |
|----------|-----------|
| EP6      | 80        |

# Features

All features are implemented based on client specifications. The intent is to keep everything as vanilla as possible.

## Stable Server Patch Groups

- Cross-faction whisper, trade, inspect, login, and world-enter support.
- Guild quality-of-life patches: officer limit, penalty removal, repeat GRB entry, and 2-player guild creation.
- Security sanitizers for command, character, and guild strings before DB-bound paths.
- Raid 150 component for expanded raid/party support.
- Rune cutting fixes for arena, auction house, capital, and guild map paths.
- Direct solo gold/item drops, optional gold bonus settings, and Fortune Bag inventory stacking.
- Boss death/spawn notices, instant mounts, revive with max HP/MP/SP, and death-skill trigger.
- Infinite consumables for explicitly listed unsellable item IDs.
- Spell cooldown fix for Ability Type 19 so the server uses the real value instead of the fixed 500s fallback.
- Stabilized Obelisk.ini spawn timing with the one-hour randomizer removed.
- Cloaks contributing real defense/resistance, off-hand support, stealth running, mantle drop fixes, and mantle packet hardening.
- Battleground movement support using the normal cast/update flow.

## Server Feature Catalog

This section maps the active server-side features installed by `Main()` in `src/main.cpp`.

### Bootstrap And Configuration

- **Root path detection**: configuration files are resolved from the executable directory, then `Data`, so injected/imported DLL working-directory issues do not break file loads.
- **Extended `CUser` memory**: user allocation is increased from `0x62A0` to `0x62F4`. Custom fields include exchange confirmation state, extended item quality arrays, skill ability 70 storage, quest EXP multiplier storage, and synergy runtime state.
- **User lifecycle cleanup**: custom user fields are initialized on construction/reset. Item-set synergy state is erased on world leave.
- **`ServerConfig.ini`**: `Data/ServerConfig.ini` is created with defaults when missing. `EnchantCap` defaults to `20`, is clamped to `1..49`, and patches the lapisian enchant cap. `LevelCap` defaults to `70`, is clamped to `1..254`, and patches all known level-cap comparisons.
- **`BattleFieldMoveInfo.ini`**: loads battlefield level ranges and per-family destination coordinates for the battleground button server packet.
- **`SetItem.SData`**: loads decrypted server-side set/synergy data and resets stale synergy state on reload.
- **`ChaoticSquare.ini`**: loads item synthesis recipes and builds the chaotic-square result tables used by the packet handlers.
- **`RewardItem.ini`**: loads the account-wide reward item event list. The server supports up to the configured `RewardItemUnit` list size and ignores malformed rows.

### Security And Stability

- Sanitizes command, character, and guild strings before DB-bound paths.
- Hardens malformed network sends through `NetworkHelper` null/length checks.
- Rejects malformed item/equipment operations where pointers or inventory coordinates are invalid.
- Validates character-name availability packet length and name length before DB lookup.
- Guards 6.4 packet conversions against overflowing fixed packet payload limits.
- Prevents invalid inventory bag indexes from being used as warehouse memory.
- Keeps unsupported mailbox behavior disabled.

### Cross-Faction And Social Behavior

- Enables cross-faction whisper.
- Enables cross-faction trade.
- Enables cross-faction inspect.
- Enables cross-faction login/world-enter paths.
- Sends GM chat to both factions.
- Allows summon/move behavior in Exiel room.
- Allows union leader summon to affect the whole raid.
- Supports party boss/sub-boss map-position broadcast packet `0xB1C`.

### Guild Behavior

- Removes the original seven-officer limit.
- Removes guild creation/join penalty timer checks.
- Allows repeated GRB entry by not storing the one-entry marker.
- Allows guild creation with two players instead of seven.

### Character Growth And Timers

- Uses Ultimate Mode stat/skill-point reset and level-up paths for every mode.
- Gives five skill points per level across the patched growth branches.
- Prevents stat/skill reset items from clearing potential skills when reset is disabled.
- Shortens the ress-leader gameplay timer from 30 seconds to 5 seconds.
- Shortens logout/game-over server-side timing so it aligns with the client visual patch.
- Revives players with max HP/MP/SP.
- Allows characters to run while stealthed.

### Raid 150

- Expands `CParty` from 30 users to 150 users.
- Patches party-user array offsets, raid/party flags, critical-section offsets, allocation size, and all known loop/count comparisons.
- Validates original operand bytes before patching; if an unsupported `ps_game.exe` layout is detected, the raid patch aborts instead of writing blind bytes.
- Installs detours for server paths that iterate, send, or index raid members.
- Works with the client five-page raid UI, where each page represents 30 users.

### EP6.4 Equipment And Shape Support

- Enables EP6.4 equipment slots: vehicle, pet, costume, and wings.
- Rebuilds equipment-slot validation using real type and item type rules.
- Supports one-hand weapon plus shield/off-hand combinations.
- Sends `0x307` inspect equipment packets with up to 17 visible equipment entries.
- Sends 6.4 user-shape packets including extended visual fields and vehicle data.
- Expands clone-user allocation for the extended shape data.
- Extends equipment initialization, bag-to-bag equipment movement, and clear-equipment paths to the 6.4 item-list count.
- Stores extended item quality outside the original compact array and redirects quality/enchant paths to it.

### Item Drops, Inventory, And Gold

- Sends solo gold and solo item drops directly to inventory where appropriate.
- Supports optional gold bonus settings in the direct gold path.
- Stacks Fortune Bag drops in inventory.
- Enables helmets and mantles to drop.
- Fixes mantles not dropping from bag-item drop paths.
- Fixes mantle merchants spawning at server start.
- Hardens mantle enhancement packets so invalid mantle enhancement attempts go to the normal failure path.
- Adds infinite consumable behavior for explicitly listed unsellable item IDs.
- Fixes lapisia operation flux behavior.

### Item Effects

- **Safety charms**: item effect `103` is handled in gem/lapisian flows.
- **Town move scrolls**: item effect `104` uses item `ReqVg` as the gatekeeper `NpcTypeID` on map `2`.
- **Item ability transfer cube**: item effect `105` transfers `CraftName` and `Gems` from inventory slot 1 to slot 3, with slot 2 as the cube.
- **Recreation runes**: supports vanilla, explicit random, perfect, and removal recreation rune effects, including HP/MP/SP effects.
- **Chaotic squares / synthesis**: supports configured item synthesis recipes and material checks before consuming resources.
- Unsupported or intentionally disabled effects fall back to stock behavior.

### Recreation Runes

- Vanilla random recreation effect `62` remains supported.
- Random STR/DEX/INT/WIS/REC/LUC effects `86..91` are supported.
- Random HP/MP/SP effects `220..222` are supported.
- Perfect STR/DEX/INT/WIS/REC/LUC/HP/MP/SP effects `223..231` are supported.
- Removal STR/DEX/INT/WIS/REC/LUC/HP/MP/SP effects `232..240` are supported.
- HP/MP/SP recreation is restricted to non-weapon, non-accessory armor-style items.
- `ReqWis` is reused as the maximum single craft-stat value.
- MP/SP handling uses the correct `maxMana` and `maxStamina` item fields.

### Lapisian And Enchanting

- Perfect lapisian support uses item `ReqRec` as a custom scaled success rate.
- If `ReqRec` is zero, the server falls back to `g_LapisianEnchantSuccessRate`.
- Safety charm behavior is integrated into success/failure paths.
- Weapon-step packet `0x116` sends server enchant-step values to the client.
- The enchant cap comes from `ServerConfig.ini`.

### Packets And EP6.4 Protocol Support

- **Character list `0x101`**: converts DB character list rows to the EP6.4 client structure, including cloak info and deletion state.
- **Name availability `0x119` / DB `0x40D`**: forwards validated name checks to dbAgent and returns the availability result.
- **Warehouse `0x711`**: sends 6.4 bank item units in chunks that stay under the 2048-byte packet limit. Warehouse access is read-only in this path.
- **Reward item event `0x407` / `0x1F00`**: tracks account-wide reward progress and notifies the client when a reward can be claimed.
- **Main interface `0x233`**: handles battleground movement requests from the client.
- **PC `0x55A`**: handles town move scroll requests with explicit gate index validation.
- **MyShop `0x230B`**: converts shop item lists to EP6.4 units.
- **Market `0x1C01`**: maps the EP6.4 vehicle market type around unsupported stock market categories.
- **Exchange `0xA05/0xA09/0xA0A/0x240A/0x240D`**: supports 6.4 exchange item units and PVP exchange packet compatibility.
- **Shop / Item Mall `0x2602/0x2603/0xE06`**: supports 6.4 item mall purchase/gift point flows and point persistence.
- Unknown custom main-interface packets are closed explicitly rather than silently ignored.

### Battleground Movement

- Packet `0x233` chooses the valid battlefield by player level from `BattleFieldMoveInfo.ini`.
- Destination is selected by family index: HU, EL, DE, VI.
- Dead players, players logging out, players already in the destination map, and missing destination zones are rejected.
- Movement uses the same 5 second cast/update path as town move scrolls.
- The request is not backed by a consumable item; `savePosUseBag`, `savePosUseSlot`, and `savePosUseIndex` are cleared before scheduling movement.
- MyShop is ended and current actions are cancelled before the cast starts.

### Quests And Rewards

- Supports EP6 quest-end packet `0x903`.
- Supports six quest result choices, each with up to three item rewards.
- Sends game-log quest-end entries for awarded items.
- Adds EXP and money through stock server helpers so downstream accounting remains intact.
- Account-wide reward item event progress is checked every three seconds in the world thread.
- Reward progress resets or completes according to the existing reward event flow described below.

### Skill Abilities

- Extends ability support for ability `35` EXP stones.
- Supports ability `70` effects with delayed removal after the skill is stopped.
- Supports ability `87` quest/EXP-style multiplier behavior.
- Fixes ability type `19` cooldown handling so the server uses the real skill cooldown instead of the old fixed 500 second fallback.
- Hooks skill cleanup paths so custom ability state is removed on death, skill clear, and end-time handling.
- Keeps unsupported ability types on the stock server path.

### Status, Stats, And Combat

- Recalculates attack and equipment stats through patched equipment add/remove/reset paths.
- Cloaks contribute real defense and resistance by including cloak slot/type in the recalculation range.
- Patches recover-add sends so HP/MP/SP recovery packets behave with the custom status layout.
- Supports off-hand one-hand weapon recognition in server equipment logic.
- Applies death-skill behavior using the configured/default death skill path.
- Fixes javelin/jump-cut style movement/combat edge cases from the original patch set.

### Bosses, Obelisks, And World Thread

- Sends server-wide boss death and spawn notices.
- Stabilizes `Obelisk.ini` spawn timing by removing the stock one-hour randomizer. Practical scheduler delay can still add a small offset.
- Runs reward item event checks from `CWorldThread::Update`.

### Disabled Or Intentionally Not Implemented

- Mailbox packets are present as a stub but the hook is disabled.
- Medal event behavior is not implemented.
- Free chaotic-square combination is not implemented.
- Unsupported EP6.4 market vehicle behavior is handled with a compatibility workaround, not full native market support.

## Item Mall

Install the following procedures:

```
[dbo].[usp_Read_User_CashPoint_UsersMaster]
[dbo].[usp_Save_User_BuyPointItems2]
[dbo].[usp_Save_User_GiftPointItems2]
[dbo].[usp_Update_UserPoint]
```

If you receive an error, change `ALTER` to `CREATE` and try again.

## Reward Item Event

Event progress is account-wide. The progress of the current item will reset if a character leaves the game world. Do not expect the progress bar to synchronize perfectly. The event will reset when the last item is received.

### Configuration

The client expects no more than 20 items. Use the following example to get started:

```ini
; PSM_Client\Bin\Data\RewardItem.ini

[RewardItem_1]
; minutes
Delay=5
Type=100
TypeID=1
Count=1

[RewardItem_2]
Delay=10
Type=100
TypeID=2
Count=1

[RewardItem_3]
Delay=15
Type=100
TypeID=3
Count=1

[RewardItem_4]
Delay=20
Type=100
TypeID=4
Count=1

[RewardItem_5]
Delay=25
Type=100
TypeID=5
Count=1
```

Add the following system messages:

```
2044		"The keep-alive event has ended."
//
7186		"Medal event begins in 5 minutes! Make sure you have at least 3 open inventory slots or else you will not be able to receive a reward."
7187		"Medal event begins in 1 minute! Make sure you have at least 3 open inventory slots or else you will not be able to receive a reward."
7188		"Could not receive your reward because you do not have space in your inventory."
7189		"Bronze medal received"
7190		"Silver medal received"
7191		"Gold medal received"
7192		"Recurring player item received"
```

### Medal Event

This feature will not be implemented.

### Timeout

The client adds 15 seconds to the timeout.

```cpp
auto minutes = rewardItem[index].minutes;
auto timeout = GetTickCount() + ((minutes * 60000) + 15000);
```

### Clients

| Locale | Patch | Supported          |
|--------|-------|--------------------|
| ES     | 171   | :x:                |
| PT     | 182   | :white_check_mark: |
| PT     | 189   | :white_check_mark: |

## Alchemy

The client expects the merchant type of the npc to be 20 (Alchemist).

| NpcType | NpcTypeID |
|---------|-----------|
| 1       | 310       |

## Rune Combination

The following items are supported:

### Vials

| ItemId | Effect |
|--------|--------|
| 101007 | 93     |
| 101008 | 94     |
| 101009 | 95     |
| 101010 | 96     |
| 101011 | 97     |
| 101012 | 98     |

### Recreation Runes

The server supports vanilla random recreation plus explicit random,
perfect, and removal runes by item effect. The client patch allows effects
`220..240` in the NPC recreation window.

| Effect | Result |
|--------|--------|
| 62     | Vanilla random recreation |
| 86-91  | Random STR/DEX/INT/WIS/REC/LUC |
| 220-222 | Random HP/MP/SP |
| 223-231 | Perfect STR/DEX/INT/WIS/REC/LUC/HP/MP/SP |
| 232-240 | Remove STR/DEX/INT/WIS/REC/LUC/HP/MP/SP |

## NpcQuest

The episode 6 format has 6 quest results, each containing up to 3 items. The game service executable has been modified to read the file format.

## Skill Abilities

This library adds support for episode 6 skill abilities 70, 87, and 35 (exp stones). **It does not affect other skills.**

| SkillId | Ability | Supported          |
|---------|---------|--------------------|
| 222     | 35      | :white_check_mark: |
| 223     | 35      | :white_check_mark: |
| 272     | 35      | :white_check_mark: |
| 375     | 52      | :x:                |
| 376     | 53      | :x:                |
| 377     | 54      | :x:                |
| 378     | 55      | :x:                |
| 379     | 56      | :x:                |
| 380     | 57      | :x:                |
| 398     | 70      | :white_check_mark: |
| 399     | 70      | :white_check_mark: |
| 400     | 70      | :white_check_mark: |
| 401     | 70      | :white_check_mark: |
| 396     | 73      | :x:                |
| 397     | 74      | :x:                |
| 412     | 78      | :x:                |
| 426     | 35      | :white_check_mark: |
| 427     | 35      | :white_check_mark: |
| 432     | 87      | :white_check_mark: |
| 434     | 35      | :white_check_mark: |

### Skill Ability 35

The original code multiplies exp depending on the value of two `CUser` booleans:

```
// 00574080 (2.0)
bool32_t multiplyExp2;  //0x596C
// 00574090 (1.5)
bool32_t multiplyExp1;  //0x5970 (not used)
```

The library modifies `CUser::AddExpFromUser` to support 6.4 skill ability values.

| ItemId | SkillId | SkillLv | AbilityValue |
|--------|---------|---------|--------------|
| 100042 | 222     | 1       | 150          |
| 100043 | 223     | 1       | 150          |
| 101114 | 426     | 2       | 500          |
| 101115 | 426     | 3       | 1000         |
| 101117 | 427     | 2       | 500          |
| 101121 | 434     | 2       | 200          |
| 101122 | 434     | 3       | 200          |

The ability value is expected to be greater than 100. The library will divide the ability value by 100.

### Skill Ability 70

The effect(s) will be removed a few seconds after the skill has been stopped.

### Skill Ability 87

Use the following items to get started:

| ItemId | SkillId | SkillLv |
|--------|---------|---------|
| 101112 | 432     | 2       |
| 101113 | 432     | 3       |

The ability value is expected to be greater than 100. The library will divide the ability value by 100.

## Chaotic Squares

### ChaoticSquare.ini

Use the following example to get started:

```ini
; PSM_Client\Bin\Data\ChaoticSquare.ini

[ChaoticSquare_1]
ItemID=102073
SuccessRate=80
MaterialType=30,30,30,30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialTypeID=5,5,5,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialCount=1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NewItemType=30
NewItemTypeID=6
NewItemCount=1

[ChaoticSquare_2]
ItemID=102073
SuccessRate=80
MaterialType=30,30,30,30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialTypeID=12,12,12,12,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialCount=1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NewItemType=30
NewItemTypeID=13
NewItemCount=1

[ChaoticSquare_3]
ItemID=102073
SuccessRate=80
MaterialType=30,30,30,30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialTypeID=19,19,19,19,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialCount=1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NewItemType=30
NewItemTypeID=20
NewItemCount=1

[ChaoticSquare_4]
ItemID=102073
SuccessRate=80
MaterialType=30,30,30,30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialTypeID=26,26,26,26,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialCount=1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NewItemType=30
NewItemTypeID=27
NewItemCount=1

[ChaoticSquare_5]
ItemID=102073
SuccessRate=80
MaterialType=30,30,30,30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialTypeID=33,33,33,33,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialCount=1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NewItemType=30
NewItemTypeID=34
NewItemCount=1

[ChaoticSquare_6]
ItemID=102073
SuccessRate=80
MaterialType=30,30,30,30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialTypeID=40,40,40,40,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
MaterialCount=1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NewItemType=30
NewItemTypeID=41
NewItemCount=1
```

### Money

The gold per percentage value in the library is the same as the official server.

### Crafting Hammers

The **ReqVg** value is the success rate. The library will multiply the value by 100.

| ItemId | Effect | ReqVg |
|--------|--------|-------|
| 102074 | 102    | 5     |
| 102075 | 102    | 10    |

### Free Combination

This feature will not be implemented.

## Synergy

The library expects **SetItem.SData** to be in the **PSM_Client/Bin/Data** directory.

### Client Format

The client expects the file to be encrypted.

![Capture2](https://github.com/kurtekat/shaiya-episode-6/assets/142125482/01b93010-05a5-4323-b8d5-e890551ed4b7)

#### Bonus Description

`" Sinergia [5]\n- LUC +20\n- DEX +50\n- STR +70"`

### Server Format

The library expects the file to be decrypted. The bonus text is expected to be 12 comma-separated values.

![Capture](https://github.com/kurtekat/shaiya-episode-6/assets/142125482/a0a0c116-c5a0-4443-953e-35077e29f065)

The values are signed 32-bit integers, expected to be in the following order:

* strength
* dexterity
* intelligence
* wisdom
* reaction
* luck
* health
* mana
* stamina
* attackPower
* rangedAttackPower
* magicPower

## Item Ability Transfer

Use item `101150` directly from the inventory. This system does not use the client transfer window.

Inventory layout:

* slot 1 = source item
* slot 2 = transfer cube
* slot 3 = target item

When the cube is used, the transfer happens immediately. The source item loses its `CraftName` and `Gems`, and the target item receives them.

| ItemId | Effect |
|--------|--------|
| 101150 | 105    |

### Success Rate

The transfer is always successful. Catalysts are not used.

## Perfect Lapisian

The vanilla items define a perfect success rate (10000).

```cpp
auto materialCount = 10;
auto reqRec = 10000;

auto rate = reqRec * 100; // 1000000
rate += materialCount;    // 1000010
```

If `ReqRec` is zero, the game service will get the rate from `g_LapisianEnchantSuccessRate`.

### Configuration

| Column         | Value    | Description               |
|----------------|----------|---------------------------|
| Level          | 0:1      | Can use with Weapons      |
| Country        | 0:1      | Can use with Helmets      |
| AttackFighter  | 0:1      | Can use with Upper Armor  |
| DefenseFighter | 0:1      | Can use with Lower Armor  |
| PatrolRogue    | 0:1      | Can use with Shields      |
| ShootRogue     | 0:1      | Can use with Gloves       |
| AttackMage     | 0:1      | Can use with Boots        |
| ReqRec         | 0:10000  | Success rate              |
| ReqVg          | 0:1      | Needs item protection     |

## Item Effects

### Safety Charms

| ItemId | Effect |
|--------|--------|
| 101090 | 103    |
| 101132 | 103    |

### Town Move Scrolls

Town move scrolls now use the item `ReqVg` value as the `NpcTypeID` of the
gatekeeper in map `2`.

| Effect | ReqVg |
|--------|-------|
| 104    | GateKeeper NpcTypeID |

# Notes

## Warehouse Items

I received several reports over the course of this project about missing warehouse items. I just want to provide evidence that I believe will absolve me of this shit.

### Research

People who reported the issue had a teleport feature that sends invalid `0x50A` packets to the server.

```
SEND >> 0A 05 06 01
SEND >> 0A 05 06 02
SEND >> 0A 05 06 03
SEND >> 0A 05 06 04
SEND >> 0A 05 06 05
SEND >> 0A 05 06 06
SEND >> 0A 05 06 07
SEND >> 0A 05 06 08
SEND >> 0A 05 06 09
SEND >> 0A 05 06 0A
SEND >> 0A 05 06 0B
SEND >> 0A 05 06 0C
SEND >> 0A 05 06 0D
SEND >> 0A 05 06 0E
SEND >> 0A 05 06 0F
SEND >> 0A 05 06 10
```

The bag value is 6, which is an out-of-range subscript.

```cpp
Array<Array<CItem*, 24>, 6> inventory;  //0x1C0
Array<CItem*, 240> warehouse;           //0x400
```

The `CUser` memory alignment:

```
                   0x1C0 -> inventory[0][0]
(24 * 4) + 0x1C0 = 0x220 -> inventory[1][0]
(24 * 4) + 0x220 = 0x280 -> inventory[2][0]
(24 * 4) + 0x280 = 0x2E0 -> inventory[3][0]
(24 * 4) + 0x2E0 = 0x340 -> inventory[4][0]
(24 * 4) + 0x340 = 0x3A0 -> inventory[5][0]
(24 * 4) + 0x3A0 = 0x400 -> warehouse[0]
```

So...

```
inventory[6][0] = 0x400 -> warehouse[0]
inventory[6][1] = 0x404 -> warehouse[1]
inventory[6][2] = 0x408 -> warehouse[2]
inventory[6][3] = 0x40C -> warehouse[3]
inventory[6][4] = 0x410 -> warehouse[4]
inventory[6][5] = 0x414 -> warehouse[5]
...
```

I don't think I need to explain further.

## File Operations

I think the reason for this code is unclear.

```cpp
std::wstring buffer(INT16_MAX, 0);
if (!GetModuleFileNameW(nullptr, buffer.data(), INT16_MAX))
    return;

std::filesystem::path path(buffer);
path.remove_filename();
...
```

Once upon a time, I told people to inject the libraries with an application. For whatever reason, some decided to import the libraries, which, in turn, bugged all the file operations.

### Research

This is what happens when `std::filesystem::current_path` is called before the current working directory is resolved.

```cpp
auto path = std::filesystem::current_path();
// result: "C:\\WINDOWS\\system32"
```

It should be obvious that a relative path is also not an option.

### INT16_MAX

I looked at the machine code for `GetModuleFileNameW` because I couldn't find a clear answer regarding the `MAX_PATH` limitation.

```
KERNELBASE.GetModuleFileNameW+19 - B8 FF7F0000 - mov eax,00007FFF { 32767 }
KERNELBASE.GetModuleFileNameW+1E - 3B F0       - cmp esi,eax
KERNELBASE.GetModuleFileNameW+20 - 77 6A       - ja KERNELBASE.GetModuleFileNameW+8C
...
KERNELBASE.GetModuleFileNameW+8C - 8B F0       - mov esi,eax
KERNELBASE.GetModuleFileNameW+8E - EB 96       - jmp KERNELBASE.GetModuleFileNameW+26
```

I'll just pass the max size and be done with it.

## Cooldowns

1000 ticks = 1s (about)

### Item

| ReqIg | Ticks    |
|-------|----------|
| 0     | 0        |
| 1     | 15000    |
| 2     | 20000    |
| 3     | 25000    |
| 4     | 30000    |
| 5     | 60000    |
| 6     | 120000   |
| 7     | 0        |
| 8     | 0        |
| 9     | 0        |
| 10    | 600000   |
| 11    | 2000     |

### Monster

| AttackSpecial3 | Ticks     |
|----------------|-----------|
| 0              | 5000      |
| 1              | 30000     |
| 2              | 60000     |
| 3              | 180000    |
| 4              | 300000    |
| 5              | 900000    |
| 6              | 1800000   |
| 7              | 3600000   |
| 8              | 14400000  |
| 9              | 43200000  |
| 10             | 86400000  |
| 11             | 259200000 |
| 12             | 7200000   |
| 13             | 0         |
| 14             | 604800000 |
| 15             | 15000     |

## Lua

### Parameters

The developers used Hungarian notation to express the data type of parameters.

### Event Handlers

```lua
OnAttacked(dwTime, dwCharId)
OnAttackable(dwTime, dwCharId)
OnNormalReset(dwTime)
OnDeath(dwTime, dwAttackedCount)
OnReturnHome(dwTime, dwAttackedCount)
OnMoveEnd(dwTime)
```

### Functions

```lua
Init()
WhileCombat(dwTime, dwHPPercent, dwAttackedCount)
```

### Methods

```lua
Mob:LuaGetNumberAggro(dwNumber)
Mob:LuaGetMinAggroEx()
Mob:LuaAttackOrderSurroundMob(dwCharId, dlPosX, dlPosZ)
Mob:LuaSay(szMessage, fDist)
Mob:LuaSayByIndex(wIndex, fDist)
Mob:LuaSayByVoice(szFileName, fDist)
Mob:LuaGetName(dwCharId)
Mob:LuaCreateMob(dwMobId, nCount, dlPosX, dlPosZ)
Mob:LuaAttackAI(dwSkillId, dwCharId)
Mob:LuaSetTarget(dwCharId)
Mob:LuaResetTargetingTerm(dwTerm)
Mob:LuaSetOnlyAttack(bLuaAttack)
Mob:LuaHoldPosion(bHoldPosion)
Mob:LuaGetMobCountByType(dwType, dlPosX, dlPosZ, nAddDist)
Mob:LuaSetUnBeatable(bUnBeatable)
Mob:LuaResetAggro()
Mob:LuaDeleteMob(dwMobId, byCount, dlPosX, dlPosZ)
Mob:LuaFixedMove(nMoveMode, dlPosX, dlPosZ)
Mob:LuaRecallUser(byJob, fDist, wMap, dlPosX, dlPosY, dlPosZ)
Mob:LuaRecover(byType, dlValue)
Mob:LuaGetMobPos(dwId, fPosX, fPosZ)
Mob:LuaGetMobHP(dwId)
Mob:LuaUpdateInZonePortal(nPortalId, nCountry)
```

### Example

```lua
Mob = LuaMob(CMob)
math.randomseed(os.time())

bOnAttacked = 0
dwNextCreateTime = 0
bMobSay	= 0
bMobCreate = 0

function Init()
end

function OnAttacked(dwTime, dwCharId)
    if (bOnAttacked == 0) then
        szCharName = Mob:LuaGetName(dwCharId)
        Mob:LuaSay(szCharName..", you are a fool to challenge me!", 100.0)	
        bOnAttacked = 1
    end
end

function OnAttackable(dwTime, dwCharId)
end

function OnNormalReset(dwTime)
end

function OnDeath(dwTime, dwAttackedCount)
    Mob:LuaSay("I will have my revenge!", 100.0)
end

function OnReturnHome(dwTime, dwAttackedCount)
    Mob:LuaSay("You will not defeat me!", 100.0)

    bOnAttacked = 0
    dwNextCreateTime = 0
    bMobSay = 0
    bMobCreate = 0
end

function OnMoveEnd(dwTime)
end

function WhileCombat(dwTime, dwHPPercent, dwAttackedCount)
    Mob:LuaRecover(1, 0.1)

    if (dwHPPercent > 20) then
        nResult = math.random(1, 20)

        if (nResult == 1) then		
            byJob = math.random(0, 5)
            Mob:LuaSay("Leave my presence at once!", 30.0)		
            Mob:LuaResetAggro()
            Mob:LuaRecallUser(byJob, 30.0, 73, 40.077, 4.683, 59.303)
        end
    end
end
```
