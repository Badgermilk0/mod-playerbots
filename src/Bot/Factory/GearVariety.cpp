/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GearVariety.h"

#include "Config.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Random.h"
#include "RandomItemMgr.h"
#include "RandomPlayerbotMgr.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>

namespace
{
    struct VarietyConfig
    {
        bool randomBots = false;
        std::vector<float> tierWeights{30.0f, 30.0f, 25.0f, 12.0f, 3.0f};
        std::vector<float> tierCenters{0.10f, 0.30f, 0.50f, 0.75f, 0.92f};
        float slotJitter = 0.15f;
        bool matchPerSlot = false;
        float matchJitter = 0.03f;
        float matchBand = 0.05f;
    };

    VarietyConfig config;

    // Percentiles of a level's item range: trims junk at the bottom and a few outliers at the top.
    constexpr float RANGE_LOW_PERCENTILE = 0.10f;
    constexpr float RANGE_HIGH_PERCENTILE = 0.98f;

    // (window-end level, quality cap, inventory type) -> sorted mixed scores of the items in that window.
    std::map<uint32, std::vector<float>> rangeCache;
    std::shared_mutex rangeCacheLock;

    struct MatchScopeData
    {
        ObjectGuid bot;
        std::array<float, EQUIPMENT_SLOT_END> targets{};
    };

    thread_local MatchScopeData const* activeMatchScope = nullptr;

    std::vector<float> ParseFloatList(std::string const& text)
    {
        std::vector<float> values;
        std::stringstream stream(text);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            try
            {
                values.push_back(std::stof(token));
            }
            catch (...)
            {
                return {};
            }
        }
        return values;
    }

    float MixedScore(ItemTemplate const* proto)
    {
        return proto->ItemLevel * PlayerbotAI::GetItemScoreMultiplier(ItemQualities(proto->Quality));
    }

    // Mirrors the LimitGearExpansion item-id rules in PlayerbotFactory::InitEquipment.
    bool ExpansionAllows(uint32 level, uint32 itemId)
    {
        if (!sPlayerbotAIConfig.LimitGearExpansion)
            return true;
        if (level <= 60 && itemId >= 23728)
            return false;
        if (level <= 70 && itemId >= 35570 && itemId != 36737 && itemId != 37739 && itemId != 37740)
            return false;
        return true;
    }

    uint32 RangeKey(uint32 level, uint32 quality, InventoryType invType)
    {
        return (level << 16) | (quality << 8) | uint32(invType);
    }

    // Same RequiredLevel window the factory searches: (level - 10, level], and at least level 1.
    std::vector<float> BuildScores(uint32 level, uint32 quality, InventoryType invType)
    {
        std::vector<float> scores;
        uint32 const lowest = level > 10 ? level - 9 : 1;
        for (uint32 requiredLevel = lowest; requiredLevel <= level; ++requiredLevel)
        {
            for (uint32 itemId : sRandomItemMgr.GetEquipmentNew(requiredLevel, invType))
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR))
                    continue;
                if (proto->Quality < ITEM_QUALITY_NORMAL || proto->Quality > quality ||
                    !ExpansionAllows(level, itemId))
                    continue;
                scores.push_back(MixedScore(proto));
            }
        }
        std::sort(scores.begin(), scores.end());
        return scores;
    }

    // Low and high mixed score a bot of `level` can reach in a slot holding `invTypes`.
    bool GetRange(uint32 level, uint32 quality, std::vector<InventoryType> const& invTypes, float& low, float& high)
    {
        std::vector<float> merged;
        for (InventoryType invType : invTypes)
        {
            uint32 const key = RangeKey(level, quality, invType);
            {
                std::shared_lock<std::shared_mutex> readLock(rangeCacheLock);
                auto const itr = rangeCache.find(key);
                if (itr != rangeCache.end())
                {
                    merged.insert(merged.end(), itr->second.begin(), itr->second.end());
                    continue;
                }
            }

            std::vector<float> scores = BuildScores(level, quality, invType);
            merged.insert(merged.end(), scores.begin(), scores.end());
            std::unique_lock<std::shared_mutex> writeLock(rangeCacheLock);
            rangeCache.emplace(key, std::move(scores));
        }

        if (merged.empty())
            return false;

        std::sort(merged.begin(), merged.end());
        size_t const last = merged.size() - 1;
        low = merged[size_t(last * RANGE_LOW_PERCENTILE)];
        high = merged[size_t(last * RANGE_HIGH_PERCENTILE)];
        return true;
    }

    int32 RollTier()
    {
        float total = 0.0f;
        for (float weight : config.tierWeights)
            total += weight;

        float roll = frand(0.0f, total);
        for (size_t tier = 0; tier < config.tierWeights.size(); ++tier)
        {
            if (roll < config.tierWeights[tier])
                return int32(tier);
            roll -= config.tierWeights[tier];
        }
        return int32(config.tierWeights.size()) - 1;
    }
}

