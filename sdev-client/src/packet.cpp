#include <util/util.h>
#include <algorithm>
#include <cstring>
#include <shaiya/include/network/game/outgoing/0800.h>
#include <shaiya/include/network/game/outgoing/1F00.h>
#include "include/main.h"
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/RewardItemEvent.h"
#include "include/shaiya/Roulette.h"
#include "include/shaiya/Teleport.h"
#include "include/shaiya/Static.h"
using namespace shaiya;

namespace packet
{
    // Forward declarations
    void scan_roulette_packets(const char* buffer, int length);
    void scan_reward_item_packets(const char* buffer, int length);
    void scan_teleport_packets(const char* buffer, int length);
    void handler_0x1F01(GameRewardItemGetResultOutgoing* incoming);
    void handler_0x1F03(GameRewardItemListOutgoing* incoming);
    void handler_0x1F04(GameRewardItemGetOutgoing* incoming);
    void handler_0x1F05(GameRewardItemListIndexOutgoing* incoming);
    void handler_0x1F06(GameRewardItemEventEndedOutgoing* incoming);

    void patch_quantity_limit()
    {
        static constexpr unsigned int kQuantityHookAddr = 0x42E117;
        static constexpr unsigned int kQuantityPatchAddr = 0x42E127;
        static constexpr unsigned char kCmpBytes[] = {0x3D, 0xFF, 0x00, 0x00, 0x00};
        static constexpr unsigned char kNopBytes[] = {0x90, 0x90, 0x90};
        static constexpr unsigned char kMovBytes[] = {0xFF, 0x00, 0x00, 0x00};

        // Raise the client-side buy/sell quantity limit from 10 to 255.
        // cmp eax,0x0A -> cmp eax,0xFF
        util::write_memory((void*)kQuantityHookAddr, kCmpBytes, sizeof(kCmpBytes));
        // Clear the remaining bytes from the original instruction block.
        util::write_memory((void*)(kQuantityHookAddr + 5), kNopBytes, sizeof(kNopBytes));
        // mov [esi+1D58],0x0A -> mov [esi+1D58],0xFF
        util::write_memory((void*)(kQuantityPatchAddr + 6), kMovBytes, sizeof(kMovBytes));
    }

    // Resolves an issue with disguise removal
    void hook_0x303(CCharacter* user)
    {
        if (!user->equipment.type[ItemSlot::Pet])
            CCharacter::RemovePet(user);

        if (!user->equipment.type[ItemSlot::Costume])
            CCharacter::RemoveCostume(user);

        if (!user->equipment.type[ItemSlot::Wings])
            CCharacter::RemoveWings(user);
    }

    // Adds support for system message 509
    void hook_0x229(CCharacter* killer, unsigned killCount)
    {
        std::memcpy(g_var->sysmsg_t.data(), killer->charName.data(), killer->charName.size());
        g_var->sysmsg_t[killer->charName.size() - 1] = '\0';
        g_var->sysmsg_v = killCount;
        Static::SysMsgToChatBox(static_cast<ChatType>(1), 509, 1);
    }

    void handler_0x834(GameRouletteListOutgoing* incoming)
    {
        roulette_event::tokenType = incoming->tokenType;
        roulette_event::tokenTypeId = incoming->tokenTypeId;
        roulette_event::tokenCount = incoming->tokenCount;
        roulette_event::itemCount = incoming->itemCount;
        roulette_event::rewardType = incoming->rewardType;
        roulette_event::rewardTypeId = incoming->rewardTypeId;
        roulette_event::rewardCount = incoming->rewardCount;
        roulette_event::rewardChance = incoming->rewardChance;
        roulette_event::listReceived = true;
        roulette_event::hasList = incoming->itemCount > 0;
    }

