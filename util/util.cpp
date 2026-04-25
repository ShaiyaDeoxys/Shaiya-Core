#include <cstdint>
#include <cstring>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "util.h"

int util::detour(void* addr, void* dest, size_t size)
{
    #pragma pack(push, 1)
    struct {
        uint8_t opcode{ 0xE9 };
        uint32_t operand;
    } instruction{};
    #pragma pack(pop)

    static_assert(sizeof(instruction) == 5);

    if (size < sizeof(instruction))
        return 0;

    unsigned long protect;
    if (!VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &protect))
        return 0;

    auto address = (std::uintptr_t(dest) - std::uintptr_t(addr)) - sizeof(instruction);
    instruction.operand = address;

    std::memset(addr, 0x90, size);
    std::memcpy(addr, &instruction, sizeof(instruction));
    FlushInstructionCache(GetCurrentProcess(), addr, size);
    return VirtualProtect(addr, size, protect, &protect);
}

int util::read_memory(void* addr, void* dest, size_t size)
{
    if (size < 1)
        return 0;

    unsigned long protect;
    if (!VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &protect))
        return 0;

    auto success = ReadProcessMemory(GetCurrentProcess(), addr, dest, size, nullptr);

    auto restored = VirtualProtect(addr, size, protect, &protect);
    return success && restored;
}

int util::write_memory(void* addr, const void* src, size_t size)
{
    if (size < 1)
        return 0;

    unsigned long protect;
    if (!VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &protect))
        return 0;

    auto success = WriteProcessMemory(GetCurrentProcess(), addr, src, size, nullptr);
    if (success)
        FlushInstructionCache(GetCurrentProcess(), addr, size);

    auto restored = VirtualProtect(addr, size, protect, &protect);
    return success && restored;
}

int util::write_memory(void* addr, int value, size_t size)
{
    if (size < 1)
        return 0;

    unsigned long protect;
    if (!VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &protect))
        return 0;

    std::memset(addr, value, size);
    FlushInstructionCache(GetCurrentProcess(), addr, size);
    return VirtualProtect(addr, size, protect, &protect);
}
