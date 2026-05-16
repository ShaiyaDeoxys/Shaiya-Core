#include <cmath>
#include <util/util.h>
#include <shaiya/include/network/game/incoming/0500.h>
#include <shaiya/include/network/game/outgoing/0200.h>
#include "include/main.h"
#include "include/etain_shield_config.h"
#include "include/shaiya/CObject.h"
#include "include/shaiya/CMob.h"
#include "include/shaiya/CUser.h"
#include "include/shaiya/CZone.h"
#include "include/shaiya/MobInfo.h"
#include "include/shaiya/NetworkHelper.h"
#include "include/shaiya/SVector.h"
using namespace shaiya;

// ===========================================================================
//  EtainShield — Server-side anticheat module for ps_game.exe
// ===========================================================================
//
//  All protections are configurable via Data/EtainShield.ini (see config header).
//
//  PROTECTIONS
//  -----------
//  AntiSpeedHack   — Constant patching + per-packet movement speed validation
//  AntiRangeHack   — Euclidean distance checks for attacks and skills
//  AntiMoveAttack  — Block movement while the server considers the player attacking
//
//  ARCHITECTURE
//  ------------
//  - This file contains the C++ validation logic and the naked assembly detours.
//  - Packet-level hooks (0x501/0x502/0x503 dispatch) live in packet_pc.cpp.
//  - Per-user state fields live in CUser.h (etainLastPos, etainAttackLock*, etc.).
//  - The single entry point hook::etain_shield() installs all memory patches
//    and detours; it is called once from Main().
//
//  ADDRESS NOTATION
//  ----------------
//  ps_game.exe base = 0x400000.  CE notation "ps_game.exe+OFFSET" maps to
//  absolute VA = 0x400000 + OFFSET.
// ===========================================================================

namespace etain_shield
{
    // ===================================================================
    //  Helpers
    // ===================================================================

    /// Euclidean 2D ground distance between two positions.
    static float distance_2d(const SVector* a, const SVector* b)
    {
        float dx = a->x - b->x;
        float dz = a->z - b->z;
        return std::sqrt(dx * dx + dz * dz);
    }

    /// Read the server-side position from a CUser (CObject.pos at offset 0xD0).
    static SVector* user_pos(CUser* user)
    {
        return reinterpret_cast<SVector*>(reinterpret_cast<char*>(user) + 0xD0);
    }

    /// Read the server-side position from a CMob (CObject.pos at offset 0x7C).
    static SVector* mob_pos(CMob* mob)
    {
        return reinterpret_cast<SVector*>(reinterpret_cast<char*>(mob) + 0x7C);
    }

    /// Send a position correction packet to a single client.
    static void send_pos_correction(CUser* user, const SVector& pos)
    {
        GameUserSetMapPosOutgoing pkt{};
        pkt.objectId = user->id;
        pkt.mapId    = user->mapId;
        pkt.x        = pos.x;
        pkt.y        = pos.y;
        pkt.z        = pos.z;
        NetworkHelper::Send(user, &pkt, sizeof(pkt));
    }

    // ===================================================================
    //  AntiSpeedHack — Constant Patching
    // ===================================================================
    //
    //  Overwrites four timing constants in the ps_game.exe data section
    //  to tighten the native speed-validation window.
    //
    //  Const1 (double @ 0x5740D8) — max time-delta threshold
    //  Const2 (float  @ 0x5740E4) — per-tick timing tolerance
    //  Const3 (float  @ 0x5740D0) — timing multiplier
    //  Const4 (double @ 0x5740C8) — timing accumulator addend

    static void apply_speedhack_constants()
    {
        auto& c = g_etainConfig;

        double c1 = c.speedConst1;
        util::write_memory((void*)0x5740D8, &c1, sizeof(c1));

        float c2 = c.speedConst2;
        util::write_memory((void*)0x5740E4, &c2, sizeof(c2));

        float c3 = c.speedConst3;
        util::write_memory((void*)0x5740D0, &c3, sizeof(c3));

        double c4 = c.speedConst4;
        util::write_memory((void*)0x5740C8, &c4, sizeof(c4));
    }