void GearVariety::LoadConfig()
{
    VarietyConfig loaded;
    loaded.randomBots = sConfigMgr->GetOption<bool>("Playerbots.GearVariety.RandomBots", false);
    loaded.slotJitter =
        std::clamp(sConfigMgr->GetOption<float>("Playerbots.GearVariety.SlotJitter", 0.15f), 0.0f, 1.0f);
    loaded.matchPerSlot = sConfigMgr->GetOption<bool>("Playerbots.GearVariety.MatchPerSlot", false);
    loaded.matchJitter =
        std::clamp(sConfigMgr->GetOption<float>("Playerbots.GearVariety.MatchJitter", 0.03f), 0.0f, 0.5f);
    loaded.matchBand =
        std::clamp(sConfigMgr->GetOption<float>("Playerbots.GearVariety.MatchBand", 0.05f), 0.0f, 0.5f);

    std::vector<float> weights =
        ParseFloatList(sConfigMgr->GetOption<std::string>("Playerbots.GearVariety.TierWeights", "30,30,25,12,3"));
    std::vector<float> centers = ParseFloatList(
        sConfigMgr->GetOption<std::string>("Playerbots.GearVariety.TierCenters", "0.10,0.30,0.50,0.75,0.92"));

    float weightSum = 0.0f;
    bool valid = !weights.empty() && weights.size() == centers.size();
    for (size_t i = 0; valid && i < weights.size(); ++i)
    {
        valid = weights[i] >= 0.0f && centers[i] >= 0.0f && centers[i] <= 1.0f;
        weightSum += weights[i];
    }

    if (valid && weightSum > 0.0f)
    {
        loaded.tierWeights = std::move(weights);
        loaded.tierCenters = std::move(centers);
    }
    else
        LOG_WARN("server.loading",
                 "Playerbots.GearVariety.TierWeights / TierCenters are invalid (need equal-length lists, weights >= 0 "
                 "with a positive sum, centers in 0..1); using the defaults.");

    config = std::move(loaded);

    std::unique_lock<std::shared_mutex> writeLock(rangeCacheLock);
    rangeCache.clear();
}

bool GearVariety::MatchPerSlotEnabled() { return config.matchPerSlot; }

GearVariety::GearVariety(Player* bot, uint32 itemQuality, uint32 gearScoreLimit, bool incremental)
    : bot(bot), itemQuality(itemQuality), incremental(incremental)
{
    slotCenter.fill(-1.0f);

    if (activeMatchScope && activeMatchScope->bot == bot->GetGUID())
    {
        mode = Mode::Match;
        matchTarget = activeMatchScope->targets;
        LOG_DEBUG("playerbots", "GearVariety: {} matching master per slot", bot->GetName());
        return;
    }

    if (!config.randomBots || !sRandomPlayerbotMgr.IsRandomBot(bot) || sRandomPlayerbotMgr.IsAddclassBot(bot))
        return;

    // Only the factory's random-bot defaults (see the PlayerbotFactory constructor); explicit init= or
    // autogear requests keep their exact upstream behaviour.
    uint32 const defaultLimit =
        sPlayerbotAIConfig.RandomGearScoreLimit == 0
            ? 0
            : PlayerbotFactory::CalcMixedGearScore(sPlayerbotAIConfig.RandomGearScoreLimit,
                                                   sPlayerbotAIConfig.RandomGearQualityLimit);
    if (itemQuality != uint32(sPlayerbotAIConfig.RandomGearQualityLimit) || gearScoreLimit != defaultLimit)
        return;

    mode = Mode::Random;

    if (incremental)
    {
        // No stored tier: read each worn item's position in the range of its own level window, so a
        // level-up regear keeps the bot near the tier it already wears.
        uint32 const botLevel = bot->GetLevel();
        float sum = 0.0f;
        uint32 count = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                continue;

            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item)
                continue;

            ItemTemplate const* proto = item->GetTemplate();
            uint32 const itemLevelWindow = std::clamp<uint32>(proto->RequiredLevel, 1, std::max<uint32>(botLevel, 1));
            float low, high;
            if (!GetRange(itemLevelWindow, itemQuality, {InventoryType(proto->InventoryType)}, low, high))
                continue;

            float const position = high > low ? std::clamp((MixedScore(proto) - low) / (high - low), 0.0f, 1.0f) : 1.0f;
            slotCenter[slot] = position;
            sum += position;
            ++count;
        }

        if (count)
        {
            tierCenter = sum / count;
            LOG_DEBUG("playerbots", "GearVariety: {} (level {}) incremental, inferred position {:.2f} from {} items",
                      bot->GetName(), botLevel, tierCenter, count);
            return;
        }
    }

    tier = RollTier();
    tierCenter = config.tierCenters[tier];
    LOG_DEBUG("playerbots", "GearVariety: {} (level {}) rolled tier {} (centre {:.2f})", bot->GetName(),
              bot->GetLevel(), tier, tierCenter);
}

