// Test bots of the jobs update (development builds only): a crisis started by the Wandler and held off by the crew,
// the job abilities, the emote wheel, the wardrobe in the lobby, and pictures of all of it (-HCPhaseShots).
#include "Core/HCPlayerController.h"

#include "Abilities/HCRemains.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCOutfit.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerState.h"
#include "EngineUtils.h"
#include "Interaction/HCInteractionComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Roles/HCJobs.h"
#include "Sabotage/HCCrisisPanel.h"
#include "Sabotage/HCSabotageManager.h"
#include "UI/HCEmoteWheel.h"
#include "UI/HCHUD.h"
#include "UI/HCRadioWidget.h"
#include "UI/HCWardrobeWidget.h"
#include "UnrealClient.h"
#include "World/HCWardrobe.h"

namespace HCJobsSmoke
{
	/** A picture into -HCShotDir, named after the player. */
	void Shot(const APlayerController* PC, const TCHAR* What, bool bWithUI = true)
	{
		FString Dir;
		const APlayerState* PS = PC ? PC->PlayerState.Get() : nullptr;
		if (PS && FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), What, *PS->GetPlayerName())), bWithUI, false);
		}
	}
}

bool AHCPlayerController::TickAutoJobs(double Now, double InPhase)
{
#if !UE_BUILD_SHIPPING
	const AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	// The own view with a container in both hands, once
	if (HCChar && HCChar->IsCarrying() && !bSmokeCarryShot && bSmokePhaseShots)
	{
		bSmokeCarryShot = true;
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Carrying a container, flashlight %s"), HCChar->IsFlashlightOn() ? TEXT("ON (wrong)") : TEXT("off"));
		HCJobsSmoke::Shot(this, TEXT("carry_view"));
	}
	// The job indicator of the HUD, once, a while into the round
	if (HCChar && !bSmokeJobHudShot && bSmokePhaseShots && InPhase > 9.0)
	{
		bSmokeJobHudShot = true;
		const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Own job: %s%s"), PS ? HCJobs::Id(PS->GetRoleInfo().Job) : TEXT("?"), PS && PS->IsCommander() ? TEXT(" (commander)") : TEXT(""));
		HCJobsSmoke::Shot(this, TEXT("job_hud"));
	}
	TickAutoEmote(Now, InPhase);
	return TickAutoCrisis(Now) || TickAutoAbility(Now, InPhase);
#else
	return false;
#endif
}

