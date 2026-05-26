#include <algorithm>
#include <cstring>
#include <ranges>
#include <util/util.h>
#include <shaiya/include/network/TP_MAIN.h>
#include <shaiya/include/network/game/incoming/0200.h>
#include <shaiya/include/network/game/incoming/0800.h>
#include <shaiya/include/network/game/outgoing/0200.h>
#include <shaiya/include/network/game/outgoing/0800.h>
#include "include/main.h"
#include "include/shaiya/BattlefieldMoveInfo.h"
#include "include/shaiya/CObject.h"
#include "include/shaiya/CUser.h"
#include "include/shaiya/CWorld.h"
#include "include/shaiya/MyShop.h"
#include "include/shaiya/SConnection.h"
#include "include/shaiya/UserHelper.h"
using namespace shaiya;

namespace packet_main_interface
{
    void handler_0x233(CUser* user)
    {
        // Battleground buttons.
        // The client only requests the move; the server decides the valid
        // battlefield by level and family, then uses the same 5s cast movement
        // path as town move scrolls. Debuffs intentionally do not block this.
        if (user->status == UserStatus::Death)
            return;

        if (user->logoutTime)
            return;

        auto it = std::ranges::find_if(g_battlefieldMoveData, BattlefieldLvInRange(user->level));
        if (it == g_battlefieldMoveData.end())
            return;

        auto family = std::clamp(static_cast<int>(user->family), 0, 3);
        auto& pos = it->mapPos[family];
        if (user->mapId == pos.mapId)
            return;

        if (!CWorld::GetZone(pos.mapId))
            return;

        CUser::CancelActionExc(user);
        MyShop::Ended(&user->myShop);

        user->savePosUseBag = 0;
        user->savePosUseSlot = 0;
        user->savePosUseIndex = 0;
        UserHelper::SetMovePosition(user, pos.mapId, pos.x, pos.y, pos.z, UserMovePosType::TownMoveScroll, 5000);

        GameUserItemCastOutgoing outgoing{};
        outgoing.objectId = user->id;
        CObject::PSendViewCombat(user, &outgoing, sizeof(GameUserItemCastOutgoing));
    }

    void handler_0x838(CUser* user)
    {
        GameTeleportListOutgoing outgoing{};
        outgoing.count = 0;

        for (const auto& dest : g_teleportDestinations)
        {
            if (outgoing.count >= kTeleportMaxDestinations)
                break;

            auto& entry = outgoing.entries[outgoing.count];
            std::memset(&entry, 0, sizeof(entry));
            std::memcpy(entry.name, dest.name.c_str(), std::min(dest.name.size(), static_cast<size_t>(kTeleportNameLength - 1)));
            entry.levelMin = static_cast<uint16_t>(dest.levelMin);
            entry.levelMax = static_cast<uint16_t>(dest.levelMax);

            for (int f = 0; f < kTeleportFactionCount; ++f)
            {
                entry.factionPos[f].mapId = static_cast<uint16_t>(dest.factionData[f].mapId);
                entry.factionPos[f].x = dest.factionData[f].x;
                entry.factionPos[f].y = dest.factionData[f].y;
                entry.factionPos[f].z = dest.factionData[f].z;
            }

            ++outgoing.count;
        }

        // Send only the bytes actually used: header + count + entries
        auto size = offsetof(GameTeleportListOutgoing, entries)
            + outgoing.count * sizeof(TeleportDestinationEntry);
        SConnection::Send(user, &outgoing, static_cast<int>(size));
    }

    void handler_0x839(CUser* user, GameTeleportMoveIncoming* packet)
    {
        auto sendResult = [user](GameTeleportMoveResult result)
        {
            GameTeleportMoveOutgoing outgoing{};
            outgoing.result = result;
            SConnection::Send(user, &outgoing, sizeof(outgoing));
        };

        if (user->status == UserStatus::Death)
            return sendResult(GameTeleportMoveResult::Dead);

        if (user->logoutTime)
            return sendResult(GameTeleportMoveResult::Busy);

        auto index = packet->index;
        if (index >= g_teleportDestinations.size())
            return sendResult(GameTeleportMoveResult::InvalidDestination);

        const auto& dest = g_teleportDestinations[index];

        if (dest.levelMin > 0 && user->level < dest.levelMin)
            return sendResult(GameTeleportMoveResult::LevelTooLow);

        if (dest.levelMax > 0 && user->level > dest.levelMax)
            return sendResult(GameTeleportMoveResult::LevelTooHigh);

        // Resolve faction: family 0,1 = Light; 2,3 = Fury
        auto factionIndex = std::clamp(static_cast<int>(user->family), 0, 3) >= 2 ? 1 : 0;
        const auto& fpos = dest.factionData[factionIndex];

        if (fpos.mapId < 0)
            return sendResult(GameTeleportMoveResult::InvalidDestination);

        if (user->mapId == fpos.mapId)
            return sendResult(GameTeleportMoveResult::AlreadyInMap);

        if (!CWorld::GetZone(fpos.mapId))
            return sendResult(GameTeleportMoveResult::InvalidDestination);

        CUser::CancelActionExc(user);
        MyShop::Ended(&user->myShop);

        user->savePosUseBag = 0;
        user->savePosUseSlot = 0;
        user->savePosUseIndex = 0;
        UserHelper::SetMovePosition(user, fpos.mapId, fpos.x, fpos.y, fpos.z, UserMovePosType::TownMoveScroll, 5000);

        GameUserItemCastOutgoing castOutgoing{};
        castOutgoing.objectId = user->id;
        CObject::PSendViewCombat(user, &castOutgoing, sizeof(GameUserItemCastOutgoing));

        sendResult(GameTeleportMoveResult::Success);
    }

    void handler(CUser* user, TP_MAIN* packet)
    {
        switch (packet->opcode)
        {
        case 0x233:
            handler_0x233(user);
            break;
        default:
            SConnection::Close(user, 9, 0);
            break;
        }
    }
}

unsigned u0x477999 = 0x477999;
void __declspec(naked) naked_0x47798E()
{
    __asm
    {
        pushad

        push ebx // packet
        push ebp // user
        call packet_main_interface::handler
        add esp,0x8

        popad

        jmp u0x477999
    }
}

void hook::packet_main_interface()
{
    // CUser::PacketMainInterface default case.
    util::detour((void*)0x47798E, naked_0x47798E, 6);
}
