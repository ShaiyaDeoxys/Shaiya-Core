#pragma once
#include <array>
#include <cstdint>

namespace shaiya
{
    namespace roulette_event
    {
        inline bool hasList = false;
        inline uint8_t tokenType = 0;
        inline uint8_t tokenTypeId = 0;
        inline uint8_t tokenCount = 1;
        inline uint8_t itemCount = 0;
        inline std::array<uint8_t, 10> rewardType{};
        inline std::array<uint8_t, 10> rewardTypeId{};
        inline std::array<uint8_t, 10> rewardCount{};
        inline std::array<uint16_t, 10> rewardChance{};

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