bool AHCPlayerController::TickAutoCrisis(double Now)
{
#if !UE_BUILD_SHIPPING
	static const bool bWanted = FParse::Param(FCommandLine::Get(), TEXT("HCAutoCrisis"));
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	AHCHUD* HUD = GetHUD<AHCHUD>();
	if (!GS)
	{
		return false;
	}
	// Seen by everybody: when it started and when it was over
	const bool bRunning = GS->GetCrisis() != EHCCrisis::None;
	if (bRunning && SmokeCrisisSeen <= 0.0)
	{
		SmokeCrisisSeen = Now;
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Crisis %s seen, %.0f s left"), *AHCSabotageManager::CrisisTargetId(GS->GetCrisis()).ToString(), GS->GetCrisisRemaining());
	}
	else if (!bRunning && SmokeCrisisSeen > 0.0)
	{
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Crisis over after %.1f s"), Now - SmokeCrisisSeen);
		SmokeCrisisSeen = 0.0;
		SmokeCrisisState = 0;
		if (HCChar)
		{
			HCChar->GetInteraction()->EndInteract();
		}
	}
	if (bRunning && bSmokePhaseShots && (SmokeCrisisShots & 2) == 0 && Now - SmokeCrisisSeen > 2.5)
	{
		SmokeCrisisShots |= 2;
		HCJobsSmoke::Shot(this, TEXT("crisis_banner"));
	}
	if (!bWanted || !HCChar || !PS || !HUD || !PS->IsAlive() || HCChar->IsKillLocked())
	{
		return false;
	}

	if (PS->GetRoleInfo().Role == EHCRole::Monster)
	{
		// The Wandler starts one crisis per round from the station map (-HCCrisis=Oxygen for the other one)
		if (bSmokeCrisisSent || bRunning || HUD->IsTaskOverlayOpen() || HUD->IsShapeshiftMenuOpen())
		{
			return false;
		}
		const double ServerNow = GS->GetServerWorldTimeSeconds();
		if (ServerNow < PS->GetSabotageEarliestTime() || ServerNow < PS->GetPowerSabotageReadyTime())
		{
			return false;
		}
		if (!HUD->IsStationMapOpen())
		{
			if (Now - SmokeCrisisSince < 3.0)
			{
				return false;
			}
			SmokeCrisisSince = Now;
			SmokeTaskState = 0;
			HCChar->Input_Map();
			return true;
		}
		if (Now - SmokeCrisisSince < 1.0)
		{
			return true;
		}
		if (bSmokePhaseShots && (SmokeCrisisShots & 1) == 0)
		{
			SmokeCrisisShots |= 1;
			HCJobsSmoke::Shot(this, TEXT("crisis_map"));
			SmokeCrisisSince = Now - 0.4;
			return true;
		}
		FString Which = TEXT("Meltdown");
		FParse::Value(FCommandLine::Get(), TEXT("HCCrisis="), Which);
		EHCCrisis Kind = EHCCrisis::Meltdown;
		AHCSabotageManager::ParseCrisis(Which, Kind);
		const bool bSent = HUD->StationMapSabotage(static_cast<uint8>(EHCSabotageType::Crisis), AHCSabotageManager::CrisisTargetId(Kind));
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Crisis %s: %s"), *AHCSabotageManager::CrisisTargetId(Kind).ToString(), bSent ? TEXT("sent") : TEXT("FAILED"));
		if (!bSent)
		{
			UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR the station map did not accept the crisis"));
			HUD->CloseStationMap();
		}
		bSmokeCrisisSent = true;
		return true;
	}

	// The crew: one panel each (by the order of the names), held until the crisis is over
	if (!bRunning)
	{
		return false;
	}
	TArray<FString> Names;
	for (const APlayerState* Other : GS->PlayerArray)
	{
		const AHCPlayerState* HCOther = Cast<AHCPlayerState>(Other);
		if (HCOther && HCOther->IsAlive())
		{
			Names.Add(Other->GetPlayerName());
		}
	}
	Names.Sort();
	const int32 Index = FMath::Max(0, Names.IndexOfByKey(PS->GetPlayerName())) % 2;
	const AHCCrisisPanel* Panel = nullptr;
	for (TActorIterator<AHCCrisisPanel> It(GetWorld()); It; ++It)
	{
		if (It->GetCrisis() == GS->GetCrisis() && It->GetPanelIndex() == Index)
		{
			Panel = *It;
		}
	}
	UHCInteractionComponent* Interaction = HCChar->GetInteraction();
	if (!Panel)
	{
		UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR no crisis panel %d on this machine"), Index);
		return false;
	}
	if (SmokeCrisisState == 0)
	{
		SmokeCrisisState = 1;
		SmokeCrisisSince = Now;
		SmokeTaskState = 0;
		HUD->CloseModal(nullptr);
		ServerDebugCommand(TEXT("GotoCrisisPanel"), { FString::FromInt(Index) });
		return true;
	}
	SetControlRotation((Panel->GetActorLocation() - HCChar->GetCamera()->GetComponentLocation()).Rotation());
	if (SmokeCrisisState == 1 && Now - SmokeCrisisSince > 0.8 && Interaction->GetFocusedActor() == Panel && Interaction->IsFocusAvailable())
	{
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] At crisis panel %d: %s"), Index, *Interaction->GetPromptText().ToString());
		Interaction->BeginInteract();
		SmokeCrisisState = 2;
		SmokeCrisisSince = Now;
	}
	else if (SmokeCrisisState == 2)
	{
		if (bSmokePhaseShots && (SmokeCrisisShots & 4) == 0 && Now - SmokeCrisisSince > 1.2)
		{
			SmokeCrisisShots |= 4;
			HCJobsSmoke::Shot(this, TEXT("crisis_hold"));
		}
		if (Interaction->GetHoldProgress() < 0.f && Now - SmokeCrisisSince > 1.0)
		{
			// The hold was broken off: start again
			SmokeCrisisState = 1;
			SmokeCrisisSince = Now;
		}
	}
	else if (SmokeCrisisState == 1 && Now - SmokeCrisisSince > 8.0)
	{
		UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR crisis panel %d could not be used (%s)"), Index, *Interaction->GetPromptText().ToString());
		SmokeCrisisState = 0;
	}
	return true;
