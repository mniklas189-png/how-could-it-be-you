// Test bots for automated round tests (Development builds only). They use the real paths a player uses:
// the interaction component, the kill component and the task overlays - only the walking is replaced by
// debug teleports.
//   -HCAutoTasks      work through the own task list (living players and ghosts)
//   -HCAutoKill       as the Wandler: kill a crew member whenever the cooldown allows
//   -HCAutoReport     as a living player: report remains
//   -HCAutoKillMe=<s> die through hc.KillMe after <s> seconds of play
//   -HCGhostCheck     log every few seconds which ghosts and monster markers this machine can see
//   -HCAutoButton=<s> press the emergency button after <s> seconds of play
//   -HCAutoVote=<Name|skip|none>  vote in every meeting for that player (skip if dead or unknown)
//   -HCAutoChat=<text>  write this line into the meeting chat once per meeting ({name} = own name); every
//                     machine logs the chat lines it receives ("Chat in:")
//   -HCDiscussion=<s> -HCVoting=<s>  shorter meeting stages (read by UHCMatchRules)
//   -HCAutoShift      as the Wandler: shapeshift whenever possible; all bots log the looks they see
//   -HCShiftRevert=<s>  end the disguise with Q after <s> seconds
//   -HCAutoTrueForm   as the Wandler: show the true form whenever possible; crew bots take a picture of it
//   -HCShiftDuration=<s> -HCShiftCooldown=<s>  shorter shapeshift times (read by UHCMatchRules)
//   -HCAutoSabotage   as the Wandler: cut the power and lock doors in turn, through the station map
//   -HCAutoRepair     repair a dark sector at its fuse box
//   -HCSabotageDelay=<s> -HCOutageMax=<s> -HCSabotageCooldown=<s>  shorter sabotage times (UHCMatchRules)

#include "Core/HCPlayerController.h"

#include "Abilities/HCKillComponent.h"
#include "Abilities/HCRemains.h"
#include "Abilities/HCShapeshiftComponent.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Characters/HCGhostPawn.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/HCInteractionComponent.h"
#include "Meeting/HCEmergencyButton.h"
#include "Sabotage/HCDoor.h"
#include "Sabotage/HCFuseBox.h"
#include "Sabotage/HCPowerSector.h"
#include "Sabotage/HCSabotageManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"
#include "UI/HCHUD.h"
#include "UI/HCMeetingWidget.h"
#include "UObject/UObjectIterator.h"
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
			FString ShotDir;
			if (Interaction->GetFocusedActor() == Station && Interaction->IsFocusAvailable() && bSmokePhaseShots && !bSmokePromptShot
				&& FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), ShotDir))
			{
				// Before the very first press: let the prompt settle, then take a picture of it
				if (SmokePromptSeenAt <= 0.0)
				{
					SmokePromptSeenAt = Now;
				}
				else if (Now - SmokePromptSeenAt > 0.8)
				{
					bSmokePromptShot = true;
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(ShotDir, FString::Printf(TEXT("prompt_%s.png"), GetNetMode() == NM_Client ? TEXT("client") : TEXT("host"))), true, false);
				}
			}
			else if (Interaction->GetFocusedActor() == Station && Interaction->IsFocusAvailable())
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
		FString ShotDir;
		if (HoldProgress > 0.45f && HoldProgress < 0.8f && bSmokePhaseShots && !bSmokeHoldShot && FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), ShotDir))
		{
			bSmokeHoldShot = true;
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(ShotDir, FString::Printf(TEXT("hold_%s.png"), GetNetMode() == NM_Client ? TEXT("client") : TEXT("host"))), true, false);
		}
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