    void handler_0x835(GameRouletteSpinOutgoing* incoming)
    {
        roulette_event::lastSpinSuccess = incoming->result == GameRouletteResult::Success;
        roulette_event::lastResult = static_cast<uint8_t>(incoming->result);
        if (!roulette_event::lastSpinSuccess)
            return;

        roulette_event::spinActive = true;
        roulette_event::spinStartTick = GetTickCount();
        roulette_event::spinDurationMs = incoming->spinDurationMs;
        roulette_event::rewardIndex = incoming->rewardIndex;
        roulette_event::rewardTypeCurrent = incoming->rewardType;
        roulette_event::rewardTypeIdCurrent = incoming->rewardTypeId;
        roulette_event::rewardCountCurrent = incoming->rewardCount;
        roulette_event::lastGrantSuccess = false;
        roulette_event::celebrationUntilTick = 0;
        ++roulette_event::spinSerial;
    }

    void handler_0x836(GameRouletteRewardOutgoing* incoming)
    {
        roulette_event::lastGrantSuccess = incoming->result == GameRouletteResult::Success;
        roulette_event::lastResult = static_cast<uint8_t>(incoming->result);
        roulette_event::spinActive = false;
        roulette_event::rewardIndex = incoming->rewardIndex;
        roulette_event::rewardTypeCurrent = incoming->rewardType;
        roulette_event::rewardTypeIdCurrent = incoming->rewardTypeId;
        roulette_event::rewardCountCurrent = incoming->rewardCount;
        if (roulette_event::lastGrantSuccess)
            roulette_event::celebrationUntilTick = GetTickCount() + 2500U;
        ++roulette_event::grantSerial;
    }

    template <typename Packet>
    void dispatch_roulette_payload(uint16_t opcode, const void* packet, void(*handler)(Packet*))
    {
        if (!packet)
            return;

        if (*reinterpret_cast<const uint16_t*>(packet) == opcode)
        {
            handler(const_cast<Packet*>(reinterpret_cast<const Packet*>(packet)));
            return;
        }

        Packet normalized{};
        normalized.opcode = opcode;
        constexpr auto payloadSize = sizeof(Packet) - sizeof(uint16_t);
        std::memcpy(reinterpret_cast<uint8_t*>(&normalized) + sizeof(uint16_t), packet, payloadSize);
        handler(&normalized);
    }

    void handle_roulette_dispatch(uint16_t opcode, void* packet)
    {
        switch (opcode)
        {
        case 0x834:
            dispatch_roulette_payload<GameRouletteListOutgoing>(opcode, packet, handler_0x834);
            break;
        case 0x835:
            dispatch_roulette_payload<GameRouletteSpinOutgoing>(opcode, packet, handler_0x835);
            break;
        case 0x836:
            dispatch_roulette_payload<GameRouletteRewardOutgoing>(opcode, packet, handler_0x836);
            break;
        default:
            break;
        }
    }

    template <typename Packet>
    void dispatch_reward_item_payload(uint16_t opcode, const void* packet, void(*handler)(Packet*))
    {
        if (!packet)
            return;

        if (*reinterpret_cast<const uint16_t*>(packet) == opcode)
        {
            handler(const_cast<Packet*>(reinterpret_cast<const Packet*>(packet)));
            return;
        }

        Packet normalized{};
        normalized.opcode = opcode;
        constexpr auto payloadSize = sizeof(Packet) - sizeof(uint16_t);
        std::memcpy(reinterpret_cast<uint8_t*>(&normalized) + sizeof(uint16_t), packet, payloadSize);
        handler(&normalized);
    }

    void dispatch_reward_item_list_payload(uint16_t opcode, const void* packet)
    {
        if (!packet)
            return;

        if (*reinterpret_cast<const uint16_t*>(packet) == opcode)
        {
            handler_0x1F03(const_cast<GameRewardItemListOutgoing*>(
                reinterpret_cast<const GameRewardItemListOutgoing*>(packet)));
            return;
        }

        auto* payload = reinterpret_cast<const uint8_t*>(packet);
        uint32_t count = *reinterpret_cast<const uint32_t*>(payload);
        if (count > reward_item_event::items.size())
            count = static_cast<uint32_t>(reward_item_event::items.size());

        GameRewardItemListOutgoing normalized{};
        normalized.opcode = opcode;
        normalized.itemCount = count;
        if (count > 0)
        {
            std::memcpy(&normalized.itemList[0], payload + sizeof(uint32_t),
                count * sizeof(RewardItemUnit));
        }

        handler_0x1F03(&normalized);
    }

