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
