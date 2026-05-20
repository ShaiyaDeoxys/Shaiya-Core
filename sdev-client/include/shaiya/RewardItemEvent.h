#pragma once
#include <array>
#include <cstdint>

// ===========================================================================
//  RewardItemEvent — Client-side state for the time-played reward bar.
// ===========================================================================
//
//  The server sends a list of sequential rewards (0x1F03), the current
//  index/timer (0x1F05), and notifies when a reward is ready (0x1F04).
//  The client can claim via opcode 0x1F04 (incoming to server).
//
//  Packet flow:
//    Login  -> Server sends 0x1F03 (item list) + 0x1F05 (current index/timer)
//    Timer  -> Server sends 0x1F04 when the current reward becomes claimable
//    Claim  -> Client sends 0x1F04, server replies 0x1F01 (result) + 0x1F05 (next)
//    End    -> Server sends 0x1F06 if the event ends
// ===========================================================================

namespace shaiya::reward_item_event
{
    struct Item
    {
        uint32_t minutes{};
        uint8_t type{};
        uint8_t typeId{};
        uint8_t count{};
    };

    inline bool hasList = false;
    inline uint32_t itemCount = 0;
    inline std::array<Item, 20> items{};
    inline uint32_t index = 0;
    inline bool claimReady = false;
    inline uint32_t timerStartTick = 0;
    inline uint32_t timerDurationMs = 0;
    inline uint32_t lastMessageNumber = 0;
    inline bool lastClaimSuccess = false;
}