    void handle_roulette_packet(const void* data, int length)
    {
        if (!data || length < static_cast<int>(sizeof(uint16_t)))
            return;

        auto opcode = *reinterpret_cast<const uint16_t*>(data);
        switch (opcode)
        {
        case 0x834:
            if (length >= static_cast<int>(sizeof(GameRouletteListOutgoing)))
                handler_0x834(const_cast<GameRouletteListOutgoing*>(reinterpret_cast<const GameRouletteListOutgoing*>(data)));
            break;
        case 0x835:
            if (length >= static_cast<int>(sizeof(GameRouletteSpinOutgoing)))
                handler_0x835(const_cast<GameRouletteSpinOutgoing*>(reinterpret_cast<const GameRouletteSpinOutgoing*>(data)));
            break;
        case 0x836:
            if (length >= static_cast<int>(sizeof(GameRouletteRewardOutgoing)))
                handler_0x836(const_cast<GameRouletteRewardOutgoing*>(reinterpret_cast<const GameRouletteRewardOutgoing*>(data)));
            break;
        default:
            break;
        }
    }

    void scan_roulette_packets(const char* buffer, int length)
    {
        if (!buffer || length < static_cast<int>(sizeof(uint16_t)))
            return;

        for (int offset = 0; offset <= length - static_cast<int>(sizeof(uint16_t)); ++offset)
        {
            auto* data = buffer + offset;
            auto remaining = length - offset;
            auto opcode = *reinterpret_cast<const uint16_t*>(data);

            if (opcode == 0x834 && remaining >= static_cast<int>(sizeof(GameRouletteListOutgoing)))
            {
                auto* packet = reinterpret_cast<const GameRouletteListOutgoing*>(data);
                if (packet->itemCount <= kRouletteMaxRewards && packet->tokenType != 0)
                    handle_roulette_packet(data, remaining);
            }
            else if (opcode == 0x835 && remaining >= static_cast<int>(sizeof(GameRouletteSpinOutgoing)))
            {
                auto* packet = reinterpret_cast<const GameRouletteSpinOutgoing*>(data);
                if (packet->result >= GameRouletteResult::Success && packet->result <= GameRouletteResult::NotConfigured)
                    handle_roulette_packet(data, remaining);
            }
            else if (opcode == 0x836 && remaining >= static_cast<int>(sizeof(GameRouletteRewardOutgoing)))
            {
                auto* packet = reinterpret_cast<const GameRouletteRewardOutgoing*>(data);
                if (packet->result >= GameRouletteResult::Success && packet->result <= GameRouletteResult::NotConfigured)
                    handle_roulette_packet(data, remaining);
            }
        }
    }


    // =======================================================================
    //  Reward Item Event handlers (opcodes 0x1F01 – 0x1F06)
    // =======================================================================

    void handler_0x1F01(GameRewardItemGetResultOutgoing* incoming)
    {
        reward_item_event::lastClaimSuccess = incoming->result == GameRewardItemGetResult::Success;
        reward_item_event::lastMessageNumber = incoming->messageNumber;
        if (reward_item_event::lastClaimSuccess)
            reward_item_event::claimReady = false;
    }