void AHCPlayerController::ParseMeetingSmokeFlags()
{
#if !UE_BUILD_SHIPPING
	if (!bSmokeMeetingParsed)
	{
		bSmokeMeetingParsed = true;
		FParse::Value(FCommandLine::Get(), TEXT("HCAutoVote="), SmokeAutoVote);
		FParse::Value(FCommandLine::Get(), TEXT("HCAutoChat="), SmokeAutoChat);
		FParse::Value(FCommandLine::Get(), TEXT("HCAutoButton="), SmokeAutoButton);
		bSmokeAutoShift = FParse::Param(FCommandLine::Get(), TEXT("HCAutoShift"));
		bSmokeAutoSabotage = FParse::Param(FCommandLine::Get(), TEXT("HCAutoSabotage"));
		bSmokeAutoRepair = FParse::Param(FCommandLine::Get(), TEXT("HCAutoRepair"));
		FParse::Value(FCommandLine::Get(), TEXT("HCShiftRevert="), SmokeShiftRevert);
	}
#endif
}

bool AHCPlayerController::TickAutoButton(double Now, double InPhase)
{
#if !UE_BUILD_SHIPPING
	ParseMeetingSmokeFlags();
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	if (SmokeAutoButton <= 0.f || SmokeButtonState == 2 || !HCChar || HCChar->IsKillLocked() || InPhase < SmokeAutoButton)
	{
		return false;
	}
	AHCEmergencyButton* Button = nullptr;
	for (TActorIterator<AHCEmergencyButton> It(GetWorld()); It; ++It)
	{
		Button = *It;
	}
	UHCInteractionComponent* Interaction = HCChar->GetInteraction();
	if (!Button || (SmokeButtonState == 1 && Now - SmokeButtonSince > 6.0))
	{
		UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR emergency button could not be pressed (%s)"), Button ? *Interaction->GetPromptText().ToString() : TEXT("no button in this map"));
		SmokeButtonState = 2;
		return false;
	}
	if (SmokeButtonState == 0)
	{
		SmokeButtonState = 1;
		SmokeButtonSince = Now;
		SmokeTaskState = 0;
		ServerDebugCommand(TEXT("GotoButton"), {});
		return true;
	}
	SetControlRotation((Button->GetActorLocation() + FVector(0.f, 0.f, 30.f) - HCChar->GetCamera()->GetComponentLocation()).Rotation());
	if (Now - SmokeButtonSince > 0.8 && Interaction->GetFocusedActor() == Button && Interaction->IsFocusAvailable())
	{
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Prompt at button: %s"), *Interaction->GetPromptText().ToString());
		Interaction->BeginInteract();
		SmokeButtonState = 2;
	}
	return true;
#else
	return false;
#endif
}

