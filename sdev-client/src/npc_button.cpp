// ===========================================================================
// npc_button.cpp - Remote NPC shortcut dispatcher for the ImGui NPC panel.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <shaiya/include/common/NpcTypes.h>
#include <shaiya/include/network/game/incoming/0200.h>
#include "include/main.h"
#include "include/shaiya/CNetwork.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/Static.h"
using namespace shaiya;

namespace
{
    enum class RemoteNpcAction
    {
        Vet,
        Bank,
        Guild
    };

    enum class ShortcutActionType
    {
        LegacyNpcId,
        RemoteNpc
    };

    struct RemoteNpcShortcut
    {
        ShortcutActionType actionType;
        int actionValue;
    };

    constexpr std::array<RemoteNpcShortcut, 6> kShortcuts{{
        { ShortcutActionType::LegacyNpcId, 0x65 },
        { ShortcutActionType::LegacyNpcId, 0x66 },
        { ShortcutActionType::LegacyNpcId, 0x79 },
        { ShortcutActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Vet) },
        { ShortcutActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Bank) },
        { ShortcutActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Guild) },
    }};

    void open_remote_npc(RemoteNpcAction action)
    {
        if (g_pPlayerData->windowType != WindowType::None)
        {
            Static::SysMsgToChatBox(ChatType::Acquire31, 806, 12);
            return;
        }

        switch (action)
        {
        case RemoteNpcAction::Vet:
        {
            GameStatusResultInfoIncoming outgoing{};
            CNetwork::Send(&outgoing, sizeof(GameStatusResultInfoIncoming));

            g_var->killLv = 0;
            g_var->deathLv = 0;

            g_pPlayerData->npcType = std::to_underlying(NpcType::VetManager);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 1 : 2;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            break;
        }
        case RemoteNpcAction::Bank:
        {
            g_pPlayerData->npcType = std::to_underlying(NpcType::Merchant);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 179 : 180;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            g_pPlayerData->windowType = WindowType::BankTeller;
            break;
        }
        case RemoteNpcAction::Guild:
        {
            g_pPlayerData->npcType = std::to_underlying(NpcType::GuildMaster);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 1 : 2;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            g_pPlayerData->windowType = WindowType::GuildMaster;
            break;
        }
        default:
            break;
        }
    }

    void open_legacy_inventory_npc(int npcId)
    {
        *reinterpret_cast<int*>(0x91AD44) = 0x1;
        *reinterpret_cast<int*>(0x91AD40) = 0x12C;
        *reinterpret_cast<int*>(0x9144F0) = -1;
        *reinterpret_cast<int*>(0x22AB7B8) = 0x0;
        *reinterpret_cast<int*>(0x9144E4) = npcId;
    }

    void trigger_shortcut(const RemoteNpcShortcut& shortcut)
    {
        switch (shortcut.actionType)
        {
        case ShortcutActionType::LegacyNpcId:
            open_legacy_inventory_npc(shortcut.actionValue);
            break;
        case ShortcutActionType::RemoteNpc:
            open_remote_npc(static_cast<RemoteNpcAction>(shortcut.actionValue));
            break;
        }
    }
}

void open_remote_npc_shortcut(int index)
{
    if (index < 0 || index >= static_cast<int>(kShortcuts.size()))
        return;

    trigger_shortcut(kShortcuts[static_cast<std::size_t>(index)]);
}

void hook::npc_button()
{
    // Inventory NPC shortcut buttons were replaced by the ImGui NPC panel.
}
