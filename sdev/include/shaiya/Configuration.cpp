#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <util/ini/ini.h>
#include <shaiya/include/network/game/RewardItemUnit.h>
#include "include/extensions/filesystem.hpp"
#include "BattlefieldMoveInfo.h"
#include "CGameData.h"
#include "Configuration.h"
#include "ItemInfo.h"
#include "ItemSynthesis.h"
#include "RewardItem.h"
#include "Roulette.h"
#include "SBinaryReader.h"
#include "Synergy.h"
#include <include/etain_shield_config.h>
using namespace shaiya;

// Global EtainShield configuration (loaded from Data/EtainShield.ini)
EtainShieldConfig g_etainConfig{};

namespace
{
    constexpr int DefaultEnchantCap = 20;
    constexpr int DefaultLevelCap = 70;
    constexpr int MinEnchantCap = 1;
    // Hard safety limit for the INI-driven enchant cap. The patched byte can
    // technically hold higher values, but production configs should not allow
    // enchant caps above 49 because higher values can destabilize balance and
    // related item calculations.
    constexpr int MaxEnchantCap = 49;
    constexpr int MinLevelCap = 1;
    constexpr int MaxLevelCap = 254;
    constexpr uintptr_t EnchantCapAddress = 0x46CCC1;
    constexpr uintptr_t LevelCapAddresses[]
    {
        0x460C57,
        0x460C87,
        0x460CB7,
        0x4612CE,
        0x4612F9,
        0x461324,
        0x46135D,
        0x4613AE,
        0x4613FB,
        0x461440,
        0x464FF7,
        0x465080,
        0x465161,
        0x4651DC,
        0x4651EC,
        0x465241,
        0x467BFE,
        0x467C20,
        0x467C99,
        0x480E0E,
        0x480FC6,
        0x49241D,
        0x49243D,
        0x49B4A4,
        0x49B4D5,
        0x49B506,
        0x49B537,
        0x49B564,
        0x49B591,
        0x49B5E0,
        0x49B63C,
        0x49B690,
        0x49B6DC,
        0x49B71E,
        0x49B760,
        0x49B7C4,
        0x49B820,
        0x49B874,
        0x49B8C0,
        0x49B902,
        0x49B944,
        0x49BB77,
        0x49BB9E,
        0x49BDA0,
        0x49BDD1,
        0x49BE02,
        0x49BE37,
        0x49BE64,
        0x49BE91,
        0x49BEDD,
        0x49BF36,
        0x49BF83,
        0x49BFD3,
        0x49C015,
        0x49C054,
        0x49C0B6,
        0x49C10F,
        0x49C15C,
        0x49C1AC,
        0x49C1EE,
        0x49C22D
    };
}

void Configuration::Init()
{
    std::wstring buffer(INT16_MAX, 0);
    auto count = GetModuleFileNameW(nullptr, buffer.data(), INT16_MAX);
    if (!count)
        return;

    auto first = buffer.begin();
    auto last = first + count;
    m_root.assign(first, last).remove_filename();
}

void Configuration::LoadServerConfig()
{
    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "ServerConfig.ini");

        if (!std::filesystem::exists(path))
        {
            util::ini::write_pair(L"Server", L"EnchantCap", L"20", path);
            util::ini::write_pair(L"Server", L"LevelCap", L"70", path);
            util::ini::write_pair(L"Server", L"RewardBar", L"1", path);
            util::ini::write_pair(L"Server", L"Roulette", L"1", path);
        }

        auto enchantCap = static_cast<int>(util::ini::get_value(L"Server", L"EnchantCap", DefaultEnchantCap, path));
        enchantCap = std::clamp(enchantCap, MinEnchantCap, MaxEnchantCap);

        auto enchantCapValue = static_cast<uint8_t>(enchantCap);
        util::write_memory(reinterpret_cast<void*>(EnchantCapAddress), &enchantCapValue, sizeof(enchantCapValue));

        auto levelCap = static_cast<int>(util::ini::get_value(L"Server", L"LevelCap", DefaultLevelCap, path));
        levelCap = std::clamp(levelCap, MinLevelCap, MaxLevelCap);

        auto levelCapValue = static_cast<uint8_t>(levelCap);
        for (auto address : LevelCapAddresses)
            util::write_memory(reinterpret_cast<void*>(address), &levelCapValue, sizeof(levelCapValue));

        RewardBarEnabled = util::ini::get_value(L"Server", L"RewardBar", 1, path) != 0;
        RouletteEnabled = util::ini::get_value(L"Server", L"Roulette", 1, path) != 0;
    }
    catch (...)
    {
        auto enchantCapValue = static_cast<uint8_t>(DefaultEnchantCap);
        util::write_memory(reinterpret_cast<void*>(EnchantCapAddress), &enchantCapValue, sizeof(enchantCapValue));

        auto levelCapValue = static_cast<uint8_t>(DefaultLevelCap);
        for (auto address : LevelCapAddresses)
            util::write_memory(reinterpret_cast<void*>(address), &levelCapValue, sizeof(levelCapValue));
    }
}