#else
	return false;
#endif
}

bool AHCPlayerController::TickAutoAbility(double Now, double InPhase)
{
#if !UE_BUILD_SHIPPING
	static const bool bWanted = FParse::Param(FCommandLine::Get(), TEXT("HCAutoAbility"));
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	AHCHUD* HUD = GetHUD<AHCHUD>();
	if (!bWanted || !HCChar || !PS || !HUD || !PS->IsAlive() || HCChar->IsKillLocked() || SmokeAbilityState >= 9
		|| PS->GetRoleInfo().Role != EHCRole::Crew || HUD->IsTaskOverlayOpen())
	{
		return false;
	}
	const EHCJob Job = PS->GetRoleInfo().Job;
	switch (Job)
	{
	case EHCJob::Medic:
	{
		// Wait for remains, go there, examine them; the result comes as a notice (and in the log: [HCNotice])
		const bool bRemains = TActorIterator<AHCRemains>(GetWorld()) ? true : false;
		if (SmokeAbilityState == 0 && bRemains)
		{
			SmokeAbilityState = 1;
			SmokeAbilitySince = Now;
			ServerDebugCommand(TEXT("GotoRemains"), {});
			return true;
		}
		if (SmokeAbilityState == 1 && Now - SmokeAbilitySince > 1.0)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Medic examines the remains"));
			HCChar->Input_Ability();
			SmokeAbilityState = 2;
			SmokeAbilitySince = Now;
		}
		if (SmokeAbilityState == 2 && Now - SmokeAbilitySince > 4.2)
		{
			HCJobsSmoke::Shot(this, TEXT("medic_result"));
			SmokeAbilityState = 9;
		}
		return SmokeAbilityState >= 1 && SmokeAbilityState < 9;
	}
	case EHCJob::Guard:
		if (SmokeAbilityState == 0 && InPhase > 18.0)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Guard opens the tablet"));
			HCChar->Input_Ability();
			SmokeAbilityState = 1;
			SmokeAbilitySince = Now;
		}
		if (SmokeAbilityState == 1 && Now - SmokeAbilitySince > 2.5)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Guard tablet open: %s, %.0f s left"), HUD->IsDeviceViewOpen() ? TEXT("yes") : TEXT("NO"), PS->GetJobState().TabletSeconds);
			HCJobsSmoke::Shot(this, TEXT("guard_tablet"));
			SmokeAbilityState = 2;
		}
		if (SmokeAbilityState == 2 && Now - SmokeAbilitySince > 18.0)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Guard tablet after its time: %s, %.0f s left"), HUD->IsDeviceViewOpen() ? TEXT("STILL OPEN") : TEXT("closed"), PS->GetJobState().TabletSeconds);
			SmokeAbilityState = 9;
		}
		return SmokeAbilityState == 1 || (SmokeAbilityState == 2 && HUD->IsDeviceViewOpen());
	case EHCJob::Biologist:
		if (SmokeAbilityState == 0 && InPhase > 14.0)
		{
			HCChar->Input_Ability();
			SmokeAbilityState = 1;
			SmokeAbilitySince = Now;
		}
		if (SmokeAbilityState == 1 && Now - SmokeAbilitySince > 1.5)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Biologist UV light: %s"), HCChar->IsUVLight() ? TEXT("on") : TEXT("OFF"));
			HCJobsSmoke::Shot(this, TEXT("biologist_uv"));
			SmokeAbilityState = 2;
		}
		if (SmokeAbilityState == 2 && Now - SmokeAbilitySince > 8.0)
		{
			HCChar->Input_Ability();
			SmokeAbilityState = 9;
		}
		return false;
	case EHCJob::Radio:
		if (SmokeAbilityState == 0 && InPhase > 22.0 && !HUD->GetModal())
		{
			HCChar->Input_Ability();
			SmokeAbilityState = 1;
			SmokeAbilitySince = Now;
		}
		if (SmokeAbilityState == 1 && Now - SmokeAbilitySince > 1.0)
		{
			HCJobsSmoke::Shot(this, TEXT("radio"));
			SmokeAbilityState = 2;
			SmokeAbilitySince = Now;
		}
		if (SmokeAbilityState == 2 && Now - SmokeAbilitySince > 0.6)
		{
			UHCRadioWidget* Radio = Cast<UHCRadioWidget>(HUD->GetModal());
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Radio set open: %s"), Radio ? TEXT("yes") : TEXT("NO"));
			if (Radio)
			{
				Radio->SendAnnouncement(TEXT("Bin im Lager, hier ist alles ruhig."));
			}
			SmokeAbilityState = 3;
			SmokeAbilitySince = Now;
		}
		if (SmokeAbilityState == 3 && Now - SmokeAbilitySince > 1.2)
		{
			HCJobsSmoke::Shot(this, TEXT("radio_heard"));
			SmokeAbilityState = 9;
		}
		return SmokeAbilityState >= 1 && SmokeAbilityState < 3;
	default:
		return false;
	}
