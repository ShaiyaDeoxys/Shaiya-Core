#include <util/util.h>
#include <shaiya/include/common/ItemEffect.h>
#include <shaiya/include/common/ItemSlot.h>
#include <shaiya/include/network/game/incoming/0500.h>
#include <shaiya/include/network/game/outgoing/0200.h>
#include "include/main.h"
#include "include/etain_shield.h"
#include "include/shaiya/CItem.h"
#include "include/shaiya/CObject.h"
#include "include/shaiya/CUser.h"
#include "include/shaiya/CWorld.h"
#include "include/shaiya/CZone.h"
#include "include/shaiya/ItemInfo.h"
#include "include/shaiya/NetworkHelper.h"
#include "include/shaiya/UserHelper.h"
using namespace shaiya;

namespace packet_pc
{
    bool move_to_pending_position(CUser* user, bool& changedMap)
    {
        changedMap = user->mapId != user->moveMapId;

        if (user->mapId != user->moveMapId)
        {
            CWorld::ZoneLeaveUserMove(user, user->moveMapId, user->movePos.x, user->movePos.y, user->movePos.z);

            GameUserSetMapPosOutgoing outgoing{};
            outgoing.objectId = user->id;
            outgoing.mapId = user->moveMapId;
            outgoing.x = user->movePos.x;
            outgoing.y = user->movePos.y;
            outgoing.z = user->movePos.z;
            NetworkHelper::Send(user, &outgoing, sizeof(GameUserSetMapPosOutgoing));

            // EtainShield: reset speed tracking after zone change
            etain_shield::reset_tracking(user);
            return true;
        }

        if (!user->zone)
            return false;

        if (!CZone::MoveUser(user->zone, user, user->movePos.x, user->movePos.y, user->movePos.z))
            return false;

        GameUserSetMapPosOutgoing outgoing{};
        outgoing.objectId = user->id;
        outgoing.mapId = user->moveMapId;
        outgoing.x = user->movePos.x;
        outgoing.y = user->movePos.y;
        outgoing.z = user->movePos.z;
        CObject::SendView(user, &outgoing, sizeof(GameUserSetMapPosOutgoing));

        // EtainShield: reset speed tracking after teleport
        etain_shield::reset_tracking(user);
        return true;
    }

    /// <summary>
    /// Handles incoming 0x55A packets.
    /// </summary>
    void handler_0x55A(CUser* user, GameTownMoveScrollIncoming* incoming)
    {
        if (user->status == UserStatus::Death)
            return;

        if (!incoming->bag || incoming->bag > user->bagsUnlocked)
            return;

        if (incoming->slot >= ItemSlotCount)
            return;

        auto& item = user->inventory[incoming->bag][incoming->slot];
        if (!item)
            return;

        if (item->info->effect != ItemEffect::TownMoveScroll)
            return;

        if (incoming->gateIndex > 2)
            return;

        user->savePosUseBag = incoming->bag;
        user->savePosUseSlot = incoming->slot;
        user->savePosUseIndex = incoming->gateIndex;

        CUser::CancelActionExc(user);
        MyShop::Ended(&user->myShop);
        CUser::ItemUse(user, incoming->bag, incoming->slot, user->id, 0);
    }

    void town_move_scroll_hook(CUser* user)
    {
        // Battleground button movement reuses the same 5s cast/update path as
        // town scrolls, but it is not backed by a consumable item.
        if (!user->savePosUseBag && !user->savePosUseSlot)
        {
            bool changedMap = false;
            move_to_pending_position(user, changedMap);
            return;
        }

        auto& item = user->inventory[user->savePosUseBag][user->savePosUseSlot];
        if (!item)
            return;

        if (item->info->realType != RealType::Consumable)
            return;

        if (item->info->effect != ItemEffect::TownMoveScroll)
            return;

        bool changedMap = false;
        if (move_to_pending_position(user, changedMap))
            CUser::ItemUseNSend(user, user->savePosUseBag, user->savePosUseSlot, changedMap);
    }

    /// <summary>
    /// EtainShield: validates 0x501 movement packet before the native handler.
    /// Returns 1 (allow) or 0 (drop).
    /// ecx = user, ebp = packet at call site.
    /// </summary>
    int validate_move_packet(CUser* user, GameCharMoveIncoming* packet)
    {
        return etain_shield::validate_movement(user, packet) ? 1 : 0;
    }

