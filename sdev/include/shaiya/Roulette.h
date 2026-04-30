#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>

namespace shaiya
{
    struct CUser;

    struct RouletteReward
    {
        uint8_t type{};
        uint8_t typeId{};
        uint8_t count{};
        uint16_t chance{};
    };

    struct RouletteConfig
    {
        uint8_t tokenType{};
        uint8_t tokenTypeId{};
        uint8_t tokenCount{ 1 };
        uint8_t rewardCount{};
        std::array<RouletteReward, 10> rewards{};
        bool valid{};
    };

    struct RoulettePendingSpin
    {
        uint8_t rewardIndex{};
        RouletteReward reward{};
        uint8_t tokenType{};
        uint8_t tokenTypeId{};
        uint8_t tokenCount{};
        std::chrono::system_clock::time_point completeAt{};
    };

    inline RouletteConfig g_rouletteConfig{};
    inline std::map<uint32_t, RoulettePendingSpin> g_roulettePending{};
    inline std::mutex g_rouletteMutex{};
}

namespace roulette
{
    void send_list(shaiya::CUser* user);
    void handle_spin(shaiya::CUser* user);
    void update_pending_spins();
}