void AHCPlayerController::TickAutoMeeting(double Now)
{
#if !UE_BUILD_SHIPPING
	ParseMeetingSmokeFlags();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	if (!GS || !PS)
	{
		return;
	}
	const EHCMeetingStage Stage = GS->GetMeetingStage();
	if (static_cast<uint8>(Stage) != SmokeLastStage)
	{
		SmokeLastStage = static_cast<uint8>(Stage);
		SmokeStageSince = Now;
		bSmokeStageShot = false;
		bSmokeVoteTried = false;
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Meeting stage %d, called by %s, own seat %s"), static_cast<int32>(Stage), *GS->GetMeetingCallerName(),
			GetPawn() ? *GetPawn()->GetActorLocation().ToCompactString() : TEXT("-"));
	}
	const double InStage = Now - SmokeStageSince;
	if (Stage == EHCMeetingStage::Gathering)
	{
		bSmokeChatSent = false;
	}
	if (Stage == EHCMeetingStage::Discussion && !SmokeAutoChat.IsEmpty() && !bSmokeChatSent && InStage > 1.0)
	{
		// Through the chat box of the meeting screen, like typing and pressing Enter
		bSmokeChatSent = true;
		UHCMeetingWidget* Screen = nullptr;
		for (TObjectIterator<UHCMeetingWidget> It; It; ++It)
		{
			if (It->GetOwningPlayer() == this && It->IsInViewport())
			{
				Screen = *It;
			}
		}
		const FString Text = SmokeAutoChat.Replace(TEXT("{name}"), *PS->GetPlayerName());
		const bool bSent = Screen && Screen->SendChatText(Text);
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Chat out%s: %s (%s)"), PS->IsAlive() ? TEXT("") : TEXT(" as ghost"), *Text, bSent ? TEXT("sent") : TEXT("FAILED"));
		if (!bSent)
		{
			UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR the meeting screen did not send the chat line"));
		}
	}
	if (bSmokePhaseShots && !bSmokeStageShot && InStage > 1.0 && (Stage == EHCMeetingStage::Voting || Stage == EHCMeetingStage::Results))
	{
		bSmokeStageShot = true;
		FString Dir;
		if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("meeting_%s_%s.png"),
				Stage == EHCMeetingStage::Voting ? TEXT("voting") : TEXT("results"), *PS->GetPlayerName())), true, false);
		}
	}
	if (Stage == EHCMeetingStage::Voting && !SmokeAutoVote.IsEmpty() && !bSmokeVoteTried && InStage > 2.0 && PS->IsAlive() && !PS->HasVoted())
	{
		bSmokeVoteTried = true;
		if (SmokeAutoVote.Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			return;
		}
		int32 TargetId = -1;
		for (const APlayerState* Other : GS->PlayerArray)
		{
			const AHCPlayerState* OtherPS = Cast<AHCPlayerState>(Other);
			if (OtherPS && OtherPS->IsAlive() && OtherPS->GetPlayerName().Equals(SmokeAutoVote, ESearchCase::IgnoreCase))
			{
				TargetId = OtherPS->GetPlayerId();
			}
		}
		// Through the meeting screen, like a click on the button
		AHCHUD* HUD = GetHUD<AHCHUD>();
		const bool bSent = HUD && HUD->SubmitMeetingVote(TargetId);
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Vote for %s: %s"), TargetId == -1 ? TEXT("skip") : *SmokeAutoVote, bSent ? TEXT("sent") : TEXT("FAILED"));
		if (!bSent)
		{
			UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR the meeting screen did not accept the vote"));
		}
	}
#endif
}