    // ===================================================================
    //  AntiMoveAttack — Attack Movement Lock
    // ===================================================================
    //
    //  Uses the server's own CUser::attackType field (0x1458) to know
    //  whether the player is currently in an attack.  While attackType is
    //  Basic or Skill AND we hold a lock snapshot, incoming 0x501 movement
    //  packets are silently dropped.  A safety timeout (5 s) prevents
    //  permanent freezes if the attack state gets stuck.
    //
    //  Lock lifecycle:
    //    1. lock_movement_for_attack() — called when an attack passes validation
    //    2. is_lock_active()           — queried per 0x501 packet
    //    3. Lock clears when attackType==None OR timeout expires

    static constexpr DWORD kLockTimeoutMs = 5000;

    /// Returns true if the lock is still active (movement should be blocked).
    /// If the lock has expired, clears it and sends a correction if needed.
    static bool is_lock_active(CUser* user, DWORD now)
    {
        if (user->etainAttackLockTick == 0)
            return false;

        bool timedOut   = (now - user->etainAttackLockTick) >= kLockTimeoutMs;
        bool attackDone = (user->attackType == UserAttackType::None);

        if (!timedOut && !attackDone)
            return true;

        // Lock ended — clean up.
        user->etainAttackLockTick = 0;

        if (user->etainAttackLockDirty)
        {
            user->etainAttackLockDirty = false;
            send_pos_correction(user, user->etainAttackLockPos);

            // Sync speed-hack tracking so Protection 2 doesn't false-positive.
            user->etainLastPos          = user->etainAttackLockPos;
            user->etainLastMoveTick     = now;
            user->etainViolationCount   = 0;
        }

        return false;
    }

    void lock_movement_for_attack(CUser* user)
    {
        if (!g_etainConfig.enabled || !g_etainConfig.moveAttackEnabled)
            return;

        auto now = GetTickCount();

        // If a stale lock exists, let is_lock_active() clear it first.
        if (user->etainAttackLockTick != 0)
        {
            if (is_lock_active(user, now))
                return;  // legitimately locked — don't re-snapshot
        }

        user->etainAttackLockTick  = now;
        user->etainAttackLockDirty = false;
        user->etainAttackLockPos   = *user_pos(user);
    }

    // ===================================================================
    //  AntiSpeedHack — Active Movement Validation  +  AntiMoveAttack
    // ===================================================================
    //
    //  validate_movement() is the single entry point for every 0x501
    //  packet.  It runs both the AntiMoveAttack check and the speed
    //  validation in sequence.

