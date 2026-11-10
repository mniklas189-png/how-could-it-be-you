// Test bots for automated round tests (Development builds only). They use the real paths a player uses:
// the interaction component, the kill component and the task overlays - only the walking is replaced by
// debug teleports.
//   -HCAutoTasks      work through the own task list (living players and ghosts)
//   -HCAutoKill       as the Wandler: kill a crew member whenever the cooldown allows
//   -HCAutoReport     as a living player: report remains
//   -HCAutoKillMe=<s> die through hc.KillMe after <s> seconds of play
//   -HCGhostCheck     log every few seconds which ghosts and monster markers this machine can see

#include "Core/HCPlayerController.h"

#include "Abilities/HCKillComponent.h"
#include "Abilities/HCRemains.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCGhostPawn.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/HCInteractionComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"
#include "UI/HCHUD.h"
#include "UnrealClient.h"
#include "World/HCStationBuilder.h"

void AHCPlayerController::TickAutoTasks(double Now)
{
#if !UE_BUILD_SHIPPING
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	AHCGhostPawn* Ghost = Cast<AHCGhostPawn>(GetPawn());
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	const UHCTaskComponent* Tasks = PS ? PS->GetTaskComponent() : nullptr;
	if ((!HCChar && !Ghost) || !Tasks || (HCChar && HCChar->IsKillLocked()))
	{
		SmokeTaskState = 0;
		return;
	}
	if (SmokeTaskState == 0)
	{
		if (Now < SmokeTaskNextTime)
		{
			return;
		}
		// Next open step that can be started now
		for (const FHCTaskEntry& Entry : Tasks->GetTasks())
		{
			if (Entry.bCompleted || Tasks->GetWaitSeconds(Entry) > 0.f)
			{
				continue;
			}
			for (TActorIterator<AHCTaskStation> It(GetWorld()); It; ++It)
			{
				if (It->GetTaskId() == Entry.TaskId && It->GetStepIndex() == Entry.CurrentStep)
				{
					SmokeTaskStation = *It;
					SmokeTaskId = Entry.TaskId;
					SmokeTaskStep = Entry.CurrentStep;
					SmokeTaskSince = Now;
					SmokeTaskState = 1;
					ServerDebugCommand(TEXT("GotoStation"), { Entry.TaskId.ToString(), FString::FromInt(Entry.CurrentStep) });
					return;
				}
			}
			UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR no station on this machine for %s step %d"), *Entry.TaskId.ToString(), Entry.CurrentStep);
		}
		if (Tasks->GetTasks().Num() > 0 && Tasks->CountOpen() == 0 && !bSmokeTasksDoneLogged)
		{
			bSmokeTasksDoneLogged = true;
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] All %d own tasks done%s"), Tasks->GetTasks().Num(), Ghost ? TEXT(" (as ghost)") : TEXT(""));
		}
		SmokeTaskNextTime = Now + 0.5;
		return;
	}

	AHCTaskStation* Station = SmokeTaskStation.Get();
	bool bAdvanced = Station == nullptr;
	for (const FHCTaskEntry& Entry : Tasks->GetTasks())
	{
		if (Entry.TaskId == SmokeTaskId)
		{
			bAdvanced = Entry.bCompleted || Entry.CurrentStep != SmokeTaskStep;
		}
	}
	if (bAdvanced)
	{
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Task %s step %d done after %.1f s%s"), *SmokeTaskId.ToString(), SmokeTaskStep, Now - SmokeTaskSince, Ghost ? TEXT(" (as ghost)") : TEXT(""));
		SmokeTaskState = 0;
		SmokeTaskNextTime = Now + 0.5;
		return;
	}
	if (Now - SmokeTaskSince > 45.0)
	{
		UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR task %s step %d timed out in state %d"), *SmokeTaskId.ToString(), SmokeTaskStep, SmokeTaskState);
		SmokeTaskState = 0;
		SmokeTaskNextTime = Now + 2.0;
		return;
	}

	const AHCHUD* HUD = GetHUD<AHCHUD>();
	const bool bOverlay = HUD && HUD->IsTaskOverlayOpen();
	if (SmokeTaskState == 1 && Now - SmokeTaskSince > 0.8)
	{
		bool bStarted = false;
		if (HCChar)
		{
			// Look at the station, then press [E] once the interaction component has it in focus
			SetControlRotation((Station->GetActorLocation() - HCChar->GetCamera()->GetComponentLocation()).Rotation());
			UHCInteractionComponent* Interaction = HCChar->GetInteraction();
			if (Interaction->GetFocusedActor() == Station && Interaction->IsFocusAvailable())
			{
				Interaction->BeginInteract();
				bStarted = true;
			}
		}
		else if (Ghost->GetFocusedStation() == Station)
		{
			Ghost->BeginTaskInteract();
			bStarted = true;
		}
		if (bStarted)
		{
			SmokeTaskState = 2;
			SmokeTaskInteractAt = Now;
			SmokeTaskIdleSince = 0.0;
			bSmokeOverlayShot = false;
		}
	}
	else if (SmokeTaskState == 2 && Now - SmokeTaskInteractAt > 2.0)
	{
		if (bOverlay && !bSmokeOverlayShot && bSmokePhaseShots)
		{
			bSmokeOverlayShot = true;
			FString Dir;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("overlay_%s_%s%s.png"), *SmokeTaskId.ToString(),
					GetNetMode() == NM_Client ? TEXT("client") : TEXT("host"), Ghost ? TEXT("_ghost") : TEXT(""))), true, false);
			}
		}
		const float HoldProgress = HCChar ? HCChar->GetInteraction()->GetHoldProgress() : Ghost->GetHoldProgress();
		const bool bIdle = !bOverlay && HoldProgress < 0.f;
		if (!bIdle)
		{
			SmokeTaskIdleSince = 0.0;
		}
		else if (SmokeTaskIdleSince <= 0.0)
		{
			SmokeTaskIdleSince = Now;
		}
		if (bIdle && Now - SmokeTaskIdleSince > 2.0)
		{
			// The step was cancelled: go back and try again
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Retry %s step %d"), *SmokeTaskId.ToString(), SmokeTaskStep);
			SmokeTaskIdleSince = 0.0;
			SmokeTaskState = 0;
			SmokeTaskNextTime = Now + 0.3;
		}
	}