    void handler_0x1F03(GameRewardItemListOutgoing* incoming)
    {
        auto count = incoming->itemCount;
        if (count > reward_item_event::items.size())
            count = static_cast<uint32_t>(reward_item_event::items.size());

        reward_item_event::items.fill({});
        reward_item_event::itemCount = count;

        for (uint32_t i = 0; i < count; ++i)
        {
            reward_item_event::items[i].minutes = incoming->itemList[i].minutes;
            reward_item_event::items[i].type    = incoming->itemList[i].type;
            reward_item_event::items[i].typeId  = incoming->itemList[i].typeId;
            reward_item_event::items[i].count   = incoming->itemList[i].count;
        }

        reward_item_event::hasList = count > 0;
    }

    void handler_0x1F04(GameRewardItemGetOutgoing*)
    {
        reward_item_event::claimReady = true;
    }

    void handler_0x1F05(GameRewardItemListIndexOutgoing* incoming)
    {
        reward_item_event::index = incoming->index;
        reward_item_event::claimReady = false;

        if (!reward_item_event::hasList || reward_item_event::index >= reward_item_event::itemCount)
        {
            reward_item_event::timerStartTick = 0;
            reward_item_event::timerDurationMs = 0;
            return;
        }

        auto minutes = reward_item_event::items[reward_item_event::index].minutes;
        reward_item_event::timerDurationMs = (minutes * 60000U) + 15000U;
        reward_item_event::timerStartTick = GetTickCount();
    }

    void handler_0x1F06(GameRewardItemEventEndedOutgoing*)
    {
        reward_item_event::claimReady = false;
        reward_item_event::timerStartTick = 0;
        reward_item_event::timerDurationMs = 0;
    }

    void handle_reward_item_dispatch(uint16_t opcode, void* packet)
    {
        switch (opcode)
        {
        case 0x1F01:
            dispatch_reward_item_payload<GameRewardItemGetResultOutgoing>(opcode, packet, handler_0x1F01);
            break;
        case 0x1F03:
            dispatch_reward_item_list_payload(opcode, packet);
            break;
        case 0x1F04:
            dispatch_reward_item_payload<GameRewardItemGetOutgoing>(opcode, packet, handler_0x1F04);
            break;
        case 0x1F05:
            dispatch_reward_item_payload<GameRewardItemListIndexOutgoing>(opcode, packet, handler_0x1F05);
            break;
        case 0x1F06:
            handler_0x1F06(nullptr);
            break;
        default:
            break;
        }
    }

    void handle_reward_item_packet(const void* data, int length)
    {
        if (!data || length < static_cast<int>(sizeof(uint16_t)))
            return;

        auto opcode = *reinterpret_cast<const uint16_t*>(data);
        switch (opcode)
        {
        case 0x1F01:
            if (length >= static_cast<int>(sizeof(GameRewardItemGetResultOutgoing)))
                handler_0x1F01(const_cast<GameRewardItemGetResultOutgoing*>(
                    reinterpret_cast<const GameRewardItemGetResultOutgoing*>(data)));
            break;
        case 0x1F03:
            if (length >= static_cast<int>(GameRewardItemListOutgoing::baseLength))
                handler_0x1F03(const_cast<GameRewardItemListOutgoing*>(
                    reinterpret_cast<const GameRewardItemListOutgoing*>(data)));
            break;
        case 0x1F04:
            if (length >= static_cast<int>(sizeof(GameRewardItemGetOutgoing)))
                handler_0x1F04(const_cast<GameRewardItemGetOutgoing*>(
                    reinterpret_cast<const GameRewardItemGetOutgoing*>(data)));
            break;
        case 0x1F05:
            if (length >= static_cast<int>(sizeof(GameRewardItemListIndexOutgoing)))
                handler_0x1F05(const_cast<GameRewardItemListIndexOutgoing*>(
                    reinterpret_cast<const GameRewardItemListIndexOutgoing*>(data)));
            break;
        case 0x1F06:
            handler_0x1F06(nullptr);
            break;
        default:
            break;
        }
    }

