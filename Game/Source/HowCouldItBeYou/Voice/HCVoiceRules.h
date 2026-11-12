#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"

/**
 * Who hears whom, by the voice chat rule the host chose in the lobby (EHCVoiceChatRule). Pure functions, so that the
 * host's mute lists, the own microphone and the volume all follow the same rules (Tests/HCVoiceRuleTests.cpp).
 *
 * Outside a round (lobby, loading, result screen) everybody may talk. In a round the dead are only heard by the dead.
 */
namespace HCVoiceRules
{
	/** A round runs: from the intro to the last ejection. */
	HOWCOULDITBEYOU_API bool IsRound(EHCMatchPhase Phase);
	/** Meeting or the ejection screen after it. */
	HOWCOULDITBEYOU_API bool IsMeeting(EHCMatchPhase Phase);

	/** Host: may the voice of this talker reach this listener? */
	HOWCOULDITBEYOU_API bool CanHear(EHCVoiceChatRule Rule, EHCMatchPhase Phase, bool bTalkerAlive, bool bListenerAlive);
	/** Is there anybody who could hear this player now? If not, the microphone stays closed. */
	HOWCOULDITBEYOU_API bool CanSend(EHCVoiceChatRule Rule, EHCMatchPhase Phase, bool bAlive);
	/** Do voices get quieter with the distance now? (Not in meetings, where everybody sits at the table.) */
	HOWCOULDITBEYOU_API bool UsesDistance(EHCVoiceChatRule Rule, EHCMatchPhase Phase);

	/** Name of the rule for the lobby buttons, and one sentence what it means. */
	HOWCOULDITBEYOU_API FText Label(EHCVoiceChatRule Rule);
	HOWCOULDITBEYOU_API FText Description(EHCVoiceChatRule Rule);
}
