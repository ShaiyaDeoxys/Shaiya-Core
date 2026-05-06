#include <util/util.h>
#include <algorithm>
#include <cstring>
#include <shaiya/include/network/game/outgoing/0800.h>
#include "include/main.h"
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/Roulette.h"
#include "include/shaiya/Static.h"
using namespace shaiya;

namespace packet
{
    using RecvFn = int(__stdcall*)(uintptr_t socket, char* buffer, int length, int flags);
    inline RecvFn g_originalRecv = nullptr;

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
                if (packet->itemCount <= 10 && packet->tokenType != 0)
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

    int __stdcall hooked_recv(uintptr_t socket, char* buffer, int length, int flags)
    {
        auto result = g_originalRecv ? g_originalRecv(socket, buffer, length, flags) : -1;
        if (result > 0)
            scan_roulette_packets(buffer, result);

        return result;
    }

    bool hook_import_function(const char* moduleName, const char* functionName, void* replacement, void** original)
    {
        auto* module = GetModuleHandleA(nullptr);
        if (!module || !moduleName || !functionName || !replacement || !original)
            return false;

        auto* base = reinterpret_cast<uint8_t*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        auto importRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (!importRva)
            return false;

        auto* importDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importRva);
        for (; importDesc->Name; ++importDesc)
        {
            auto* importedModuleName = reinterpret_cast<const char*>(base + importDesc->Name);
            if (_stricmp(importedModuleName, moduleName) != 0)
                continue;

            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + importDesc->FirstThunk);
            auto* originalThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + importDesc->OriginalFirstThunk);
            for (; thunk->u1.Function; ++thunk, ++originalThunk)
            {
                if (IMAGE_SNAP_BY_ORDINAL(originalThunk->u1.Ordinal))
                    continue;

                auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + originalThunk->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(importByName->Name), functionName) != 0)
                    continue;

                DWORD oldProtect = 0;
                if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
                    return false;

                *original = reinterpret_cast<void*>(thunk->u1.Function);
                thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);

                DWORD ignored = 0;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &ignored);
                return true;
            }
        }

        return false;
    }

    void hook_recv_packets()
    {
        if (g_originalRecv)
            return;

        void* original = nullptr;
        if (hook_import_function("ws2_32.dll", "recv", reinterpret_cast<void*>(hooked_recv), &original))
            g_originalRecv = reinterpret_cast<RecvFn>(original);
    }

    void handle_normalized_packet(void* packet)
    {
        scan_roulette_packets(reinterpret_cast<const char*>(packet), 0x78);
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
        cmp eax,0x834
        je roulette_packet
        cmp eax,0x835
        je roulette_packet
        cmp eax,0x836
        je roulette_packet

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