bool AHCPlayerController::TickAutoShift(double Now)
{
#if !UE_BUILD_SHIPPING
	ParseMeetingSmokeFlags();
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	if (HCChar && PS && FParse::Param(FCommandLine::Get(), TEXT("HCAutoTrueForm")))
	{
		// One local player per process, so plain statics do for the test state
		static double RequestedAt = -100.0, ShotSince = 0.0;
		static bool bLogged = false;
		static int32 ShotState = 0;
		static TWeakObjectPtr<AHCCharacter> ShotTarget;
		if (PS->GetRoleInfo().Role == EHCRole::Monster)
		{
			UHCShapeshiftComponent* Shift = HCChar->GetShapeshift();
			if (Shift->GetState() == EHCShapeshiftState::Ready && Shift->GetCooldownRemaining() <= 0.f && Now - RequestedAt > 3.0 && !HCChar->IsKillLocked())
			{
				RequestedAt = Now;
				Shift->ServerRequestTrueForm();
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] True form: requested"));
			}
			if (Shift->IsTrueForm() && HCChar->IsMonsterForm() && !bLogged)
			{
				bLogged = true;
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] True form: active, top speed %.0f, %.0f s left"), HCChar->GetCharacterMovement()->GetMaxSpeed(), Shift->GetStateRemaining());
			}
		}
		else if (ShotState < 3)
		{
			if (ShotState == 0)
			{
				for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
				{
					if (*It != HCChar && It->IsMonsterForm() && It->GetPlayerState() && !HCChar->IsKillLocked())
					{
						ShotTarget = *It;
						ShotState = 1;
						ShotSince = Now;
						ServerDebugCommand(TEXT("GotoPlayer"), { It->GetPlayerState()->GetPlayerName(), TEXT("front") });
						return true;
					}
				}
			}
			else if (const AHCCharacter* Target = ShotTarget.Get())
			{
				SetControlRotation((Target->GetActorLocation() + FVector(0.f, 0.f, 60.f) - HCChar->GetCamera()->GetComponentLocation()).Rotation());
				if (ShotState == 1 && Now - ShotSince > 0.8)
				{
					ShotState = 2;
					UE_LOG(LogHC, Display, TEXT("[HCSmoke] True form seen: %s shows the monster, name tag %s"),
						Target->GetPlayerState() ? *Target->GetPlayerState()->GetPlayerName() : TEXT("?"), *Target->GetAppearance().DisplayName);
					FString Dir;
					if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
					{
						FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("trueform_seen_by_%s.png"), *PS->GetPlayerName())), true, false);
					}
				}
				else if (ShotState == 2 && Now - ShotSince > 1.6)
				{
					ShotState = 3;
				}
				return true;
			}
			else
			{
				ShotState = 3;
			}
		}
	}
	if (!bSmokeAutoShift || !PS || !GS)
	{
		return false;
	}
	// Every bot: what does each figure look like on this machine? Logged whenever it changes.
	TArray<FString> Parts;
	AHCCharacter* Disguised = nullptr;
	for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
	{
		const APlayerState* OtherPS = It->GetPlayerState();
		if (!OtherPS)
		{
			continue;
		}
		Parts.Add(FString::Printf(TEXT("%s=%s"), *OtherPS->GetPlayerName(), *It->GetAppearance().DisplayName));
		if (*It != HCChar && !It->GetAppearance().DisplayName.Equals(OtherPS->GetPlayerName()))
		{
			Disguised = *It;
		}
	}
	Parts.Sort();
	const FString Looks = FString::Join(Parts, TEXT(" "));
	if (Looks != SmokeLastLooks)
	{
		SmokeLastLooks = Looks;
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Looks: %s"), *Looks);
	}

	const bool bMonster = PS->GetRoleInfo().Role == EHCRole::Monster;
	if (bSmokePhaseShots && HCChar && !bMonster && SmokeLookShotState < 3 && FParse::Param(FCommandLine::Get(), TEXT("HCDisguisePhoto")))
	{
		// One picture per crew bot: stand behind a disguised figure and look at the badge on its back
		if (SmokeLookShotState == 0 && Disguised && !HCChar->IsKillLocked())
		{
			SmokeLookTarget = Disguised;
			SmokeLookShotState = 1;
			SmokeLookShotSince = Now;
			SmokeTaskState = 0;
			ServerDebugCommand(TEXT("GotoPlayer"), { Disguised->GetPlayerState()->GetPlayerName(), TEXT("front") });
			return true;
		}
		if (SmokeLookShotState > 0)
		{
			const AHCCharacter* Target = SmokeLookTarget.Get();
			if (!Target || Now - SmokeLookShotSince > 4.0)
			{
				SmokeLookShotState = SmokeLookShotState == 2 ? 3 : 0;
				return false;
			}
			SetControlRotation((Target->GetActorLocation() + FVector(0.f, 0.f, 88.f) - HCChar->GetCamera()->GetComponentLocation()).Rotation());
			if (SmokeLookShotState == 1 && Now - SmokeLookShotSince > 0.6)
			{
				SmokeLookShotState = 2;
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] Photo: %s looks like %s"), Target->GetPlayerState() ? *Target->GetPlayerState()->GetPlayerName() : TEXT("?"), *Target->GetAppearance().DisplayName);
				FString Dir;
				if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
				{
					FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("disguise_seen_by_%s.png"), *PS->GetPlayerName())), true, false);
				}
			}
			else if (SmokeLookShotState == 2 && Now - SmokeLookShotSince > 1.2)
			{
				SmokeLookShotState = 3;
			}
			return true;
		}
	}
	if (!HCChar || !bMonster)
	{
		return false;
	}

	UHCShapeshiftComponent* Shift = HCChar->GetShapeshift();
	AHCHUD* HUD = GetHUD<AHCHUD>();
	if (Shift->GetState() == EHCShapeshiftState::Disguised)
	{
		if (SmokeDisguisedSince <= 0.0)
		{
			SmokeDisguisedSince = Now;
		}
		if (SmokeShiftRevert > 0.f && Now - SmokeDisguisedSince > SmokeShiftRevert && Now - SmokeShiftSince > 1.0)
		{
			SmokeShiftSince = Now;
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Ending the disguise early with Q"));
			HCChar->Input_Shapeshift();
		}
		return false; // disguised: go on with tasks and kills
	}
	SmokeDisguisedSince = 0.0;
	if (Shift->IsChanging())
	{
		return true;
	}
	if (!HUD || HCChar->IsKillLocked() || Shift->GetCooldownRemaining() > 0.f || HUD->IsTaskOverlayOpen())
	{
		return false;
	}
	if (!HUD->IsShapeshiftMenuOpen())
	{
		if (Now - SmokeShiftSince < 2.0)
		{
			return false;
		}
		SmokeShiftSince = Now;
		SmokeTaskState = 0;
		HCChar->Input_Shapeshift(); // the Q key
		return true;
	}
	if (Now - SmokeShiftSince < 0.6)
	{
		return true;
	}
	if (bSmokePhaseShots && !bSmokeShiftMenuShot)
	{
		bSmokeShiftMenuShot = true;
		FString Dir;
		if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, TEXT("shapeshift_menu.png")), true, false);
		}
		SmokeShiftSince = Now - 0.3;
		return true;
	}
	// The living player with the lowest identity
	const AHCPlayerState* Target = nullptr;
	for (const APlayerState* Other : GS->PlayerArray)
	{
		const AHCPlayerState* OtherPS = Cast<AHCPlayerState>(Other);
		if (OtherPS && OtherPS != PS && OtherPS->IsAlive() && (!Target || OtherPS->GetIdentity() < Target->GetIdentity()))
		{
			Target = OtherPS;
		}
	}
	const bool bSent = Target && HUD->PickShapeshiftTarget(Target->GetPlayerId());
	UE_LOG(LogHC, Display, TEXT("[HCSmoke] Shapeshift into %s: %s"), Target ? *Target->GetPlayerName() : TEXT("nobody"), bSent ? TEXT("sent") : TEXT("FAILED"));
	if (!bSent)
	{
		UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR the shapeshift menu did not accept the choice"));
		HUD->CloseShapeshiftMenu();
	}
	SmokeShiftSince = Now;
	return true;
