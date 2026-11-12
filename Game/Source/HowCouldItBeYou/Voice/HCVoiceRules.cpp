#include "Voice/HCVoiceRules.h"

bool HCVoiceRules::IsRound(EHCMatchPhase Phase)
{
	return Phase != EHCMatchPhase::Lobby && Phase != EHCMatchPhase::Loading && Phase != EHCMatchPhase::GameOver;
}

bool HCVoiceRules::IsMeeting(EHCMatchPhase Phase)
{
	return Phase == EHCMatchPhase::Meeting || Phase == EHCMatchPhase::Ejection;
}

bool HCVoiceRules::CanHear(EHCVoiceChatRule Rule, EHCMatchPhase Phase, bool bTalkerAlive, bool bListenerAlive)
{
	if (!IsRound(Phase))
	{
		return true;
	}
	if (!bTalkerAlive)
	{
		return !bListenerAlive; // the dead only among themselves, in every rule
	}
	// The living: outside a meeting only if the rule allows talking during the round
	return Rule != EHCVoiceChatRule::MeetingsOnly || IsMeeting(Phase);
}

bool HCVoiceRules::CanSend(EHCVoiceChatRule Rule, EHCMatchPhase Phase, bool bAlive)
{
	// The dead are always heard by the other dead; the living by everybody, or by nobody
	return !bAlive || CanHear(Rule, Phase, true, true);
}

bool HCVoiceRules::UsesDistance(EHCVoiceChatRule Rule, EHCMatchPhase Phase)
{
	return Rule != EHCVoiceChatRule::Always && !IsMeeting(Phase);
}

FText HCVoiceRules::Label(EHCVoiceChatRule Rule)
{
	// Short: three buttons side by side in the lobby panel; Description says the whole rule
	switch (Rule)
	{
	case EHCVoiceChatRule::MeetingsOnly: return NSLOCTEXT("HC", "VoiceRuleMeetings", "MEETINGS");
	case EHCVoiceChatRule::Always:       return NSLOCTEXT("HC", "VoiceRuleAlways", "IMMER");
	default:                             return NSLOCTEXT("HC", "VoiceRuleNear", "NÄHE");
	}
}

FText HCVoiceRules::Description(EHCVoiceChatRule Rule)
{
	switch (Rule)
	{
	case EHCVoiceChatRule::MeetingsOnly:
		return NSLOCTEXT("HC", "VoiceRuleMeetingsInfo", "Nur in Meetings: sonst hört in der Runde niemand die Lebenden. Tote reden untereinander.");
	case EHCVoiceChatRule::Always:
		return NSLOCTEXT("HC", "VoiceRuleAlwaysInfo", "Immer: alle hören sich überall gleich laut. Tote hören nur Tote.");
	default:
		return NSLOCTEXT("HC", "VoiceRuleNearInfo", "Nähe + Meetings: ihr hört, wer in der Nähe ist, im Meeting alle. Tote hören nur Tote.");
	}
}
