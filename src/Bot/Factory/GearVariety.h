/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_GEARVARIETY_H
#define PLAYERBOTS_GEARVARIETY_H

#include "ItemTemplate.h"
#include "Player.h"
#include <array>
#include <utility>
#include <vector>

// Fork-only gear shaping layered on top of PlayerbotFactory::InitEquipment (Playerbots.GearVariety.*).
//
// Random mode : world RND bots roll a gear tier (or infer it from what they wear on an incremental
//               regear) and every slot targets a jittered point of its level's item range, so the
//               world shows a mix of greens, blues and purples instead of best-in-slot everywhere.
// Match mode  : 'autogear match' gives each slot the master's same-slot mixed score (ilvl x quality
//               factor) +/- a small jitter.
//
// In random mode a slot target is a ceiling on the candidates' mixed score; in match mode it is a
// band either side of the master's score. The factory still picks the best stat-scoring item among
// the candidates left. With both features off the object is inert and InitEquipment runs the
// upstream logic unchanged.
class GearVariety
{
public:
    static void LoadConfig();

    GearVariety(Player* bot, uint32 itemQuality, uint32 gearScoreLimit, bool incremental);

    // In variety mode the candidate gathering accepts every quality up to the cap, not just the cap.
    bool Active() const { return mode != Mode::Off; }

    // Match mode turns off the factory's random 25% candidate skip, which would otherwise drop the
    // item closest to the master's slot one time in four.
    bool KeepsAllCandidates() const { return mode == Mode::Match; }

    // Drops candidates above the slot's target; never leaves a slot without a candidate.
    void Filter(uint8 slot, std::vector<InventoryType> const& invTypes,
                std::vector<std::pair<uint32, int32>>& candidates) const;

    static bool MatchPerSlotEnabled();

    // Opens per-slot master matching for the next InitEquipment of `bot` on this thread.
    class MatchScope
    {
    public:
        MatchScope(Player* bot, Player* master);
        ~MatchScope();
        MatchScope(MatchScope const&) = delete;
        MatchScope& operator=(MatchScope const&) = delete;
    };

private:
    enum class Mode : uint8
    {
        Off,
        Random,
        Match
    };

    Mode mode = Mode::Off;
    Player* bot = nullptr;
    uint32 itemQuality = 0;
    bool incremental = false;
    float tierCenter = 0.0f;                                  // random mode: rolled or inferred centre
    int32 tier = -1;                                          // random mode: rolled tier, -1 = inferred
    std::array<float, EQUIPMENT_SLOT_END> slotCenter{};       // random mode, incremental: per-slot centre (<0 = none)
    std::array<float, EQUIPMENT_SLOT_END> matchTarget{};      // match mode: per-slot mixed-score target
};

#endif
