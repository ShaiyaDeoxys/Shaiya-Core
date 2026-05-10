#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace shaiya
{
    inline constexpr std::size_t kRouletteMaxRewards = 20;

    namespace roulette_event
    {
        inline bool hasList = false;
        inline bool listReceived = false;
        inline uint8_t tokenType = 0;
        inline uint8_t tokenTypeId = 0;
        inline uint8_t tokenCount = 1;
        inline uint8_t itemCount = 0;
        inline std::array<uint8_t, kRouletteMaxRewards> rewardType{};
        inline std::array<uint8_t, kRouletteMaxRewards> rewardTypeId{};
        inline std::array<uint8_t, kRouletteMaxRewards> rewardCount{};
        inline std::array<uint16_t, kRouletteMaxRewards> rewardChance{};

        inline bool spinActive = false;
        inline uint32_t spinStartTick = 0;
        inline uint32_t spinDurationMs = 0;
        inline uint8_t rewardIndex = 0;
        inline uint8_t rewardTypeCurrent = 0;
        inline uint8_t rewardTypeIdCurrent = 0;
        inline uint8_t rewardCountCurrent = 0;
        inline uint32_t spinSerial = 0;

        inline bool lastSpinSuccess = false;
        inline uint8_t lastResult = 0;
        inline bool lastGrantSuccess = false;
        inline uint32_t grantSerial = 0;
        inline uint32_t celebrationUntilTick = 0;
    }
}