#else
	return false;
#endif
}

bool AHCPlayerController::TickAutoRepair(double Now)
{
#if !UE_BUILD_SHIPPING
	ParseMeetingSmokeFlags();
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	if (!HCChar || !PS)
	{
		return false;
	}
	const AHCPowerSector* Dark = nullptr;
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		if (!It->IsPowered())
		{
			Dark = *It;
		}
	}
	if (Dark && SmokeOutageSince <= 0.0)
	{
		SmokeOutageSince = Now;
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Outage seen in sector %d"), Dark->GetSectorIndex());
	}
	else if (!Dark && SmokeOutageSince > 0.0)
	{
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Power back after %.0f s"), Now - SmokeOutageSince);
		SmokeOutageSince = 0.0;
	}
	if (Dark && bSmokePhaseShots && !bSmokeOutageShot && Now - SmokeOutageSince > 2.2)
	{
		// One picture from inside the dark sector
		for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
		{
			FString Dir;
			if (It->GetSectorIndexAt(HCChar->GetActorLocation()) == Dark->GetSectorIndex() && FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
			{
				bSmokeOutageShot = true;
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("outage_%s.png"), *PS->GetPlayerName())), true, false);
			}
		}
	}
	if (!bSmokeAutoRepair || !Dark || HCChar->IsKillLocked())
	{
		SmokeRepairState = 0;
		return false;
	}
	AHCFuseBox* Box = nullptr;
	for (TActorIterator<AHCFuseBox> It(GetWorld()); It; ++It)
	{
		if (It->GetSectorIndex() == Dark->GetSectorIndex())
		{
			Box = *It;
		}
	}
	UHCInteractionComponent* Interaction = HCChar->GetInteraction();
	if (!Box || (SmokeRepairState > 0 && Now - SmokeRepairSince > 9.0))
	{
		UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR fuse box could not be used (%s)"), Box ? *Interaction->GetPromptText().ToString() : TEXT("no fuse box on this machine"));
		SmokeRepairState = 0;
		return false;
	}
	if (SmokeRepairState == 0)
	{
		SmokeRepairState = 1;
		SmokeRepairSince = Now;
		SmokeTaskState = 0;
		ServerDebugCommand(TEXT("GotoFuseBox"), { FString::FromInt(Dark->GetSectorIndex()) });
		return true;
	}
	SetControlRotation((Box->GetActorLocation() - HCChar->GetCamera()->GetComponentLocation()).Rotation());
	if (SmokeRepairState == 1 && Now - SmokeRepairSince > 0.8 && Interaction->GetFocusedActor() == Box && Interaction->IsFocusAvailable())
	{
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Prompt at fuse box: %s"), *Interaction->GetPromptText().ToString());
		Interaction->BeginInteract();
		SmokeRepairState = 2;
	}
	return true;