void Configuration::LoadBattlefieldMoveData()
{
    g_battlefieldMoveData.clear();

    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "BattleFieldMoveInfo.ini");

        if (!std::filesystem::exists(path))
            return;

        auto count = static_cast<int>(util::ini::get_value(L"BATTLEFIELD_INFO", L"BATTLEFIELD_COUNT", 0, path));
        if (count <= 0)
            return;

        g_battlefieldMoveData.reserve(count);

        for (int num = 1; num <= count; ++num)
        {
            auto section = std::format(L"BATTLEFIELD_{}", num);
            auto mapId = static_cast<int>(util::ini::get_value(section.c_str(), L"MAP_NO", -1, path));
            if (mapId < 0)
                continue;

            BattlefieldMoveInfo info{};
            info.levelMin = static_cast<int>(util::ini::get_value(section.c_str(), L"LEVEL_MIN", 0, path));
            info.levelMax = static_cast<int>(util::ini::get_value(section.c_str(), L"LEVEL_MAX", 0, path));

            auto readPos = [&](int index, const wchar_t* prefix)
            {
                auto keyX = std::format(L"{}_POSX", prefix);
                auto keyY = std::format(L"{}_POSY", prefix);
                auto keyZ = std::format(L"{}_POSZ", prefix);

                auto x = util::ini::get_value(section.c_str(), keyX.c_str(), L"", path);
                auto y = util::ini::get_value(section.c_str(), keyY.c_str(), L"", path);
                auto z = util::ini::get_value(section.c_str(), keyZ.c_str(), L"", path);

                info.mapPos[index].mapId = mapId;
                info.mapPos[index].x = std::stof(x);
                info.mapPos[index].y = std::stof(y);
                info.mapPos[index].z = std::stof(z);
            };

            readPos(0, L"HU");
            readPos(1, L"EL");
            readPos(2, L"DE");
            readPos(3, L"VI");

            g_battlefieldMoveData.push_back(info);
        }
    }
    catch (...)
    {
        g_battlefieldMoveData.clear();
    }
}

void Configuration::LoadTeleportDestinations()
{
    g_teleportDestinations.clear();

    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "Teleport.ini");

        if (!std::filesystem::exists(path))
            return;

        auto count = static_cast<int>(util::ini::get_value(L"TELEPORT_INFO", L"DESTINATION_COUNT", 0, path));
        if (count <= 0)
            return;

        g_teleportDestinations.reserve(count);

        for (int num = 1; num <= count; ++num)
        {
            auto section = std::format(L"DESTINATION_{}", num);

            TeleportDestination dest{};

            auto nameW = util::ini::get_value(section.c_str(), L"NAME", L"", path);
            for (auto ch : nameW)
                dest.name.push_back(static_cast<char>(ch & 0x7F));

            if (dest.name.empty())
                continue;

            dest.levelMin = static_cast<int>(util::ini::get_value(section.c_str(), L"LEVEL_MIN", 0, path));
            dest.levelMax = static_cast<int>(util::ini::get_value(section.c_str(), L"LEVEL_MAX", 0, path));

            // Light faction (family 0)
            dest.factionData[0].mapId = static_cast<int>(util::ini::get_value(section.c_str(), L"MAP_LIGHT", -1, path));
            auto lx = util::ini::get_value(section.c_str(), L"POSX_LIGHT", L"0", path);
            auto ly = util::ini::get_value(section.c_str(), L"POSY_LIGHT", L"0", path);
            auto lz = util::ini::get_value(section.c_str(), L"POSZ_LIGHT", L"0", path);
            dest.factionData[0].x = std::stof(lx);
            dest.factionData[0].y = std::stof(ly);
            dest.factionData[0].z = std::stof(lz);

            // Fury faction (family 1)
            dest.factionData[1].mapId = static_cast<int>(util::ini::get_value(section.c_str(), L"MAP_FURY", -1, path));
            auto fx = util::ini::get_value(section.c_str(), L"POSX_FURY", L"0", path);
            auto fy = util::ini::get_value(section.c_str(), L"POSY_FURY", L"0", path);
            auto fz = util::ini::get_value(section.c_str(), L"POSZ_FURY", L"0", path);
            dest.factionData[1].x = std::stof(fx);
            dest.factionData[1].y = std::stof(fy);
            dest.factionData[1].z = std::stof(fz);

            // At least one faction must have a valid map
            if (dest.factionData[0].mapId < 0 && dest.factionData[1].mapId < 0)
                continue;

            g_teleportDestinations.push_back(std::move(dest));
        }
    }
    catch (...)
    {
        g_teleportDestinations.clear();
    }
}

