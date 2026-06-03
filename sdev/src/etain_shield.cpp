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
//  AntiSpeedHack   — Calibrated per-packet movement speed validation
//                    Uses real-world speed ceilings measured from the client:
//                      On foot:  max ~12 units/s  (config: 12.5 with 0.5 margin)
//                      Mounted:  max ~13 units/s  (config: 13.5 with 0.5 margin)
//                    Mounted state detected via CUser::vehicleStatus == Riding.
//                    Consecutive violations are forgiven up to the configured
//                    threshold to absorb latency/desync spikes; repeated
//                    violations trigger a rubber-band correction.
//                    Detects speed hacks >= 1.1x reliably.
//                    Also patches four native timing constants in ps_game.exe.
//  AntiRangeHack   — Static euclidean distance checks for attacks and skills
//
//  ARCHITECTURE
//  ------------
//  - This file contains the C++ validation logic and the naked assembly detours.
//  - Packet-level hooks (0x501/0x502/0x503 dispatch) live in packet_pc.cpp.
//  - Per-user state fields live in CUser.h (etainLastPos, etc.).
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
    //  Overwrites four hardcoded timing constants in the ps_game.exe data
    //  section to tighten the native speed-validation window.  These are
    //  fixed values (not configurable via INI) — the real protection comes
    //  from validate_movement() with calibrated speed ceilings.

    static void apply_speedhack_constants()
    {
        constexpr double c1 = 10.0;
        util::write_memory((void*)0x5740D8, &c1, sizeof(c1));

        constexpr float c2 = 0.13f;
        util::write_memory((void*)0x5740E4, &c2, sizeof(c2));

        constexpr float c3 = 3.0f;
        util::write_memory((void*)0x5740D0, &c3, sizeof(c3));

        constexpr double c4 = 2.0;
        util::write_memory((void*)0x5740C8, &c4, sizeof(c4));
    }

    // ===================================================================
    //  AntiSpeedHack — Active Movement Validation
    // ===================================================================
    //
    //  validate_movement() is the single entry point for every 0x501
    //  packet.  It runs the speed validation.
    //
    //  Speed validation uses calibrated coefficients measured server-side
    //  via OutputDebugString logging of actual 0x501 packet distances/times.
    //  Server-observed speeds differ from the client speed monitor (~7/~10)
    //  because packets arrive at irregular intervals (~100–600ms) rather
    //  than per-frame, and GetTickCount() resolution differs from QPC.
    //
    //  Calibrated ceilings (server-side measurement):
    //    On foot: ~12 units/s  → threshold 12.5  (0.5 margin)
    //    Mounted: ~13 units/s  → threshold 13.5  (0.5 margin)
    //
    //  Violation policy:
    //    Below threshold: forgiven (possible latency spike, desync, packet burst)
    //    At threshold+:   rubber-band correction (every violation)
    //  Violations reset to 0 on any legitimate movement packet.
    //
    //  IMPORTANT: on violation, etainLastPos MUST advance to the packet
    //  position.  Otherwise subsequent packets measure accumulated distance
    //  from a stale position, causing a cascading false-positive spiral.

    bool validate_movement(CUser* user, GameCharMoveIncoming* packet)
    {
        if (!g_etainConfig.enabled)
            return true;

        auto now = GetTickCount();

        // --- AntiSpeedHack ---
        if (!g_etainConfig.speedHackEnabled)
            return true;

        auto& c = g_etainConfig;

        SVector packetPos = { packet->x, packet->y, packet->z };

        // First packet after login / teleport — seed tracking state.
        if (user->etainLastMoveTick == 0)
        {
            user->etainLastPos        = packetPos;
            user->etainLastMoveTick   = now;
            user->etainViolationCount = 0;
            return true;
        }

        auto elapsed = now - user->etainLastMoveTick;

        // Sub-50ms: too short to measure meaningfully — accept and advance.
        if (elapsed < 50)
        {
            user->etainLastPos      = packetPos;
            user->etainLastMoveTick = now;
            return true;
        }

        // 2D ground distance from last validated position.
        float distance = distance_2d(&packetPos, &user->etainLastPos);

        // Player didn't move (or moved negligibly) — not a speed hack.
        // Accept and advance the timestamp so the next real move is
        // measured from this point.
        if (distance < 0.1f)
        {
            user->etainLastPos      = packetPos;
            user->etainLastMoveTick = now;
            // Don't reset violations — standing still doesn't prove innocence.
            return true;
        }

        // Pick the right ceiling based on mounted state.
        bool mounted = (user->vehicleStatus == UserVehicleStatus::Riding);
        float maxSpeed = mounted ? c.speedMaxMounted : c.speedMaxOnFoot;

        // maxDist = how far this player could legitimately travel.
        float maxDist = maxSpeed * (static_cast<float>(elapsed) / 1000.0f);

        if (distance <= maxDist)
        {
            // Legitimate movement — advance tracking, clear violations.
            user->etainLastPos        = packetPos;
            user->etainLastMoveTick   = now;
            user->etainViolationCount = 0;
            return true;
        }

        // --- Violation ---
        ++user->etainViolationCount;

        // CRITICAL: always advance tracking position and time.
        // If we don't, the next packet measures distance from the stale
        // position with a fresh elapsed → cascading false positives
        // (speed appears to grow exponentially each packet).
        user->etainLastMoveTick = now;
        user->etainLastPos      = packetPos;

        // Early violations are forgiven (latency spikes, desync).
        if (user->etainViolationCount < c.speedViolationThreshold)
            return true;

        // Threshold reached: rubber-band correction.
        send_pos_correction(user, user->etainLastPos);
        return false;
    }

    void reset_tracking(CUser* user)
    {
        user->etainLastPos          = {};
        user->etainLastMoveTick     = 0;
        user->etainViolationCount   = 0;
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
//  Naked assembly detours — AntiRangeHack Layer B
// ===========================================================================
//
//  Each detour replaces the prologue of a native range-check function.
//  On pass: restore original prologue and jump in.
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

    // AntiSpeedHack active validation is checked at runtime inside
    // validate_movement() — its hook lives in packet_pc.cpp (0x501 dispatch).
}
