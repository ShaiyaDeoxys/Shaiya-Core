#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace shaiya
{
    // Named teleport destinations with optional level gates.
    // Each destination has separate map/position per faction (Light=0, Fury=1).
    struct TeleportFactionData
    {
        int32_t mapId;
        float x;
        float y;
        float z;
    };

    struct TeleportDestination
    {
        std::string name;
        int32_t levelMin;
        int32_t levelMax;
        TeleportFactionData factionData[2]; // 0=Light, 1=Fury
    };

    inline std::vector<TeleportDestination> g_teleportDestinations;
}
