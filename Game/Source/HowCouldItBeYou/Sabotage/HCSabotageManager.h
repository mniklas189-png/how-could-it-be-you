#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Sabotage/HCSabotageRules.h"
#include "HCSabotageManager.generated.h"

class AHCCharacter;
class AHCDoor;
class AHCGameMode;
class AHCPlayerController;
class AHCPlayerState;
class AHCPowerSector;

/**
 * Sabotage on the server (docs/02 §11): power outages per sector and door locks, their cooldowns, the automatic
 * emergency power and the announcements for everyone. Exists only on the server and is not replicated; what
 * players see lives in AHCPowerSector, AHCDoor and (cooldowns, Wandler only) AHCPlayerState.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCSabotageManager : public AInfo
{
	GENERATED_BODY()

public:
	AHCSabotageManager();

	void Init(AHCGameMode* InGameMode);

	/** Request of the Wandler. Targets: sector "North" / "East" / "SouthWest", or the id of a door. */
	EHCSabotageReject RequestSabotage(const AHCPlayerState* Who, EHCSabotageType Type, FName TargetId, AHCPlayerController* Requester);

	/** hc.Sabotage: without the rules (phase, role, cooldowns). False if the target does not exist. */
	bool ForceSabotage(EHCSabotageType Type, FName TargetId);

	/** Fuse box or emergency power: the sector has power again and the cooldown starts. Who = null: automatic. */
	void RepairPower(uint8 SectorIndex, const AHCCharacter* Who);

	/** New round: everything repaired, no cooldowns. */
	void ResetForRound();
	/** Round start and end of a meeting: no sabotage for the start delay (docs/01 §7 rule 5). */
	void OnPlayResumed();
	/** A meeting lifts every door lock at once; an outage stays. */
	void OnMeetingStart();

	static FName SectorTargetId(uint8 SectorIndex);
	/** Accepts "North", "Nord", "N", "East", "Ost", "O", "SouthWest", "SW" and the numbers 1-3. */
	static bool ParseSector(const FString& Text, uint8& OutSectorIndex);

private:
	void TickSabotage();
	void StartOutage(AHCPowerSector* Sector);
	void PublishTimes() const;
	AHCDoor* FindDoor(FName DoorId) const;
	bool IsAnySectorDark() const;
	double GetNow() const;

	UPROPERTY(Transient)
	TObjectPtr<AHCGameMode> GameMode;

	double EarliestTime = 0.0;
	double PowerReadyTime = 0.0;
	double DoorReadyTime = 0.0;
	/** Reachability with every sabotageable door locked was checked when the map was built. */
	bool bLayoutSafeForLocks = true;
	TWeakObjectPtr<AHCDoor> PendingDoor;
	TWeakObjectPtr<AHCDoor> LockedDoor;
	TWeakObjectPtr<AHCPlayerController> DoorRequester;
	FTimerHandle TickTimer;
};
