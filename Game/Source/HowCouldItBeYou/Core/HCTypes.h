#pragma once

#include "CoreMinimal.h"
#include "HCTypes.generated.h"

/** Match phases (docs/02 §5.1). Loading = waiting until every traveller has loaded the match map. */
UENUM(BlueprintType)
enum class EHCMatchPhase : uint8
{
	Lobby,
	Loading,
	Intro,
	RoleReveal,
	Playing,
	Meeting,
	Ejection,
	GameOver
};

UENUM(BlueprintType)
enum class EHCRole : uint8
{
	Crew,
	Monster
};

UENUM(BlueprintType)
enum class EHCWinner : uint8
{
	None,
	Crew,
	Monster
};

/**
 * Lobby option chosen by the host: when the players hear each other (Voice/HCVoiceRules.h). In every rule the dead
 * are only heard by the dead while a round runs.
 */
UENUM(BlueprintType)
enum class EHCVoiceChatRule : uint8
{
	/** In a round only during meetings; the dead can still talk among themselves. */
	MeetingsOnly,
	/** Who stands near you, quieter with the distance; in meetings everybody. */
	Proximity,
	/** Everybody, everywhere, equally loud. */
	Always
};

/** Personal setting, not a lobby option (docs/01 §10). */
UENUM(BlueprintType)
enum class EHCJumpscareIntensity : uint8
{
	/** Close-up, stinger, white flash, shake. */
	Full,
	/** No white flash, stinger about 12 dB quieter, less shake. */
	Reduced,
	/** No close-up: a gentle fade to black with a dull sound. */
	Minimal
};

/** Own role, replicated only to the owner. MatchSerial makes every assignment a real change (docs/02 §2.4). */
USTRUCT(BlueprintType)
struct FHCRoleInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	EHCRole Role = EHCRole::Crew;

	UPROPERTY(BlueprintReadOnly)
	uint8 MatchSerial = 0;
};

/** Filled at GameOver for everyone: who was what. */
USTRUCT(BlueprintType)
struct FHCRevealedRole
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString PlayerName;

	UPROPERTY(BlueprintReadOnly)
	int32 Identity = 0;

	UPROPERTY(BlueprintReadOnly)
	EHCRole Role = EHCRole::Crew;

	UPROPERTY(BlueprintReadOnly)
	bool bAlive = true;
};

/** Shapeshift states of the monster (docs/02 §6.3). "Ready" includes the running cooldown. */
UENUM(BlueprintType)
enum class EHCShapeshiftState : uint8
{
	Ready,
	Transforming,
	Disguised,
	Reverting
};

/** What everyone sees of a figure: suit colour (identity) and the name on the badge. A disguise copies exactly this. */
USTRUCT(BlueprintType)
struct FHCAppearance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 Identity = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	FString DisplayName;
};

/** Stages inside the phase Meeting (docs/02 §8.3). */
UENUM(BlueprintType)
enum class EHCMeetingStage : uint8
{
	None,
	Gathering,
	Discussion,
	Voting,
	Results
};

/** One line of a vote result, filled only when the vote is over (docs/02 §8.4). TargetPlayerId -1 = skipped. */
USTRUCT(BlueprintType)
struct FHCVoteResultEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 TargetPlayerId = -1;

	UPROPERTY(BlueprintReadOnly)
	int32 Count = 0;

	/** Empty with anonymous votes. */
	UPROPERTY(BlueprintReadOnly)
	TArray<int32> VoterIds;
};

/** The 8 researcher identities: colour + symbol (+ the player's name), colour-blind safe (docs/01 §13). */
namespace HCIdentity
{
	constexpr int32 Count = 8;
	HOWCOULDITBEYOU_API FLinearColor Color(int32 Index);
	HOWCOULDITBEYOU_API FText ColorName(int32 Index);
	HOWCOULDITBEYOU_API FText SymbolName(int32 Index);
	HOWCOULDITBEYOU_API FText RoleName(EHCRole Role);
	HOWCOULDITBEYOU_API const TCHAR* PhaseName(EHCMatchPhase Phase);
}