#endif
}

bool AHCPlayerController::TickAutoKill(double Now)
{
#if !UE_BUILD_SHIPPING
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	if (!HCChar || !PS || PS->GetRoleInfo().Role != EHCRole::Monster)
	{
		return false;
	}
	UHCKillComponent* Kill = HCChar->GetKillComponent();
	if (HCChar->IsKillLocked())
	{
		SmokeKillState = 0;
		return true; // attack is running
	}
	if (Kill->GetCooldownRemaining() > 0.f)
	{
		SmokeKillState = 0;
		return false; // back to fake tasks while waiting
	}
	if (SmokeKillState == 0)
	{
		// A living crew member outside the kill-free command center
		AHCStationBuilder* Builder = nullptr;
		for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
		{
			Builder = *It;
		}
		for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
		{
			const AHCPlayerState* OtherPS = It->GetPlayerState<AHCPlayerState>();
			if (*It == HCChar || !OtherPS || It->IsKillLocked())
			{
				continue;
			}
			if (Builder && Builder->GetSpaceNameAt(It->GetActorLocation()) == Builder->GetLayout().SpawnRoom)
			{
				continue;
			}
			SmokeKillTarget = *It;
			SmokeKillSince = Now;
			SmokeKillState = 1;
			SmokeTaskState = 0;
			ServerDebugCommand(TEXT("GotoPlayer"), { OtherPS->GetPlayerName() });
			return true;
		}
		return false;
	}
	AHCCharacter* Target = SmokeKillTarget.Get();
	if (!Target || Now - SmokeKillSince > 4.0)
	{
		SmokeKillState = 0; // target moved on: pick again
		return true;
	}
	SetControlRotation((Target->GetActorLocation() + FVector(0.f, 0.f, 30.f) - HCChar->GetCamera()->GetComponentLocation()).Rotation());
	if (Now - SmokeKillSince > 0.6 && Kill->GetLocalTarget() == Target)
	{
		const APlayerState* TargetPS = Target->GetPlayerState();
		if (!bSmokeMonsterShot && bSmokePhaseShots)
		{
			// First the picture of the kill prompt, the attempt follows on the next pass
			bSmokeMonsterShot = true;
			FString Dir;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, TEXT("monster_target.png")), true, false);
			}
			SmokeKillSince = Now - 0.3;
			return true;
		}
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Kill attempt on %s"), TargetPS ? *TargetPS->GetPlayerName() : TEXT("?"));
		Kill->TryKill();
		SmokeKillSince = Now - 3.0; // if nothing happens, pick again in a second
	}
	return true;
