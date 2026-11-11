#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "HCChat.generated.h"

/**
 * One line of the meeting chat as it reaches a player. The server sends every line on its own to each player who
 * may read it, so a ghost's line never even reaches the machine of a living player (docs/01 §4.3: ghosts speak only
 * among themselves, they hear the living in the meeting).
 */
USTRUCT()
struct HOWCOULDITBEYOU_API FHCChatLine
{
	GENERATED_BODY()

	/** INDEX_NONE = no player: the line marks the start of a meeting. */
	UPROPERTY()
	int32 SenderId = INDEX_NONE;

	UPROPERTY()
	FString SenderName;

	UPROPERTY()
	int32 SenderIdentity = INDEX_NONE;

	/** Written by a ghost; only ghosts receive it. */
	UPROPERTY()
	bool bFromGhost = false;

	UPROPERTY()
	FString Text;

	/** Which meeting of the round (1, 2, ...). */
	UPROPERTY()
	uint8 MeetingNumber = 0;

	/** AHCGameState::GetMatchSerial of the round; the first line of a new round starts an empty chat. */
	UPROPERTY()
	uint8 MatchSerial = 0;

	/** Running number on the receiving machine; not sent. */
	int32 LocalSerial = 0;

	bool IsMeetingStart() const { return SenderId == INDEX_NONE; }
};

/** Rules of the meeting chat. Pure functions, covered by HCChatTests. */
namespace HCChat
{
	/** Characters per line. */
	constexpr int32 MaxLength = 120;
	/** Lines each machine keeps; older ones drop out. */
	constexpr int32 MaxLines = 80;
	/** Seconds between two lines of one player. The server allows a little less because of network jitter. */
	constexpr double MinInterval = 0.8;
	constexpr double ServerMinInterval = 0.6;

	/**
	 * Trims, turns line breaks, tabs and other control characters into spaces, merges runs of spaces, drops
	 * invisible direction and zero-width marks (they could disguise a line) and cuts to MaxLength.
	 * Empty = nothing to send.
	 */
	HOWCOULDITBEYOU_API FString Sanitize(const FString& Raw);

	/** The living never read ghosts; ghosts read everyone. */
	inline bool CanRead(bool bSenderAlive, bool bReaderAlive) { return bSenderAlive || !bReaderAlive; }

	/** The chat is open for the whole meeting, from the gathering to the result, but not during the ejection. */
	inline bool IsOpen(EHCMatchPhase Phase, EHCMeetingStage Stage)
	{
		return Phase == EHCMatchPhase::Meeting && Stage != EHCMeetingStage::None;
	}
}
