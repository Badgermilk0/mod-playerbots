/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RTSCVALUES_H
#define PLAYERBOTS_RTSCVALUES_H

#include "NamedObjectContext.h"
#include "TravelMgr.h"
#include "Value.h"

class PlayerbotAI;

class SeeSpellLocationValue : public LogCalculatedValue<WorldPosition>
{
public:
    SeeSpellLocationValue(PlayerbotAI* botAI, std::string const name = "see spell location")
        : LogCalculatedValue(botAI, name){};

    bool EqualToLast(WorldPosition value) override;
    WorldPosition Calculate() override;

    // MemoryCalculatedValue::Set() discards its argument (it re-sets the member to itself), so
    // without this override "see spell location" can never be written: SET_AI_VALUE in
    // SeeSpellAction and the MultiBot bridge's "regroup on me" both silently did nothing, and
    // `rtsc last` then moved bots relative to a default WorldPosition (MAPID_INVALID, 0/0/0).
    void Set(WorldPosition value) override;
};

class RTSCSelectedValue : public ManualSetValue<bool>
{
public:
    RTSCSelectedValue(PlayerbotAI* botAI, bool defaultvalue = false, std::string const name = "RTSC selected")
        : ManualSetValue(botAI, defaultvalue, name){};
};

// Set while the master's UI owns the selection. SeeSpellAction's rubber-band branch otherwise
// replaces *every* bot's "RTSC selected" with "was within 10 yards of the click", which drops the
// bots that were deliberately selected (they are far from the click) and adds whatever bystanders
// happen to stand at the destination. Defaults false, so an unlocked bot behaves exactly as
// upstream does; only a client that asks for the lock sees the difference.
class RTSCSelectionLockedValue : public ManualSetValue<bool>
{
public:
    RTSCSelectionLockedValue(PlayerbotAI* botAI, bool defaultvalue = false,
                             std::string const name = "RTSC selection locked")
        : ManualSetValue(botAI, defaultvalue, name){};
};

// Set while the master's UI asks for a "force move": an RTSC destination the bot carries out to
// completion. Upstream's move is a one-shot spline stamped MOVEMENT_NORMAL, so the first combat
// chase (MOVEMENT_COMBAT) re-points the motion master on the very next tick and the order is lost
// - nothing stores it. With this flag set, MoveToSpell remembers the point below and moves at
// MOVEMENT_FORCED, and "rtsc forced move" re-issues it until the bot arrives. Defaults false, so
// an unforced bot behaves exactly as upstream does.
class RTSCForceEnabledValue : public ManualSetValue<bool>
{
public:
    RTSCForceEnabledValue(PlayerbotAI* botAI, bool defaultvalue = false,
                          std::string const name = "RTSC force enabled")
        : ManualSetValue(botAI, defaultvalue, name){};
};

// The point a forced move is heading for, already carrying the bot's formation offset. Beware:
// WorldPosition::operator bool() is TRUE for a default-constructed value (it holds MAPID_INVALID),
// so this must never be tested with `if (position)` - see RTSCForceMoveAction::HasDestination.
class RTSCForcedDestinationValue : public ManualSetValue<WorldPosition>
{
public:
    RTSCForcedDestinationValue(PlayerbotAI* botAI, WorldPosition defaultvalue = WorldPosition(),
                               std::string const name = "RTSC forced destination")
        : ManualSetValue(botAI, defaultvalue, name){};
};

// getMSTime() past which a forced move gives up, so a bot that cannot reach its point (bad path,
// permanent root) returns to normal AI instead of running at a wall for ever.
class RTSCForcedDeadlineValue : public ManualSetValue<uint32>
{
public:
    RTSCForcedDeadlineValue(PlayerbotAI* botAI, uint32 defaultvalue = 0,
                            std::string const name = "RTSC forced deadline")
        : ManualSetValue(botAI, defaultvalue, name){};
};

class RTSCNextSpellActionValue : public ManualSetValue<std::string>
{
public:
    RTSCNextSpellActionValue(PlayerbotAI* botAI, std::string const defaultvalue = "",
                             std::string const name = "RTSC next spell action")
        : ManualSetValue(botAI, defaultvalue, name){};
};

class RTSCSavedLocationValue : public ManualSetValue<WorldPosition>, public Qualified
{
public:
    RTSCSavedLocationValue(PlayerbotAI* botAI, WorldPosition defaultvalue = WorldPosition(),
                           std::string const name = "RTSC saved location")
        : ManualSetValue(botAI, defaultvalue, name){};

    std::string const Save() override;
    bool Load(std::string const text) override;
};

#endif
