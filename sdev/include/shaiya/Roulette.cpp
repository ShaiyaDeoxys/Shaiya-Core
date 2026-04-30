#include <array>
#include <random>
#include <shaiya/include/network/game/outgoing/0800.h>
#include "CGameData.h"
#include "CItem.h"
#include "CUser.h"
#include "CWorld.h"
#include "ItemInfo.h"
#include "Roulette.h"
#include "SConnection.h"
#include "UserHelper.h"
using namespace shaiya;

namespace
{
    constexpr int kInventorySlotCount = 24;
    constexpr uint32_t kRouletteSpinDurationMs = 4500U;

    bool find_item_stack(CUser* user, uint8_t type, uint8_t typeId, uint8_t count, int& outBag, int& outSlot)
    {
        if (!user || !type || !typeId || !count)
            return false;

        for (int bag = 0; bag <= user->bagsUnlocked; ++bag)
        {
            for (int slot = 0; slot < kInventorySlotCount; ++slot)
            {
                auto& item = user->inventory[bag][slot];
                if (!item || !item->count)
                    continue;
                if (item->type != type || item->typeId != typeId)
                    continue;
                if (item->count < count)
                    continue;

                outBag = bag;
                outSlot = slot;
                return true;
            }
        }

        return false;
    }

    bool has_inventory_capacity(CUser* user, ItemInfo* itemInfo, uint8_t count)
    {
        if (!user || !itemInfo || !count)
            return false;

        for (int bag = 1; bag <= user->bagsUnlocked; ++bag)
        {
            for (int slot = 0; slot < kInventorySlotCount; ++slot)
            {
                auto& item = user->inventory[bag][slot];
                if (!item)
                    return true;

                if (item->type != itemInfo->type || item->typeId != itemInfo->typeId)
                    continue;

                if (itemInfo->count > item->count && itemInfo->count - item->count >= count)
                    return true;
            }
        }

        return false;
    }

    void send_spin_result(CUser* user, GameRouletteResult result)
    {
        GameRouletteSpinOutgoing outgoing{};
        outgoing.result = result;
        SConnection::Send(user, &outgoing, sizeof(GameRouletteSpinOutgoing));
    }

    void send_reward_result(CUser* user, GameRouletteResult result, uint8_t rewardIndex = 0, uint8_t rewardType = 0, uint8_t rewardTypeId = 0, uint8_t rewardCount = 0)
    {
        GameRouletteRewardOutgoing outgoing{};
        outgoing.result = result;
        outgoing.rewardIndex = rewardIndex;
        outgoing.rewardType = rewardType;
        outgoing.rewardTypeId = rewardTypeId;
        outgoing.rewardCount = rewardCount;
        SConnection::Send(user, &outgoing, sizeof(GameRouletteRewardOutgoing));
    }

    std::mt19937& roulette_rng()
    {
        static thread_local std::mt19937 rng([]()
        {
            std::random_device seed;
            std::array<std::uint32_t, 8> seedData{};
            for (auto& value : seedData)
                value = seed();

            std::seed_seq seq(seedData.begin(), seedData.end());
            return std::mt19937(seq);
        }());

        return rng;
    }

    uint8_t choose_reward_index()
    {
        uint16_t totalChance = 0;
        for (uint8_t i = 0; i < g_rouletteConfig.rewardCount; ++i)
            totalChance = static_cast<uint16_t>(totalChance + g_rouletteConfig.rewards[i].chance);

        if (!totalChance)
            return 0;

        std::uniform_int_distribution<int> dist(1, totalChance);
        int roll = dist(roulette_rng());

        int accum = 0;
        for (uint8_t i = 0; i < g_rouletteConfig.rewardCount; ++i)
        {
            accum += g_rouletteConfig.rewards[i].chance;
            if (roll <= accum)
                return i;
        }

        return static_cast<uint8_t>(g_rouletteConfig.rewardCount - 1);
    }
}

void roulette::send_list(CUser* user)
{
    if (!user)
        return;

    std::lock_guard lock(g_rouletteMutex);

    GameRouletteListOutgoing outgoing{};
    outgoing.tokenType = g_rouletteConfig.tokenType;
    outgoing.tokenTypeId = g_rouletteConfig.tokenTypeId;
    outgoing.tokenCount = g_rouletteConfig.tokenCount;
    outgoing.itemCount = g_rouletteConfig.rewardCount;

    for (uint8_t i = 0; i < g_rouletteConfig.rewardCount && i < g_rouletteConfig.rewards.size(); ++i)
    {
        outgoing.rewardType[i] = g_rouletteConfig.rewards[i].type;
        outgoing.rewardTypeId[i] = g_rouletteConfig.rewards[i].typeId;
        outgoing.rewardCount[i] = g_rouletteConfig.rewards[i].count;
        outgoing.rewardChance[i] = g_rouletteConfig.rewards[i].chance;
    }

    SConnection::Send(user, &outgoing, sizeof(GameRouletteListOutgoing));
}