    bool validate_movement(CUser* user, GameCharMoveIncoming* packet)
    {
        if (!g_etainConfig.enabled)
            return true;

        auto now = GetTickCount();

        // --- AntiMoveAttack ---
        if (g_etainConfig.moveAttackEnabled && user->etainAttackLockTick != 0)
        {
            if (is_lock_active(user, now))
            {
                user->etainAttackLockDirty = true;
                return false;
            }
        }

        // --- AntiSpeedHack ---
        if (!g_etainConfig.speedHackEnabled)
            return true;

        auto& c = g_etainConfig;

        // First packet after login / teleport — seed tracking state.
        if (user->etainLastMoveTick == 0)
        {
            user->etainLastPos       = { packet->x, packet->y, packet->z };
            user->etainLastMoveTick  = now;
            user->etainViolationCount = 0;
            return true;
        }

        auto elapsed = now - user->etainLastMoveTick;

        // Too soon — delta too small for meaningful math.
        if (elapsed < c.speedMinTickDelta)
            return true;

        float dx       = packet->x - user->etainLastPos.x;
        float dz       = packet->z - user->etainLastPos.z;
        float distance = std::sqrt(dx * dx + dz * dz);

        // Likely a legitimate teleport — reset and allow.
        if (distance > c.speedTeleportThreshold)
        {
            user->etainLastPos       = { packet->x, packet->y, packet->z };
            user->etainLastMoveTick  = now;
            user->etainViolationCount = 0;
            return true;
        }

        auto speed = user->abilityMoveSpeed;
        if (speed < 1) speed = 1;

        float maxDist = (static_cast<float>(speed) *
                         (static_cast<float>(elapsed) / 1000.0f) *
                         c.speedTolerance)
                        + c.speedFreeDistance;

        if (distance <= maxDist)
        {
            user->etainLastPos       = { packet->x, packet->y, packet->z };
            user->etainLastMoveTick  = now;
            if (user->etainViolationCount > 0)
                --user->etainViolationCount;
            return true;
        }

        // --- Violation ---
        ++user->etainViolationCount;

        if (user->etainViolationCount >= c.speedViolationThreshold)
        {
            if (user->zone)
                CZone::MoveUser(user->zone, user,
                    user->etainLastPos.x, user->etainLastPos.y, user->etainLastPos.z);

            send_pos_correction(user, user->etainLastPos);

            user->etainViolationCount = 0;
            user->etainLastMoveTick   = now;
            return false;
        }

        user->etainLastMoveTick = now;
        return true;
    }

    void reset_tracking(CUser* user)
    {
        user->etainLastPos          = {};
        user->etainLastMoveTick     = 0;
        user->etainViolationCount   = 0;
        user->etainAttackLockTick   = 0;
        user->etainAttackLockDirty  = false;
    }

    // ===================================================================
    //  AntiRangeHack — Attack Distance Validation
    // ===================================================================
    //
    //  Two layers:
    //    A) Packet interception (0x502 / 0x503) — basic attacks only
    //    B) Native function detours (0x458000 / 0x457F50) — all attacks + skills
    //
    //  Both layers compute the real 2D euclidean distance from server-side
    //  positions and compare against:
    //    allowed = max(abilityAttackRange, skillRange) + targetSize + margin

    int validate_pve_range(CUser* user, CMob* mob, int skillRange)
    {
        if (!g_etainConfig.enabled || !g_etainConfig.rangeHackEnabled)
            return 1;

        if (!user || !mob || !mob->info)
            return 0;

        float dist = distance_2d(user_pos(user), mob_pos(mob));

        int range = user->abilityAttackRange;
        if (skillRange > range) range = skillRange;

        float allowed = static_cast<float>(
            range + static_cast<int>(mob->info->size) + g_etainConfig.rangeMargin);

        return (dist <= allowed) ? 1 : 0;
    }

    int validate_pvp_range(CUser* attacker, CUser* target, int skillRange)
    {
        if (!g_etainConfig.enabled || !g_etainConfig.rangeHackEnabled)
            return 1;

        if (!attacker || !target)
            return 0;

        float dist = distance_2d(user_pos(attacker), user_pos(target));

        int range = attacker->abilityAttackRange;
        if (skillRange > range) range = skillRange;

        float allowed = static_cast<float>(range + 1 + g_etainConfig.rangeMargin);

        return (dist <= allowed) ? 1 : 0;
    }

    int validate_attack_user(CUser* user, GameCharAttackUserIncoming* packet)
    {
        if (!g_etainConfig.enabled || !g_etainConfig.rangeHackEnabled)
            return 1;

        if (!user || !user->zone)
            return 1;

        auto* target = CZone::FindUser(user->zone, packet->targetId);
        if (!target)
            return 1;

        float dist = distance_2d(user_pos(user), user_pos(target));
        float allowed = static_cast<float>(
            user->abilityAttackRange + 1 + g_etainConfig.rangeMargin);

        return (dist <= allowed) ? 1 : 0;
    }

