#pragma once

namespace shaiya
{
    struct CMob;
    struct CUser;
    struct GameCharMoveIncoming;
    struct GameCharAttackUserIncoming;
    struct GameCharAttackMobIncoming;
}

namespace etain_shield
{
    // --- AntiSpeedHack ---

    /// Validates a 0x501 movement packet (speed + move-while-attack checks).
    /// Returns true to allow, false to drop.
    bool validate_movement(shaiya::CUser* user, shaiya::GameCharMoveIncoming* packet);

    /// Resets all per-user tracking state.
    /// Call on teleport, zone change, or any server-initiated position change.
    void reset_tracking(shaiya::CUser* user);

    // --- AntiRangeHack ---

    /// PVE range pre-check (0x458000 detour).  Returns 1=allow, 0=block.
    int validate_pve_range(shaiya::CUser* user, shaiya::CMob* mob, int skillRange);

    /// PVP range pre-check (0x457F50 detour).  Returns 1=allow, 0=block.
    int validate_pvp_range(shaiya::CUser* attacker, shaiya::CUser* target, int skillRange);

    /// Basic attack on player (0x502 packet).  Returns 1=allow, 0=drop.
    int validate_attack_user(shaiya::CUser* user, shaiya::GameCharAttackUserIncoming* packet);

    /// Basic attack on mob (0x503 packet).  Returns 1=allow, 0=drop.
    int validate_attack_mob(shaiya::CUser* user, shaiya::GameCharAttackMobIncoming* packet);

    // --- AntiCutting ---

    /// Snapshots position and activates the movement lock for this attack.
    /// The player cannot move for cuttingLockMs after each attack; every
    /// 0x501 packet during the lock window is dropped and a position
    /// correction is sent immediately.  Configurable skip-list exempts
    /// dash-type skills.
    void lock_movement_for_attack(shaiya::CUser* user);
}
