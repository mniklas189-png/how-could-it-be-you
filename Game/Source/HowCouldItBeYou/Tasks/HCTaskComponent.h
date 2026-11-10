#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Tasks/HCTaskTypes.h"
#include "HCTaskComponent.generated.h"

class AHCCharacter;
class APawn;
class AHCTaskStation;

/**
 * The personal task list of a player with progress per task (docs/02 §9.1). Lives on AHCPlayerState, so it
 * survives death, and is replicated ONLY to the owner. The monster has a fake list of the same shape:
 * same interaction, same duration, but the server counts nothing.
 */
UCLASS(ClassGroup = (HC))
class HOWCOULDITBEYOU_API UHCTaskComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHCTaskComponent();

	const TArray<FHCTaskEntry>& GetTasks() const { return Tasks; }
	int32 CountCompleted() const;
	int32 CountOpen() const { return Tasks.Num() - CountCompleted(); }

	/**
	 * Index of the task whose current step belongs to this station, INDEX_NONE if none.
	 * OutWaitSeconds > 0 means the step is not available yet (e.g. the centrifuge is still running).
	 */
	int32 FindPendingIndexAt(const AHCTaskStation* Station, float* OutWaitSeconds = nullptr) const;

	/** Seconds until the current step of this entry can be started (0 = now). */
	float GetWaitSeconds(const FHCTaskEntry& Entry) const;

	// --- Server ---
	void ServerAssign(const TArray<FName>& TaskIds, bool bInFake);
	void ServerClear();
	bool IsFakeList() const { return bFake; }

	/** Hold step finished (the interaction component measured the time on the server). */
	void ServerCompleteHoldStep(AHCTaskStation* Station);

	/** Sequence/minigame step started: remembers station and start time for the minimum-duration check. */
	bool ServerBeginTimedStep(AHCTaskStation* Station);

	/** Sequence/minigame step reported as done by the client; validated here (docs/02 §9.4). */
	bool ServerCompleteTimedStep(AHCTaskStation* Station, const APawn* Who, FString& OutRejectReason);

	void ServerCancelTimedStep();

	/** hc.CompleteAllTasks */
	void ServerCompleteAll();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void ServerAdvance(int32 Index);
	double GetServerTime() const;

	UPROPERTY(Replicated)
	TArray<FHCTaskEntry> Tasks;

	/** Server only: never replicated, the list looks the same for crew and monster. */
	bool bFake = false;

	TWeakObjectPtr<AHCTaskStation> ActiveStation;
	double ActiveStartTime = 0.0;
};
