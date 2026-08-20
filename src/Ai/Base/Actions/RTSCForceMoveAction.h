/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RTSCFORCEMOVEACTION_H
#define PLAYERBOTS_RTSCFORCEMOVEACTION_H

#include "MovementActions.h"
#include "Trigger.h"

class PlayerbotAI;
class WorldPosition;

// An RTSC move is upstream a single spline stamped MOVEMENT_NORMAL and nothing re-issues it, so
// the first combat chase (MOVEMENT_COMBAT) takes the motion master over on the next tick. When the
// master's UI asked for a forced move, SeeSpellAction::MoveToSpell stores the destination in
// "RTSC forced destination" and moves at MOVEMENT_FORCED; this action keeps that destination alive
// until the bot actually stands on it.
//
// It runs at relevance 100 (above ACTION_EMERGENCY, the same relevance the "see spell" packet
// handler already uses), so it is always evaluated before any combat action can claim the tick.
// That matters because MoveTo caps its no-interruption window at AiPlayerbot.MaxWaitForMove: the
// re-issue has to happen in the same tick the window lapses, or a combat chase slips through.
//
// While its own spline is in flight it deliberately returns false, handing the tick back to the
// rest of the AI - the bot still casts, heals and swings on the way. Only *movement* is
// monopolised, and that is enforced by the MOVEMENT_FORCED stamp rather than by this action.
class RTSCForceMoveAction : public MovementAction
{
public:
    RTSCForceMoveAction(PlayerbotAI* botAI) : MovementAction(botAI, "rtsc forced move") {}

    bool Execute(Event event) override;
    bool isUseful() override;

    // WorldPosition::operator bool() is true for a default-constructed value (it carries
    // MAPID_INVALID), which is what made saved slots impossible to clear once - so "is there a
    // destination" is an explicit test for a real map id and non-zero coordinates.
    static bool IsUsableDestination(WorldPosition const& position);

private:
    void ClearDestination();
};

class RTSCForceMoveTrigger : public Trigger
{
public:
    RTSCForceMoveTrigger(PlayerbotAI* botAI) : Trigger(botAI, "rtsc forced move") {}

    bool IsActive() override;
};

#endif