void Configuration::LoadItemSetData()
{
    g_itemSets.clear();
    g_itemSetSynergies.clear();

    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "SetItem.SData");

        if (!std::filesystem::exists(path))
            return;

        SBinaryReader reader(path);

        auto itemSetCount = reader.readUInt32();
        for (size_t i = 0; i < itemSetCount; ++i)
        {
            ItemSet itemSet{};
            itemSet.id = reader.readUInt16();

            // Discard the name
            auto length = reader.readUInt32();
            reader.ignore(length);

            for (auto&& itemId : itemSet.items)
            {
                auto type = reader.readUInt16();
                auto typeId = reader.readUInt16();

                auto value = (type * 1000) + typeId;
                if (value >= ItemId_MIN && value <= ItemId_MAX)
                    itemId = value;
            }

            for (auto&& synergy : itemSet.synergies)
            {
                // e.g., 70,50,0,0,0,20,0,0,0,0,0,0
                auto effects = reader.readString();
                auto rng = std::views::split(effects, ',');
                auto vec = std::ranges::to<std::vector<std::string>>(rng);
                if (vec.size() != synergy.effects.size())
                    continue;

                for (size_t i = 0; i < synergy.effects.size(); ++i)
                    synergy.effects[i] = std::stoi(vec[i]);
            }

            g_itemSets.push_back(itemSet);
        }

        reader.close();
    }
    catch (...)
    {
        g_itemSets.clear();
    }
}

void Configuration::LoadItemSynthesis()
{
    g_itemSyntheses.clear();
    g_chaoticSquares.clear();

    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "ChaoticSquare.ini");

        if (!std::filesystem::exists(path))
            return;

        auto sections = util::ini::get_sections(path);
        for (const auto& section : sections)
        {
            auto pairs = util::ini::get_pairs(section.c_str(), path);
            if (pairs.size() != 8)
                continue;

            auto itemId = std::stoi(pairs[0].second);
            if (itemId < ItemId_MIN || itemId > ItemId_MAX)
                continue;

            auto successRate = std::stoi(pairs[1].second);
            successRate = std::clamp(successRate, 1, 100);

            ItemSynthesis synthesis{};
            synthesis.successRate = successRate * 100;

            auto view = std::views::zip(
                synthesis.materialType,
                synthesis.materialTypeId,
                synthesis.materialCount
            );

            auto rng1 = std::views::split(pairs[2].second, L',');
            auto vec1 = std::ranges::to<std::vector<std::wstring>>(rng1);
            if (vec1.size() != view.size())
                continue;

            auto rng2 = std::views::split(pairs[3].second, L',');
            auto vec2 = std::ranges::to<std::vector<std::wstring>>(rng2);
            if (vec2.size() != view.size())
                continue;

            auto rng3 = std::views::split(pairs[4].second, L',');
            auto vec3 = std::ranges::to<std::vector<std::wstring>>(rng3);
            if (vec3.size() != view.size())
                continue;

            for (size_t i = 0; i < view.size(); ++i)
            {
                auto type = std::stoi(vec1[i]);
                if (!std::in_range<uint8_t>(type))
                    continue;

                auto typeId = std::stoi(vec2[i]);
                if (!std::in_range<uint8_t>(typeId))
                    continue;

                auto count = std::stoi(vec3[i]);
                if (!std::in_range<uint8_t>(count))
                    continue;

                std::get<0>(view[i]) = type;
                std::get<1>(view[i]) = typeId;
                std::get<2>(view[i]) = count;
            }

            auto type = std::stoi(pairs[5].second);
            if (!std::in_range<uint8_t>(type))
                continue;

            auto typeId = std::stoi(pairs[6].second);
            if (!std::in_range<uint8_t>(typeId))
                continue;

            auto count = std::stoi(pairs[7].second);
            if (!std::in_range<uint8_t>(count))
                continue;

            synthesis.newItemType = type;
            synthesis.newItemTypeId = typeId;
            synthesis.newItemCount = count;

            if (auto it = g_itemSyntheses.find(itemId); it != g_itemSyntheses.end())
                it->second.push_back(synthesis);
            else
                g_itemSyntheses.insert({ itemId, { synthesis } });
        }

        for (const auto& [itemId, syntheses] : g_itemSyntheses)
        {
            ChaoticSquare chaoticSquare{};
            chaoticSquare.itemId = itemId;

            auto view = std::views::zip(
                chaoticSquare.newItemType,
                chaoticSquare.newItemTypeId
            );

            size_t index = 0;
            for (const auto& synthesis : syntheses)
            {
                std::get<0>(view[index]) = synthesis.newItemType;
                std::get<1>(view[index]) = synthesis.newItemTypeId;

                ++index;

                if (index < view.size())
                    continue;
                else
                {
                    g_chaoticSquares.push_back(chaoticSquare);
                    std::fill(view.begin(), view.end(), std::tuple(0, 0));
                    index = 0;
                }
            }

            if (!index)
                continue;

            g_chaoticSquares.push_back(chaoticSquare);
        }
    }
    catch (...)
    {
        g_itemSyntheses.clear();
        g_chaoticSquares.clear();
    }
}