    int validate_attack_mob(CUser* user, GameCharAttackMobIncoming* packet)
    {
        if (!g_etainConfig.enabled || !g_etainConfig.rangeHackEnabled)
            return 1;

        if (!user || !user->zone)
            return 1;

        auto* mob = CZone::FindMob(user->zone, packet->targetId);
        if (!mob || !mob->info)
            return 1;

        float dist = distance_2d(user_pos(user), mob_pos(mob));
        float allowed = static_cast<float>(
            user->abilityAttackRange + static_cast<int>(mob->info->size) +
            g_etainConfig.rangeMargin);

        return (dist <= allowed) ? 1 : 0;
    }
}

// ===========================================================================
//  Exported function — backward compatibility with RangeHack.CT
// ===========================================================================

extern "C" __declspec(dllexport)
bool __cdecl EnableAttackRange(
    shaiya::CUser* user, shaiya::SVector* targetPos,
    int targetSize, int skillRange, int margin)
{
    int range = user->abilityAttackRange;
    if (skillRange > range) range = skillRange;

    float dist = etain_shield::distance_2d(
        etain_shield::user_pos(user), targetPos);

    return dist <= static_cast<float>(range + targetSize + margin);
}

// ===========================================================================
//  Naked assembly detours — AntiRangeHack Layer B + AntiMoveAttack lock
// ===========================================================================
//
//  Each detour replaces the prologue of a native range-check function.
//  On pass: call lock_movement_for_attack(), restore original prologue, jump in.
//  On fail: return al=0 immediately.

// --- PVE: 0x458000 ---
// Registers: ebx = user.  Stack: [esp+4] = mob, [esp+8] = skillRange.
// Returns al.  retn 0x08.
// Original 8 bytes: sub esp,0x10 / push ebp / mov ebp,[esp+0x18]
unsigned u0x458008 = 0x458008;
void __declspec(naked) naked_0x458000()
{
    __asm
    {
        pushad
        mov eax,[esp+0x28]      // skillRange (stack, after pushad)
        mov edx,[esp+0x24]      // mob        (stack, after pushad)
        push eax
        push edx
        push ebx                // user
        call etain_shield::validate_pve_range
        add esp,0xC
        test eax,eax
        popad
        jz blocked

        pushad
        push ebx
        call etain_shield::lock_movement_for_attack
        add esp,0x4
        popad

        sub esp,0x10
        push ebp
        mov ebp,[esp+0x18]
        jmp u0x458008

    blocked:
        xor al,al
        retn 0x08
    }
}

// --- PVP: 0x457F50 ---
// Registers: edi = attacker, ebx = target, ecx = skillRange.
// Returns al.  retn.
// Original 9 bytes: sub esp,0x18 / mov eax,[edi+0x12F0]
unsigned u0x457F59 = 0x457F59;
void __declspec(naked) naked_0x457F50()
{
    __asm
    {
        pushad
        push ecx                // skillRange
        push ebx                // target
        push edi                // attacker
        call etain_shield::validate_pvp_range
        add esp,0xC
        test eax,eax
        popad
        jz blocked

        pushad
        push edi
        call etain_shield::lock_movement_for_attack
        add esp,0x4
        popad

        sub esp,0x18
        mov eax,[edi+0x12F0]
        jmp u0x457F59

    blocked:
        xor al,al
        retn
    }
}

// ===========================================================================
//  hook::etain_shield() — public entry point, called once from Main()
// ===========================================================================

void hook::etain_shield()
{
    if (!g_etainConfig.enabled)
        return;

    // AntiSpeedHack: patch timing constants.
    if (g_etainConfig.speedHackEnabled)
        etain_shield::apply_speedhack_constants();

    // AntiRangeHack: detour native range-check functions.
    if (g_etainConfig.rangeHackEnabled)
    {
        util::detour((void*)0x458000, naked_0x458000, 8);
        util::detour((void*)0x457F50, naked_0x457F50, 9);
    }

    // AntiSpeedHack active validation and AntiMoveAttack are checked at
    // runtime inside validate_movement() — their hooks live in packet_pc.cpp
    // (0x501 / 0x502 / 0x503 dispatch).
}