    void scan_reward_item_packets(const char* buffer, int length)
    {
        if (!buffer || length < static_cast<int>(sizeof(uint16_t)))
            return;

        for (int offset = 0; offset <= length - static_cast<int>(sizeof(uint16_t)); ++offset)
        {
            auto opcode = *reinterpret_cast<const uint16_t*>(buffer + offset);
            if (opcode >= 0x1F01 && opcode <= 0x1F06)
            {
                handle_reward_item_packet(buffer + offset, length - offset);
                break;
            }
        }
    }

    // =======================================================================
    //  Teleport handlers (opcodes 0x838 – 0x839)
    // =======================================================================

    void handler_0x838(const GameTeleportListOutgoing* incoming)
    {
        auto count = incoming->count;
        if (count > kTeleportMaxDestinations)
            count = kTeleportMaxDestinations;

        teleport_event::destinationCount = count;
        teleport_event::destinations = {};

        for (uint8_t i = 0; i < count; ++i)
            teleport_event::destinations[i] = incoming->entries[i];

        teleport_event::hasList = count > 0;
        teleport_event::listReceived = true;
    }

    void handler_0x839(const GameTeleportMoveOutgoing* incoming)
    {
        teleport_event::lastMoveRequested = true;
        teleport_event::lastMoveResult = incoming->result;
        teleport_event::lastMoveResultTick = GetTickCount();
    }

    void handle_teleport_packet(const void* data, int length)
    {
        if (!data || length < static_cast<int>(sizeof(uint16_t)))
            return;

        auto opcode = *reinterpret_cast<const uint16_t*>(data);
        switch (opcode)
        {
        case 0x838:
        {
            // Variable-length: header (opcode + count) + count * entry size
            auto minSize = static_cast<int>(offsetof(GameTeleportListOutgoing, entries));
            if (length >= minSize)
                handler_0x838(reinterpret_cast<const GameTeleportListOutgoing*>(data));
            break;
        }
        case 0x839:
            if (length >= static_cast<int>(sizeof(GameTeleportMoveOutgoing)))
                handler_0x839(reinterpret_cast<const GameTeleportMoveOutgoing*>(data));
            break;
        default:
            break;
        }
    }

    void handle_teleport_dispatch(uint16_t opcode, void* packet)
    {
        switch (opcode)
        {
        case 0x838:
            if (packet)
                handler_0x838(reinterpret_cast<const GameTeleportListOutgoing*>(packet));
            break;
        case 0x839:
            if (packet)
                handler_0x839(reinterpret_cast<const GameTeleportMoveOutgoing*>(packet));
            break;
        default:
            break;
        }
    }

    void scan_teleport_packets(const char* buffer, int length)
    {
        if (!buffer || length < static_cast<int>(sizeof(uint16_t)))
            return;

        for (int offset = 0; offset <= length - static_cast<int>(sizeof(uint16_t)); ++offset)
        {
            auto opcode = *reinterpret_cast<const uint16_t*>(buffer + offset);
            if (opcode == 0x838 || opcode == 0x839)
            {
                handle_teleport_packet(buffer + offset, length - offset);
                break;
            }
        }
    }

    void handle_normalized_packet(void* packet)
    {
        auto* buf = reinterpret_cast<const char*>(packet);
        scan_roulette_packets(buf, 0x78);
        scan_reward_item_packets(buf, 0x78);
        scan_teleport_packets(buf, 0x78);
    }
}

unsigned u0x59F8AF = 0x59F8AF;
void __declspec(naked) naked_0x59F896()
{
    __asm
    {
        // CPlayerData->charId
        mov edi,dword ptr ds:[0x90E2F4]
        // CCharacter->id
        cmp edi,[esi+0x34]
        jne wrong_appearance

        // sex, size, face, hair
        mov byte ptr ds:[0x914474],al
        mov byte ptr ds:[0x913472],cl
        mov byte ptr ds:[0x913471],dl
        mov byte ptr ds:[0x913470],bl

        wrong_appearance:
        mov ecx,esi
        jmp u0x59F8AF
    }
}

