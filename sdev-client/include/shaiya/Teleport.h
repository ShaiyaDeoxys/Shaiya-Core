#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <shaiya/include/network/game/outgoing/0800.h>

namespace shaiya
{
    namespace teleport_event
    {
        inline bool hasList = false;
        inline bool listReceived = false;
        inline uint8_t destinationCount = 0;
        inline std::array<TeleportDestinationEntry, kTeleportMaxDestinations> destinations{};

        // Result of last move request
        inline bool lastMoveRequested = false;
        inline GameTeleportMoveResult lastMoveResult = GameTeleportMoveResult::Success;
        inline uint32_t lastMoveResultTick = 0;
    }
}