void roulette::handle_spin(CUser* user)
{
    if (!user)
        return;

    std::lock_guard lock(g_rouletteMutex);

    if (!g_rouletteConfig.valid || !g_rouletteConfig.rewardCount)
    {
        send_spin_result(user, GameRouletteResult::NotConfigured);
        return;
    }

    if (g_roulettePending.contains(user->billingId))
    {
        send_spin_result(user, GameRouletteResult::Busy);
        return;
    }

    uint8_t rewardIndex = choose_reward_index();
    const auto& reward = g_rouletteConfig.rewards[rewardIndex];
    auto itemInfo = CGameData::GetItemInfo(reward.type, reward.typeId);
    if (!itemInfo)
    {
        send_spin_result(user, GameRouletteResult::Failure);
        return;
    }

    int tokenBag = 0;
    int tokenSlot = 0;
    if (!find_item_stack(user, g_rouletteConfig.tokenType, g_rouletteConfig.tokenTypeId, g_rouletteConfig.tokenCount, tokenBag, tokenSlot))
    {
        send_spin_result(user, GameRouletteResult::NoToken);
        return;
    }

    if (!has_inventory_capacity(user, itemInfo, reward.count))
    {
        send_spin_result(user, GameRouletteResult::InventoryFull);
        return;
    }

    RoulettePendingSpin pending{};
    pending.rewardIndex = rewardIndex;
    pending.reward = reward;
    pending.tokenType = g_rouletteConfig.tokenType;
    pending.tokenTypeId = g_rouletteConfig.tokenTypeId;
    pending.tokenCount = g_rouletteConfig.tokenCount;
    pending.completeAt = std::chrono::system_clock::now() + std::chrono::milliseconds(kRouletteSpinDurationMs);

    auto [pendingIt, inserted] = g_roulettePending.emplace(user->billingId, pending);
    if (!inserted)
    {
        send_spin_result(user, GameRouletteResult::Busy);
        return;
    }

    if (!UserHelper::ItemRemove(user, tokenBag, tokenSlot, g_rouletteConfig.tokenCount))
    {
        g_roulettePending.erase(pendingIt);
        send_spin_result(user, GameRouletteResult::Failure);
        return;
    }

    GameRouletteSpinOutgoing outgoing{};
    outgoing.result = GameRouletteResult::Success;
    outgoing.rewardIndex = rewardIndex;
    outgoing.rewardType = reward.type;
    outgoing.rewardTypeId = reward.typeId;
    outgoing.rewardCount = reward.count;
    outgoing.spinDurationMs = kRouletteSpinDurationMs;
    SConnection::Send(user, &outgoing, sizeof(GameRouletteSpinOutgoing));
}

void roulette::update_pending_spins()
{
    std::lock_guard lock(g_rouletteMutex);

    if (g_roulettePending.empty())
        return;

    auto now = std::chrono::system_clock::now();
    for (auto it = g_roulettePending.begin(); it != g_roulettePending.end();)
    {
        if (now < it->second.completeAt)
        {
            ++it;
            continue;
        }

        const RoulettePendingSpin pending = it->second;
        const uint32_t billingId = it->first;

        auto user = CWorld::FindUserBill(billingId);
        if (!user)
        {
            it = g_roulettePending.erase(it);
            continue;
        }

        const auto rewardIndex = pending.rewardIndex;
        const auto& reward = pending.reward;

        auto itemInfo = CGameData::GetItemInfo(reward.type, reward.typeId);
        if (!itemInfo)
        {
            send_reward_result(user, GameRouletteResult::Failure, rewardIndex);
            it = g_roulettePending.erase(it);
            continue;
        }

        if (!has_inventory_capacity(user, itemInfo, reward.count))
        {
            send_reward_result(user, GameRouletteResult::InventoryFull, rewardIndex, reward.type, reward.typeId, reward.count);
            ++it;
            continue;
        }

        it = g_roulettePending.erase(it);

        if (!CUser::ItemCreate(user, itemInfo, reward.count))
        {
            if (auto tokenInfo = CGameData::GetItemInfo(pending.tokenType, pending.tokenTypeId))
                CUser::ItemCreate(user, tokenInfo, pending.tokenCount);

            send_reward_result(user, GameRouletteResult::Failure, rewardIndex, reward.type, reward.typeId, reward.count);
            continue;
        }

        send_reward_result(user, GameRouletteResult::Success, rewardIndex, reward.type, reward.typeId, reward.count);
    }
}