void GearVariety::Filter(uint8 slot, std::vector<InventoryType> const& invTypes,
                         std::vector<std::pair<uint32, int32>>& candidates) const
{
    if (mode == Mode::Off || candidates.empty() || slot >= EQUIPMENT_SLOT_END)
        return;

    float target = 0.0f;
    if (mode == Mode::Match)
        target = matchTarget[slot];
    else
    {
        float low, high;
        if (!GetRange(bot->GetLevel(), itemQuality, invTypes, low, high))
            return;

        float const centre = (incremental && slotCenter[slot] >= 0.0f) ? slotCenter[slot] : tierCenter;
        float const position = std::clamp(centre + frand(-config.slotJitter, config.slotJitter), 0.0f, 1.0f);
        target = low + position * (high - low);
    }

    if (target <= 0.0f)
        return;

    // The factory's gathering can add an item once per quality pass; drop repeats with the rest.
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    // Variety mode gathers every quality up to the cap; keep greys out unless nothing else exists,
    // as the upstream quality fallback does.
    auto const isPoor = [](std::pair<uint32, int32> const& candidate)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(candidate.first);
        return !proto || proto->Quality < ITEM_QUALITY_NORMAL;
    };
    if (!std::all_of(candidates.begin(), candidates.end(), isPoor))
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), isPoor), candidates.end());

    std::vector<std::pair<uint32, int32>> kept;
    if (mode == Mode::Match)
    {
        // Match aims AT the master's slot, not under it: keep everything within the band either
        // side of the target. A ceiling dropped the bot a whole item-level step whenever the jitter
        // landed under an item of exactly the master's score, so bots averaged ~10% below.
        float const band = target * config.matchBand;
        float nearest = -1.0f;
        for (auto const& candidate : candidates)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(candidate.first);
            if (!proto)
                continue;

            float const distance = std::fabs(MixedScore(proto) - target);
            if (distance <= band)
                kept.push_back(candidate);
            if (nearest < 0.0f || distance < nearest)
                nearest = distance;
        }

        // Nothing in the band: keep the candidates nearest to the target, above or below.
        if (kept.empty() && nearest >= 0.0f)
        {
            for (auto const& candidate : candidates)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(candidate.first);
                if (proto && std::fabs(MixedScore(proto) - target) <= nearest + 0.5f)
                    kept.push_back(candidate);
            }
        }
    }
    else
    {
        std::pair<uint32, int32> lowest{0, 0};
        float lowestScore = 0.0f;
        for (auto const& candidate : candidates)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(candidate.first);
            if (!proto)
                continue;

            float const score = MixedScore(proto);
            if (score <= target)
                kept.push_back(candidate);
            if (!lowest.first || score < lowestScore)
            {
                lowest = candidate;
                lowestScore = score;
            }
        }

        // Never leave the slot empty: with nothing under the target, take the weakest candidate.
        if (kept.empty() && lowest.first)
            kept.push_back(lowest);
    }

    LOG_DEBUG("playerbots", "GearVariety: {} slot {} target {:.0f}, {} of {} candidates kept", bot->GetName(), slot,
              target, kept.size(), candidates.size());

    candidates = std::move(kept);
}

GearVariety::MatchScope::MatchScope(Player* bot, Player* master)
{
    std::array<float, EQUIPMENT_SLOT_END> worn{};
    float sum = 0.0f;
    uint32 count = 0;
    bool twoHanded = false;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            continue;

        if (Item* item = master->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            ItemTemplate const* proto = item->GetTemplate();
            worn[slot] = MixedScore(proto);
            sum += worn[slot];
            ++count;
            if (slot == EQUIPMENT_SLOT_MAINHAND && proto->InventoryType == INVTYPE_2HWEAPON)
                twoHanded = true;
        }
    }

    // Average over the slots actually worn, for slots the master leaves empty.
    float const average = count ? sum / count : 0.0f;

    auto* data = new MatchScopeData();
    data->bot = bot->GetGUID();
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        float base = worn[slot] > 0.0f ? worn[slot] : average;
        if (slot == EQUIPMENT_SLOT_OFFHAND)
        {
            float const mainHand = worn[EQUIPMENT_SLOT_MAINHAND] > 0.0f ? worn[EQUIPMENT_SLOT_MAINHAND] : average;
            base = (twoHanded || worn[slot] <= 0.0f) ? mainHand : worn[slot];
        }
        data->targets[slot] = base * (1.0f + frand(-config.matchJitter, config.matchJitter));
    }

    delete activeMatchScope;
    activeMatchScope = data;
}

GearVariety::MatchScope::~MatchScope()
{
    delete activeMatchScope;
    activeMatchScope = nullptr;
}