    /// <summary>
    /// EtainShield: validates 0x502 basic attack on player.
    /// Returns 1 (allow) or 0 (drop).
    /// </summary>
    int validate_attack_user_packet(CUser* user, GameCharAttackUserIncoming* packet)
    {
        int result = etain_shield::validate_attack_user(user, packet);
        if (result)
            etain_shield::lock_movement_for_attack(user);
        return result;
    }

    /// <summary>
    /// EtainShield: validates 0x503 basic attack on mob.
    /// Returns 1 (allow) or 0 (drop).
    /// </summary>
    int validate_attack_mob_packet(CUser* user, GameCharAttackMobIncoming* packet)
    {
        int result = etain_shield::validate_attack_mob(user, packet);
        if (result)
            etain_shield::lock_movement_for_attack(user);
        return result;
    }
}

unsigned u0x4784DB = 0x4784DB;
unsigned u0x479155 = 0x479155;
void __declspec(naked) naked_0x4784D6()
{
    __asm
    {
        add eax,-0x501

        // EtainShield: intercept opcode 0x501 (movement) — eax == 0
        test eax,eax
        jz case_0x501

        // EtainShield: intercept 0x502 (attack user) — eax == 1
        cmp eax,0x1
        je case_0x502

        // EtainShield: intercept 0x503 (attack mob) — eax == 2
        cmp eax,0x2
        je case_0x503

        cmp eax,0x59
        je case_0x55A
        jmp u0x4784DB

        // --- 0x501: Movement ---
        case_0x501:
        pushad

        push ebp // packet (GameCharMoveIncoming*)
        push ecx // user (CUser*)
        call packet_pc::validate_move_packet
        add esp,0x8
        test eax,eax
        jz drop_0x501

        popad
        // Validation passed — continue to original 0x501 handler.
        xor eax,eax
        jmp u0x4784DB

        drop_0x501:
        popad
        jmp u0x479155

        // --- 0x502: Basic attack on player ---
        case_0x502:
        pushad

        push ebp // packet (GameCharAttackUserIncoming*)
        push ecx // user (CUser*)
        call packet_pc::validate_attack_user_packet
        add esp,0x8
        test eax,eax
        jz drop_0x502

        popad
        // Validation passed — restore eax=1 and continue to native handler.
        mov eax,0x1
        jmp u0x4784DB

        drop_0x502:
        popad
        // Out of range — drop the attack packet.
        jmp u0x479155

        // --- 0x503: Basic attack on mob ---
        case_0x503:
        pushad

        push ebp // packet (GameCharAttackMobIncoming*)
        push ecx // user (CUser*)
        call packet_pc::validate_attack_mob_packet
        add esp,0x8
        test eax,eax
        jz drop_0x503

        popad
        // Validation passed — restore eax=2 and continue to native handler.
        mov eax,0x2
        jmp u0x4784DB

        drop_0x503:
        popad
        // Out of range — drop the attack packet.
        jmp u0x479155

        // --- 0x55A: Town move scroll ---
        case_0x55A:
        pushad

        push ebp // packet
        push ecx // user
        call packet_pc::handler_0x55A
        add esp,0x8

        popad

        jmp u0x479155
    }
}

unsigned u0x49DDC8 = 0x49DDC8;
unsigned u0x49DEB5 = 0x49DEB5;
unsigned u0x49E8D1 = 0x49E8D1;
void __declspec(naked) naked_0x49DDBF()
{
    __asm
    {
        cmp eax,0x7
        je town_move_scroll

        // original
        cmp eax,0x1
        jne _0x49DEB5
        jmp u0x49DDC8

        town_move_scroll:
        pushad

        push edi // user
        call packet_pc::town_move_scroll_hook
        add esp,0x4

        popad

        jmp u0x49E8D1

        _0x49DEB5:
        jmp u0x49DEB5
    }
}

void hook::packet_pc()
{
    // CUser::PacketPC
    util::detour((void*)0x4784D6, naked_0x4784D6, 5);
    // CUser::UpdateResetPosition
    util::detour((void*)0x49DDBF, naked_0x49DDBF, 9);
}
