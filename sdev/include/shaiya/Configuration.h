#pragma once
#include <filesystem>

namespace shaiya
{
    class Configuration final
    {
    public:

        static void Init();
        static void LoadServerConfig();
        static void LoadBattlefieldMoveData();
        static void LoadTeleportDestinations();
        static void LoadItemSetData();
        static void LoadItemSynthesis();
        static void LoadRewardItemEvent();
        static void LoadRoulette();
        static void LoadEtainShield();

        // Feature toggles from ServerConfig.ini (default ON)
        inline static bool RewardBarEnabled = true;
        inline static bool RouletteEnabled = true;

    private:

        inline static std::filesystem::path m_root{};
    };
}