#else
	return false;
#endif
}

void AHCPlayerController::TickAutoEmote(double Now, double InPhase)
{
#if !UE_BUILD_SHIPPING
	static const bool bWanted = FParse::Param(FCommandLine::Get(), TEXT("HCAutoEmote"));
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	AHCHUD* HUD = GetHUD<AHCHUD>();
	// Not while the ability bot uses its window (the radio set would find the wheel in its place)
	if (!bWanted || !HCChar || !HUD || !HCChar->IsAlive() || InPhase < 6.0 || SmokeEmoteCount >= static_cast<int32>(EHCEmote::Count)
		|| (SmokeAbilityState > 0 && SmokeAbilityState < 9))
	{
		return;
	}
	if (SmokeEmoteState == 0 && Now >= SmokeEmoteNext && !HUD->GetModal() && !HUD->IsTaskOverlayOpen())
	{
		HCChar->Input_Emote();
		SmokeEmoteState = HUD->GetModal() ? 1 : 0;
		SmokeEmoteNext = Now + (SmokeEmoteState ? 0.7 : 2.0);
	}
	else if (SmokeEmoteState == 1 && Now >= SmokeEmoteNext)
	{
		// The first time: a picture of the open wheel, the choice a moment later (the picture is taken at the end of
		// the frame - after a choice in the same frame the wheel would already be gone)
		if (SmokeEmoteCount == 0 && bSmokePhaseShots)
		{
			HCJobsSmoke::Shot(this, TEXT("emote_wheel"));
		}
		SmokeEmoteState = 2;
		SmokeEmoteNext = Now + 0.4;
	}
	else if (SmokeEmoteState == 2 && Now >= SmokeEmoteNext)
	{
		UHCEmoteWheelWidget* Wheel = Cast<UHCEmoteWheelWidget>(HUD->GetModal());
		if (Wheel)
		{
			Wheel->ChooseEmote(static_cast<EHCEmote>(SmokeEmoteCount));
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Emote %d chosen"), SmokeEmoteCount);
		}
		++SmokeEmoteCount;
		SmokeEmoteState = 0;
		SmokeEmoteNext = Now + 5.0;
	}
#endif
}