void Configuration::LoadRewardItemEvent()
{
    g_rewardItemCount = 0;
    g_rewardItemList.fill({});

    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "RewardItem.ini");

        if (!std::filesystem::exists(path))
            return;

        auto sections = util::ini::get_sections(path);
        auto dest = g_rewardItemList.begin();

        for (const auto& section : sections)
        {
            auto pairs = util::ini::get_pairs(section.c_str(), path);
            if (pairs.size() != 4)
                continue;

            auto minutes = std::stoi(pairs[0].second);
            if (minutes <= 0)
                continue;

            auto type = std::stoi(pairs[1].second);
            if (!std::in_range<uint8_t>(type))
                continue;

            auto typeId = std::stoi(pairs[2].second);
            if (!std::in_range<uint8_t>(typeId))
                continue;

            auto count = std::stoi(pairs[3].second);
            if (!std::in_range<uint8_t>(count))
                continue;

            dest->minutes = minutes;
            dest->type = type;
            dest->typeId = typeId;
            dest->count = count;

            ++dest;
            ++g_rewardItemCount;

            if (dest == g_rewardItemList.end())
                break;
        }
    }
    catch (...)
    {
        g_rewardItemCount = 0;
        g_rewardItemList.fill({});
    }
}

void Configuration::LoadRoulette()
{
    g_rouletteConfig = {};
    g_roulettePending.clear();

    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "Roulette.ini");

        if (!std::filesystem::exists(path))
            return;

        auto parse_item_id = [](int itemId, int& outType, int& outTypeId) -> bool
        {
            if (itemId <= 0)
                return false;

            outType = itemId / 1000;
            outTypeId = itemId % 1000;
            return std::in_range<uint8_t>(outType) && std::in_range<uint8_t>(outTypeId);
        };

        int tokenItemId = static_cast<int>(util::ini::get_value(L"Info", L"TokenItemID", 0, path));
        int tokenCount = static_cast<int>(util::ini::get_value(L"Info", L"TokenCount", 1, path));
        if (!std::in_range<uint8_t>(tokenCount) || tokenCount <= 0)
            tokenCount = 1;

        int tokenType = 0;
        int tokenTypeId = 0;
        if (!parse_item_id(tokenItemId, tokenType, tokenTypeId))
            return;

        g_rouletteConfig.tokenType = static_cast<uint8_t>(tokenType);
        g_rouletteConfig.tokenTypeId = static_cast<uint8_t>(tokenTypeId);
        g_rouletteConfig.tokenCount = static_cast<uint8_t>(tokenCount);

        int rewardCount = 0;
        for (int i = 1; i <= static_cast<int>(g_rouletteConfig.rewards.size()); ++i)
        {
            auto section = std::format(L"Reward_{}", i);
            int itemId = static_cast<int>(util::ini::get_value(section.c_str(), L"ItemID", 0, path));
            int count = static_cast<int>(util::ini::get_value(section.c_str(), L"Count", 1, path));
            int chance = static_cast<int>(util::ini::get_value(section.c_str(), L"Chance", 0, path));

            int rewardType = 0;
            int rewardTypeId = 0;
            if (!parse_item_id(itemId, rewardType, rewardTypeId))
                continue;
            if (!std::in_range<uint8_t>(count) || count <= 0)
                count = 1;
            if (chance < 0)
                chance = 0;

            auto& reward = g_rouletteConfig.rewards[rewardCount];
            reward.type = static_cast<uint8_t>(rewardType);
            reward.typeId = static_cast<uint8_t>(rewardTypeId);
            reward.count = static_cast<uint8_t>(count);
            reward.chance = static_cast<uint16_t>(std::min(chance, 10000));
            ++rewardCount;
        }

        if (!rewardCount)
            return;

        g_rouletteConfig.rewardCount = static_cast<uint8_t>(rewardCount);

        int totalChance = 0;
        for (int i = 0; i < rewardCount; ++i)
            totalChance += g_rouletteConfig.rewards[i].chance;

        if (totalChance <= 0)
        {
            const uint16_t evenChance = static_cast<uint16_t>(10000 / rewardCount);
            uint16_t used = 0;
            for (int i = 0; i < rewardCount; ++i)
            {
                g_rouletteConfig.rewards[i].chance = evenChance;
                used = static_cast<uint16_t>(used + evenChance);
            }

            int diff = 10000 - used;
            for (int i = 0; diff > 0 && i < rewardCount; ++i, --diff)
                ++g_rouletteConfig.rewards[i].chance;
        }
        else
        {
            std::array<uint16_t, kRouletteMaxRewards> normalized{};
            int normalizedTotal = 0;
            for (int i = 0; i < rewardCount; ++i)
            {
                int value = static_cast<int>((static_cast<int64_t>(g_rouletteConfig.rewards[i].chance) * 10000LL) / totalChance);
                value = std::max(0, std::min(value, 10000));
                normalized[i] = static_cast<uint16_t>(value);
                normalizedTotal += value;
            }

            int diff = 10000 - normalizedTotal;
            int index = 0;
            while (diff != 0 && rewardCount > 0)
            {
                if (diff > 0)
                {
                    ++normalized[index];
                    --diff;
                }
                else if (normalized[index] > 0)
                {
                    --normalized[index];
                    ++diff;
                }

                index = (index + 1) % rewardCount;
            }

            for (int i = 0; i < rewardCount; ++i)
                g_rouletteConfig.rewards[i].chance = normalized[i];
        }

        g_rouletteConfig.valid = true;
    }
    catch (...)
    {
        g_rouletteConfig = {};
        g_roulettePending.clear();
    }
}

