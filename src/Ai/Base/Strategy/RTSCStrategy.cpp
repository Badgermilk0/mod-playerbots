/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RTSCStrategy.h"
#include "Playerbots.h"

RTSCStrategy::RTSCStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

// Relevance 100 - above ACTION_EMERGENCY (90) and the same relevance the "see spell" packet
// handler runs at. It has to be evaluated before any combat action can claim the tick, because
// MoveTo caps its no-interruption window at AiPlayerbot.MaxWaitForMove and the forced move must be
// re-issued in the very tick that window lapses. The trigger only fires while a forced
// destination is stored, so an unforced bot pays nothing for this.
void RTSCStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(new TriggerNode("rtsc forced move", {NextAction("rtsc forced move", 100.0f)}));
}