void AHCPlayerController::TickAutoWardrobe(double Now)
{
#if !UE_BUILD_SHIPPING
	static const bool bWanted = FParse::Param(FCommandLine::Get(), TEXT("HCAutoWardrobe"));
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	AHCHUD* HUD = GetHUD<AHCHUD>();
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	if (!bWanted || !HCChar || !HUD || !PS || SmokeWardrobeState >= 9)
	{
		return;
	}
	UHCInteractionComponent* Interaction = HCChar->GetInteraction();
	switch (SmokeWardrobeState)
	{
	case 0:
		// Close the lobby panel, walk to the wardrobe
		SmokeWardrobeState = 1;
		SmokeWardrobeSince = Now;
		if (UUserWidget* Modal = HUD->GetModal())
		{
			HUD->CloseModal(Modal);
		}
		HUD->ToggleLobbyMenu();
		ServerDebugCommand(TEXT("GotoWardrobe"), {});
		break;
	case 1:
		if (Now - SmokeWardrobeSince > 1.2)
		{
			if (TActorIterator<AHCWardrobe> It(GetWorld()); It)
			{
				SetControlRotation((It->GetActorLocation() + FVector(0.0, 0.0, 120.0) - HCChar->GetCamera()->GetComponentLocation()).Rotation());
			}
			if (Interaction->GetFocusedActor() && Interaction->GetFocusedActor()->IsA<AHCWardrobe>() && Interaction->IsFocusAvailable())
			{
				HCJobsSmoke::Shot(this, TEXT("wardrobe_prompt"));
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] At the wardrobe: %s"), *Interaction->GetPromptText().ToString());
				SmokeWardrobeState = 2;
				SmokeWardrobeSince = Now;
			}
			else if (Now - SmokeWardrobeSince > 8.0)
			{
				UE_LOG(LogHC, Error, TEXT("[HCSmoke] ERROR the wardrobe is not in focus (%s)"), *Interaction->GetPromptText().ToString());
				SmokeWardrobeState = 9;
			}
		}
		break;
	case 2:
		if (Now - SmokeWardrobeSince > 0.8)
		{
			Interaction->BeginInteract();
			Interaction->EndInteract();
			SmokeWardrobeState = 3;
			SmokeWardrobeSince = Now;
		}
		break;
	case 3:
		if (Now - SmokeWardrobeSince > 1.5)
		{
			UHCWardrobeWidget* Wardrobe = Cast<UHCWardrobeWidget>(HUD->GetModal());
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Wardrobe open: %s (colour %d, outfit %d)"), Wardrobe ? TEXT("yes") : TEXT("NO"), PS->GetIdentity(), PS->GetOutfit());
			HCJobsSmoke::Shot(this, TEXT("wardrobe_open"));
			if (!Wardrobe)
			{
				SmokeWardrobeState = 9;
				break;
			}
			// A free colour from the end of the list, and something of everything
			for (int32 Colour = HCIdentity::Count - 1; Colour >= 0; --Colour)
			{
				bool bTaken = false;
				for (const APlayerState* Other : GetWorld()->GetGameState()->PlayerArray)
				{
					const AHCPlayerState* HCOther = Cast<AHCPlayerState>(Other);
					bTaken |= HCOther && HCOther != PS && HCOther->GetIdentity() == Colour;
				}
				if (!bTaken && Colour != PS->GetIdentity())
				{
					Wardrobe->Pick(-1, Colour);
					break;
				}
			}
			Wardrobe->Pick(static_cast<int32>(HCOutfit::EPart::Trousers), 2);
			Wardrobe->Pick(static_cast<int32>(HCOutfit::EPart::Hair), 2);
			Wardrobe->Pick(static_cast<int32>(HCOutfit::EPart::Boots), 1);
			Wardrobe->Pick(static_cast<int32>(HCOutfit::EPart::Backpack), 1);
			SmokeWardrobeState = 4;
			SmokeWardrobeSince = Now;
		}
		break;
	case 4:
		if (Now - SmokeWardrobeSince > 2.0)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Wardrobe changed: colour %d, outfit %d"), PS->GetIdentity(), PS->GetOutfit());
			HCJobsSmoke::Shot(this, TEXT("wardrobe_changed"));
			SmokeWardrobeState = 5;
			SmokeWardrobeSince = Now;
		}
		break;
	case 5:
		if (Now - SmokeWardrobeSince > 0.8)
		{
			if (UHCWardrobeWidget* Wardrobe = Cast<UHCWardrobeWidget>(HUD->GetModal()))
			{
				Wardrobe->Close();
			}
			SmokeWardrobeState = 6;
			SmokeWardrobeSince = Now;
		}
		break;
	case 6:
		if (Now - SmokeWardrobeSince > 1.0)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Wardrobe closed, view back on the own figure: %s"), GetViewTarget() == HCChar ? TEXT("yes") : TEXT("NO"));
			HCJobsSmoke::Shot(this, TEXT("wardrobe_after"));
			SmokeWardrobeState = 9;
		}
		break;
	default:
		break;
	}
#endif
}
