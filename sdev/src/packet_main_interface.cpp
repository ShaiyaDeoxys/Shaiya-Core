#include <algorithm>
#include <cstring>
#include <util/util.h>
#include <shaiya/include/network/game/incoming/0200.h>
#include <shaiya/include/network/game/incoming/0800.h>
#include <shaiya/include/network/game/outgoing/0200.h>
#include <shaiya/include/network/game/outgoing/0800.h>
#include "include/main.h"
#include "include/shaiya/CObject.h"
#include "include/shaiya/CUser.h"
#include "include/shaiya/CWorld.h"
#include "include/shaiya/MyShop.h"
#include "include/shaiya/SConnection.h"
#include "include/shaiya/Teleport.h"
#include "include/shaiya/UserHelper.h"
using namespace shaiya;

namespace packet_main_interface
{
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
}
