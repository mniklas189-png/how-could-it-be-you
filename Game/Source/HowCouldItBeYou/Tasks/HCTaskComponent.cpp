#include "Tasks/HCTaskComponent.h"

#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCGhostPawn.h"
#include "Core/HCGameState.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameSettings.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Tasks/HCTaskStation.h"

UHCTaskComponent::UHCTaskComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

double UHCTaskComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : 0.0;
}

int32 UHCTaskComponent::CountCompleted() const
{
	int32 Count = 0;
	for (const FHCTaskEntry& Entry : Tasks)
	{
		Count += Entry.bCompleted ? 1 : 0;
	}
	return Count;
}

float UHCTaskComponent::GetWaitSeconds(const FHCTaskEntry& Entry) const
{
	if (Entry.bCompleted || Entry.StepAvailableAt <= 0.0)
	{
		return 0.f;
	}
	return static_cast<float>(FMath::Max(0.0, Entry.StepAvailableAt - GetServerTime()));
}

int32 UHCTaskComponent::FindPendingIndexAt(const AHCTaskStation* Station, float* OutWaitSeconds) const
{
	if (!Station)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		const FHCTaskEntry& Entry = Tasks[Index];
		if (!Entry.bCompleted && Entry.TaskId == Station->GetTaskId() && Entry.CurrentStep == Station->GetStepIndex())
		{
			if (OutWaitSeconds)
			{
				*OutWaitSeconds = GetWaitSeconds(Entry);
			}
			return Index;
		}
	}
	return INDEX_NONE;
}

void UHCTaskComponent::ServerAssign(const TArray<FName>& TaskIds, bool bInFake)
{
	check(GetOwner()->HasAuthority());
	Tasks.Reset();
	bFake = bInFake;
	ActiveStation.Reset();
	for (const FName Id : TaskIds)
	{
		const FHCTaskDefinition* Def = UHCGameSettings::Get()->FindTask(Id);
		if (!Def || Def->Steps.IsEmpty())
		{
			continue;
		}
		FHCTaskEntry Entry;
		Entry.TaskId = Id;
		Entry.NumSteps = static_cast<uint8>(Def->Steps.Num());
		Tasks.Add(Entry);
	}
}

void UHCTaskComponent::ServerClear()
{
	Tasks.Reset();
	bFake = false;
	ActiveStation.Reset();
}

void UHCTaskComponent::ServerAdvance(int32 Index)
{
	if (!Tasks.IsValidIndex(Index) || Tasks[Index].bCompleted)
	{
		return;
	}
	FHCTaskEntry& Entry = Tasks[Index];
	const FHCTaskDefinition* Def = UHCGameSettings::Get()->FindTask(Entry.TaskId);
	const float WaitAfter = Def && Def->Steps.IsValidIndex(Entry.CurrentStep) ? Def->Steps[Entry.CurrentStep].WaitAfter : 0.f;
	++Entry.CurrentStep;
	AHCPlayerState* PS = Cast<AHCPlayerState>(GetOwner());
	const FString PlayerName = PS ? PS->GetPlayerName() : FString(TEXT("?"));
	if (Entry.CurrentStep >= Entry.NumSteps)
	{
		Entry.bCompleted = true;
		Entry.StepAvailableAt = 0.0;
		UE_LOG(LogHCMatch, Log, TEXT("Task done: %s finished %s%s"), *PlayerName, *Entry.TaskId.ToString(), bFake ? TEXT(" [fake, not counted]") : TEXT(""));
		if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
		{
			GM->NotifyTaskCompleted(PS, bFake);
		}
	}
	else
	{
		Entry.StepAvailableAt = WaitAfter > 0.f ? GetServerTime() + WaitAfter : 0.0;
		UE_LOG(LogHCMatch, Log, TEXT("Task step: %s did step %d/%d of %s"), *PlayerName, Entry.CurrentStep, Entry.NumSteps, *Entry.TaskId.ToString());
	}
}

void UHCTaskComponent::ServerCompleteHoldStep(AHCTaskStation* Station)
{
	float Wait = 0.f;
	const int32 Index = FindPendingIndexAt(Station, &Wait);
	if (Index != INDEX_NONE && Wait <= 0.f)
	{
		ServerAdvance(Index);
	}
}

bool UHCTaskComponent::ServerBeginTimedStep(AHCTaskStation* Station)
{
	float Wait = 0.f;
	if (FindPendingIndexAt(Station, &Wait) == INDEX_NONE || Wait > 0.f)
	{
		return false;
	}
	ActiveStation = Station;
	ActiveStartTime = GetServerTime();
	return true;
}

bool UHCTaskComponent::ServerCompleteTimedStep(AHCTaskStation* Station, const APawn* Who, FString& OutRejectReason)
{
	float Wait = 0.f;
	const int32 Index = FindPendingIndexAt(Station, &Wait);
	if (Index == INDEX_NONE || Wait > 0.f)
	{
		OutRejectReason = TEXT("step is not pending at this station");
		return false;
	}
	if (ActiveStation.Get() != Station)
	{
		OutRejectReason = TEXT("step was not started");
		return false;
	}
	// Living players and ghosts can do tasks; ghosts only while the round is in play
	const AHCCharacter* Character = Cast<AHCCharacter>(Who);
	const AHCGhostPawn* Ghost = Cast<AHCGhostPawn>(Who);
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	if ((!Character && !Ghost) || (Character && !Character->CanAct()) || (Ghost && (!GS || GS->GetMatchPhase() != EHCMatchPhase::Playing)))
	{
		OutRejectReason = TEXT("player cannot act right now");
		return false;
	}
	const FHCTaskDefinition* Def = UHCGameSettings::Get()->FindTask(Tasks[Index].TaskId);
	const float Duration = Def && Def->Steps.IsValidIndex(Tasks[Index].CurrentStep) ? Def->Steps[Tasks[Index].CurrentStep].Duration : 0.f;
	const double Elapsed = GetServerTime() - ActiveStartTime;
	// Minigames run locally; a cheater can solve them automatically but not faster than this (docs/02 §9.4)
	if (Elapsed < Duration * 0.9)
	{
		OutRejectReason = FString::Printf(TEXT("too fast: %.1f s of %.1f s"), Elapsed, Duration);
		return false;
	}
	// Still at the station?
	const FVector Eye = Character ? Character->GetCamera()->GetComponentLocation() : Ghost->GetActorLocation();
	const float MaxRange = Character ? UHCGameSettings::Get()->InteractionRange + 60.f : 280.f;
	if (Station->GetComponentsBoundingBox().ComputeSquaredDistanceToPoint(Eye) > FMath::Square(MaxRange))
	{
		OutRejectReason = TEXT("too far from the station");
		return false;
	}
	ActiveStation.Reset();
	ServerAdvance(Index);
	return true;
}

void UHCTaskComponent::ServerCancelTimedStep()
{
	ActiveStation.Reset();
}

void UHCTaskComponent::ServerCompleteAll()
{
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		while (!Tasks[Index].bCompleted)
		{
			ServerAdvance(Index);
		}
	}
}

void UHCTaskComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UHCTaskComponent, Tasks, COND_OwnerOnly);
}