#else
	return false;
#endif
}

bool AHCPlayerController::TickAutoReport(double Now)
{
#if !UE_BUILD_SHIPPING
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	if (!HCChar || HCChar->IsKillLocked())
	{
		return false;
	}
	AHCRemains* Remains = SmokeReportTarget.Get();
	if (!Remains)
	{
		for (TActorIterator<AHCRemains> It(GetWorld()); It; ++It)
		{
			if (!It->IsReported() && !SmokeReportTried.Contains(*It))
			{
				SmokeReportTarget = *It;
				SmokeReportTried.Add(*It);
				SmokeReportSince = Now;
				bSmokeReportPressed = false;
				SmokeTaskState = 0;
				ServerDebugCommand(TEXT("GotoRemains"), {});
				return true;
			}
		}
		return false;
	}
	if (Remains->IsReported() || Now - SmokeReportSince > 6.0)
	{
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Report of %s: %s"), *Remains->GetVictimName(), Remains->IsReported() ? TEXT("done") : TEXT("FAILED"));
		if (!Remains->IsReported())
		{
			UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR remains could not be reported"));
		}
		SmokeReportTarget.Reset();
		return false;
	}
	if (Now - SmokeReportSince > 0.8 && !bSmokeReportPressed)
	{
		UHCInteractionComponent* Interaction = HCChar->GetInteraction();
		if (Interaction->GetFocusedActor() == Remains && Interaction->IsFocusAvailable())
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Prompt at remains: %s"), *Interaction->GetPromptText().ToString());
			Interaction->BeginInteract();
			bSmokeReportPressed = true;
		}
	}
	return true;
#else
	return false;
#endif
}

void AHCPlayerController::TickGhostCheck(double Now)
{
#if !UE_BUILD_SHIPPING
	const AHCPlayerState* OwnPS = GetPlayerState<AHCPlayerState>();
	if (OwnPS && !OwnPS->IsAlive())
	{
		if (SmokeDeadSince <= 0.0)
		{
			SmokeDeadSince = Now;
		}
		// A look around as a ghost, a few seconds after death: towards the nearest living player
		if (!bSmokeGhostShot && Now - SmokeDeadSince > 5.5 && bSmokePhaseShots && GetPawn())
		{
			bSmokeGhostShot = true;
			for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
			{
				if (It->IsMonsterMarkerVisible())
				{
					SetControlRotation((It->GetActorLocation() - GetPawn()->GetActorLocation()).Rotation());
				}
			}
			FString Dir;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("ghostview_%s.png"), *OwnPS->GetPlayerName())), true, false);
			}
		}
	}
	if (Now < SmokeGhostCheckNext)
	{
		return;
	}
	SmokeGhostCheckNext = Now + 4.0;
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	int32 GhostActors = 0, GhostsDrawn = 0, Markers = 0, Remains = 0;
	for (TActorIterator<AHCGhostPawn> It(GetWorld()); It; ++It)
	{
		if (*It != GetPawn())
		{
			++GhostActors;
			GhostsDrawn += It->IsMeshVisibleLocally() ? 1 : 0;
		}
	}
	for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
	{
		Markers += It->IsMonsterMarkerVisible() ? 1 : 0;
	}
	for (TActorIterator<AHCRemains> It(GetWorld()); It; ++It)
	{
		++Remains;
	}
	UE_LOG(LogHC, Display, TEXT("[HCSmoke] View check: alive=%d otherGhostActors=%d ghostsDrawn=%d monsterMarkers=%d remains=%d"),
		PS && PS->IsAlive() ? 1 : 0, GhostActors, GhostsDrawn, Markers, Remains);
#endif
}