// ===========================================================================
//  EtainShield.ini — anticheat configuration
// ===========================================================================

void Configuration::LoadEtainShield()
{
    // g_etainConfig has sensible defaults via member initializers.
    // If the INI is missing or a key is absent, the default applies.

    try
    {
        std::filesystem::path path(m_root);
        ext::filesystem::combine(path, "Data", "EtainShield.ini");

        if (!std::filesystem::exists(path))
            return;

        // [General]
        g_etainConfig.enabled =
            util::ini::get_value(L"General", L"Enabled", 1, path) != 0;

        // [AntiSpeedHack]
        g_etainConfig.speedHackEnabled =
            util::ini::get_value(L"AntiSpeedHack", L"Enabled", 1, path) != 0;

        auto mof = util::ini::get_value(L"AntiSpeedHack", L"MaxSpeedOnFoot", L"12.5", path);
        g_etainConfig.speedMaxOnFoot = std::stof(mof);

        auto mm = util::ini::get_value(L"AntiSpeedHack", L"MaxSpeedMounted", L"13.5", path);
        g_etainConfig.speedMaxMounted = std::stof(mm);

        g_etainConfig.speedViolationThreshold = static_cast<uint8_t>(
            util::ini::get_value(L"AntiSpeedHack", L"ViolationLimit", 5, path));

        // [AntiRangeHack]
        g_etainConfig.rangeHackEnabled =
            util::ini::get_value(L"AntiRangeHack", L"Enabled", 1, path) != 0;

        g_etainConfig.rangeMargin = static_cast<int>(
            util::ini::get_value(L"AntiRangeHack", L"Margin", 4, path));
    }
    catch (...)
    {
        // Parse error — defaults from member initializers remain.
    }
}