unsigned u0x5933FE = 0x5933FE;
void __declspec(naked) naked_0x5933F8()
{
    __asm
    {
        // original
        mov byte ptr[esi+0x1C9],al

        pushad

        push esi // user
        call packet::hook_0x303
        add esp,0x4

        popad

        jmp u0x5933FE
    }
}

unsigned u0x4EF315 = 0x4EF315;
void __declspec(naked) naked_0x4EF2F3()
{
    __asm
    {
        // original
        mov [eax+0x10],ebx

        pushad

        push ebx // killCount
        push edi // killer
        call packet::hook_0x229
        add esp,0x8

        popad

        jmp u0x4EF315
    }
}

unsigned u0x593D15 = 0x593D15;
unsigned u0x593D73 = 0x593D73;
void __declspec(naked) naked_0x593D0F()
{
    __asm
    {
        // original
        push 0x0 // arg #16
        cmp al,0xB
        jne _0x593D73

        push 0x0 // arg #15
        jmp u0x593D15

        _0x593D73:
        jmp u0x593D73
    }
}

unsigned u0x59EC8E = 0x59EC8E;
void __declspec(naked) naked_0x59EC88()
{
    __asm
    {
        pushad

        push eax
        call packet::handle_normalized_packet
        add esp,0x4

        popad

        mov esi,dword ptr ds:[0x22AF6F8]
        jmp u0x59EC8E
    }
}

unsigned u0x5F1E17 = 0x5F1E17;
void __declspec(naked) naked_0x5F1E10()
{
    __asm
    {
        pushad

        movzx eax,word ptr [esp+0x24]

        // --- Roulette / Reward Item (game phase) ---
        cmp eax,0x834
        je roulette_packet
        cmp eax,0x835
        je roulette_packet
        cmp eax,0x836
        je roulette_packet
        cmp eax,0x1F01
        je reward_item_packet
        cmp eax,0x1F03
        je reward_item_packet
        cmp eax,0x1F04
        je reward_item_packet
        cmp eax,0x1F05
        je reward_item_packet
        cmp eax,0x1F06
        je reward_item_packet
        cmp eax,0x838
        je teleport_packet
        cmp eax,0x839
        je teleport_packet

        popad
        mov edx,dword ptr [esp+0x4]
        movzx eax,dx
        jmp u0x5F1E17

        roulette_packet:
        mov ecx,dword ptr [esp+0x28]
        push ecx
        push eax
        call packet::handle_roulette_dispatch
        add esp,0x8

        popad
        ret

        reward_item_packet:
        mov ecx,dword ptr [esp+0x28]
        push ecx
        push eax
        call packet::handle_reward_item_dispatch
        add esp,0x8

        popad
        ret

        teleport_packet:
        mov ecx,dword ptr [esp+0x28]
        push ecx
        push eax
        call packet::handle_teleport_dispatch
        add esp,0x8

        popad
        ret
    }
}

void hook::packet()
{
    // Increase the buy/sell item quantity limit from 10 to 255.
    packet::patch_quantity_limit();
    // Custom roulette packets are consumed before the stock dispatcher can drop them.
    util::detour((void*)0x5F1E10, naked_0x5F1E10, 7);
    // disguise removal bug (0x303 handler)
    util::detour((void*)0x5933F8, naked_0x5933F8, 6);
    // appearance/sex change bug (0x226 handler)
    util::detour((void*)0x59F896, naked_0x59F896, 6);
    // system message 509 (0x229 handler)
    util::detour((void*)0x4EF2F3, naked_0x4EF2F3, 5);
    // javelin attack bug (0x502 handler)
    util::detour((void*)0x593D0F, naked_0x593D0F, 6);
    // increase the stack offsets (see detour)
    util::write_memory((void*)0x593D46, 0x3C, 1);
    util::write_memory((void*)0x593D4D, 0x4C, 1);
    // remove argument #8
    util::write_memory((void*)0x593D4F, 0x90, 2);
}