#else
	return false;
#endif
}

bool AHCPlayerController::TickAutoSabotage(double Now)
{
#if !UE_BUILD_SHIPPING
	ParseMeetingSmokeFlags();
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	AHCHUD* HUD = GetHUD<AHCHUD>();
	if (!bSmokeAutoSabotage || !HCChar || !PS || !GS || !HUD || PS->GetRoleInfo().Role != EHCRole::Monster || HCChar->IsKillLocked()
		|| HUD->IsTaskOverlayOpen() || HUD->IsShapeshiftMenuOpen() || HCChar->GetShapeshift()->IsChanging())
	{
		return false;
	}
	// Power and door in turn; the sector changes every time
	const double ServerNow = GS->GetServerWorldTimeSeconds();
	const bool bDoorTurn = (SmokeSabotageCount % 2) == 1;
	FName Target;
	bool bAvailable = ServerNow >= PS->GetSabotageEarliestTime();
	if (bDoorTurn)
	{
		bAvailable &= ServerNow >= PS->GetDoorSabotageReadyTime();
		for (TActorIterator<AHCDoor> It(GetWorld()); It; ++It)
		{
			bAvailable &= It->GetDoorState() != EHCDoorState::Locked;
			if (It->bSabotageable && (Target.IsNone() || It->DoorId.LexicalLess(Target)))
			{
				Target = It->DoorId;
			}
		}
	}
	else
	{
		bAvailable &= ServerNow >= PS->GetPowerSabotageReadyTime();
		for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
		{
			bAvailable &= It->IsPowered();
		}
		Target = AHCSabotageManager::SectorTargetId(static_cast<uint8>(1 + (SmokeSabotageCount / 2) % 3));
	}
	if (!bAvailable || Target.IsNone())
	{
		if (HUD->IsStationMapOpen() && Now - SmokeSabotageSince > 1.5)
		{
			HUD->CloseStationMap();
		}
		return false;
	}
	if (!HUD->IsStationMapOpen())
	{
		if (Now - SmokeSabotageSince < 3.0)
		{
			return false;
		}
		SmokeSabotageSince = Now;
		SmokeTaskState = 0;
		HCChar->Input_Map(); // the M key
		return true;
	}
	if (Now - SmokeSabotageSince < 0.7)
	{
		return true;
	}
	if (bSmokePhaseShots && !bSmokeMapShot)
	{
		bSmokeMapShot = true;
		FString Dir;
		if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, TEXT("station_map.png")), true, false);
		}
		SmokeSabotageSince = Now - 0.3;
		return true;
	}
	const bool bSent = HUD->StationMapSabotage(static_cast<uint8>(bDoorTurn ? EHCSabotageType::DoorLock : EHCSabotageType::PowerOutage), Target);
	UE_LOG(LogHC, Display, TEXT("[HCSmoke] Sabotage %s %s: %s"), bDoorTurn ? TEXT("door") : TEXT("power"), *Target.ToString(), bSent ? TEXT("sent") : TEXT("FAILED"));
	if (!bSent)
	{
		UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR the station map did not accept the sabotage"));
		HUD->CloseStationMap();
	}
	++SmokeSabotageCount;
	SmokeSabotageSince = Now;
	return true;
#else
	return false;
#endif
}
