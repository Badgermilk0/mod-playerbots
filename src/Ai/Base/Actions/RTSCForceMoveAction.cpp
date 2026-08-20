/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RTSCForceMoveAction.h"
#include "Playerbots.h"
#include "RTSCValues.h"

bool RTSCForceMoveAction::IsUsableDestination(WorldPosition const& position)
{
    if (position.GetMapId() == MAPID_INVALID)
        return false;

    return position.GetPositionX() != 0.0f || position.GetPositionY() != 0.0f ||
           position.GetPositionZ() != 0.0f;
}

void RTSCForceMoveAction::ClearDestination()
{
    RESET_AI_VALUE(WorldPosition, "RTSC forced destination");
    RESET_AI_VALUE(uint32, "RTSC forced deadline");
}

bool RTSCForceMoveAction::isUseful()
{
    return IsUsableDestination(AI_VALUE(WorldPosition, "RTSC forced destination"));
}

bool RTSCForceMoveAction::Execute(Event /*event*/)
{
    WorldPosition destination = AI_VALUE(WorldPosition, "RTSC forced destination");

    if (!IsUsableDestination(destination))
        return false;

    // Give up rather than run at a wall for ever: an unreachable point, a permanent root, a bot
    // that died or got teleported off the map all end the escort here and hand it back to the
    // normal AI, which is also what arriving does.
    uint32 const deadline = AI_VALUE(uint32, "RTSC forced deadline");

    if (!bot->IsAlive() || bot->GetMapId() != destination.GetMapId() ||
        (deadline && deadline <= getMSTime()) ||
        bot->GetExactDist(destination.GetPositionX(), destination.GetPositionY(),
                          destination.GetPositionZ()) <= sPlayerbotAIConfig.rtscForceMoveArriveDistance)
    {
        ClearDestination();
        return false;
    }

    // Our own spline is still running and its MOVEMENT_FORCED window is still refusing every
    // combat chase, so there is nothing to re-issue this tick. Returning false lets the engine
    // fall through to the rest of the queue - the bot fights back on the way instead of jogging
    // past everything with its weapon sheathed.
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_FORCED))
        return false;

    if (MoveTo(destination.GetMapId(), destination.GetPositionX(), destination.GetPositionY(),
               destination.GetPositionZ(), false, false, false, false,
               MovementPriority::MOVEMENT_FORCED))
        return true;

    // Refused. The usual reason is IsDuplicateMove: re-issuing the same point counts as a
    // duplicate for a fixed MaxWaitForMove (5 s), which outlasts the shorter window MoveTo granted
    // us on a move of less than 5 s of travel. In that gap nothing holds the movement any more and
    // a combat chase would claim it. Re-stamp the hold at MOVEMENT_FORCED instead - the spline we
    // already started keeps running, and the stamp keeps refusing lower-priority movement for the
    // actions that run after this one.
    if (bot->isMoving())
        SetNextMovementDelay(1000.0f);

    return false;
}

bool RTSCForceMoveTrigger::IsActive()
{
    return RTSCForceMoveAction::IsUsableDestination(AI_VALUE(WorldPosition, "RTSC forced destination"));
}
