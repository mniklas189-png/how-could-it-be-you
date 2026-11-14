#include "Core/HCPlayerController.h"

#include "Characters/HCCharacter.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerState.h"
#include "Core/HCInputConfig.h"
#include "Core/HCKeyBindings.h"
#include "Online/HCSessionSubsystem.h"
#include "Core/HCLog.h"
#include "AudioDevice.h"
#include "Abilities/HCJumpscareDirector.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Interaction/HCInteractionComponent.h"
#include "Meeting/HCMeetingManager.h"
#include "Sabotage/HCSabotageManager.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "UI/HCHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Engine/LocalPlayer.h"
#include "HAL/FileManager.h"
#include "InputMappingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "World/HCMonitorDevices.h"

AHCPlayerController::AHCPlayerController()
{
}

UHCInputConfig* AHCPlayerController::GetInputConfig()
{
	if (!InputConfig)
	{
		InputConfig = UHCInputConfig::Create(this);
	}
	return InputConfig;
}

void AHCPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		ApplyMappingContext();
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
		StartSmokeTestIfRequested();
		if (UHCGameInstance* Instance = GetGameInstance<UHCGameInstance>())
		{
			Instance->ApplyMasterVolume();
		}
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("HCMute")))
		{
			if (FAudioDevice* AudioDevice = GetWorld()->GetAudioDeviceRaw())
			{
				AudioDevice->SetTransientPrimaryVolume(0.f);
			}
		}
#endif
	}
}

void AHCPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent))
	{
		Input->BindAction(GetInputConfig()->Pause, ETriggerEvent::Started, this, &AHCPlayerController::Input_Pause);
		Input->BindAction(GetInputConfig()->LobbyMenu, ETriggerEvent::Started, this, &AHCPlayerController::Input_LobbyMenu);
	}
}

void AHCPlayerController::Input_Pause()
{
	if (AHCHUD* HUD = GetHUD<AHCHUD>())
	{
		HUD->TogglePauseMenu();
	}
}

void AHCPlayerController::Input_LobbyMenu()
{
	if (AHCHUD* HUD = GetHUD<AHCHUD>())
	{
		HUD->ToggleLobbyMenu();
	}
}

bool AHCPlayerController::IsHost() const
{
	return IsLocalController() && GetNetMode() != NM_Client;
}

void AHCPlayerController::ServerRequestStartMatch_Implementation()
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		FText Error;
		if (!GM->RequestStartMatch(this, Error))
		{
			UE_LOG(LogHCNet, Log, TEXT("Start rejected for %s: %s"), *GetNameSafe(PlayerState.Get()), *Error.ToString());
			ClientShowMessage(Error);
		}
	}
}

void AHCPlayerController::ServerRequestReturnToLobby_Implementation()
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		GM->RequestReturnToLobby(this);
	}
}

void AHCPlayerController::ServerVoteSkipIntro_Implementation()
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		GM->VoteSkipIntro(this);
	}
}

void AHCPlayerController::ServerDebugCommand_Implementation(const FString& Command, const TArray<FString>& Args)
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		GM->ExecuteDebugCommand(Command, Args, this);
	}
}

void AHCPlayerController::ClientOpenTaskOverlay_Implementation(AHCTaskStation* Station)
{
	if (AHCHUD* HUD = GetHUD<AHCHUD>())
	{
		HUD->OpenTaskOverlay(Station);
	}
}

void AHCPlayerController::ServerCompleteTaskStep_Implementation(AHCTaskStation* Station)
{
	AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	FString Reason;
	if (!PS || !Station || !PS->GetTaskComponent()->ServerCompleteTimedStep(Station, GetPawn(), Reason))
	{
		// Not punished: with ping an honest request can be just invalid (docs/02 §2.6)
		UE_LOG(LogHCNet, Log, TEXT("Task step of %s rejected: %s"), PS ? *PS->GetPlayerName() : TEXT("?"), *Reason);
		ClientShowMessage(NSLOCTEXT("HC", "TaskRejected", "Aufgabe nicht anerkannt. Bitte noch einmal."));
	}
}

void AHCPlayerController::ServerCancelTaskStep_Implementation()
{
	if (AHCPlayerState* PS = GetPlayerState<AHCPlayerState>())
	{
		PS->GetTaskComponent()->ServerCancelTimedStep();
	}
}

void AHCPlayerController::ClientOpenDeviceView_Implementation(AHCMonitorDevice* Device)
{
	if (AHCHUD* HUD = GetHUD<AHCHUD>())
	{
		HUD->OpenDeviceView(Device);
	}
}

void AHCPlayerController::ClientCloseDeviceView_Implementation()
{
	if (AHCHUD* HUD = GetHUD<AHCHUD>())
	{
		HUD->CloseDeviceView(false);
	}
}

void AHCPlayerController::ServerCloseDeviceView_Implementation(AHCMonitorDevice* Device)
{
	if (Device)
	{
		Device->ServerRemoveViewer(Cast<AHCCharacter>(GetPawn()), false);
	}
}

void AHCPlayerController::ClientStartJumpscare_Implementation(FVector_NetQuantize AttackerLocation)
{
	// Menus and task windows of the victim close (docs/02 §7.2)
	if (AHCHUD* HUD = GetHUD<AHCHUD>())
	{
		HUD->CloseMenusForJumpscare();
	}
	AHCJumpscareDirector::Start(this, AttackerLocation, false);
}

void AHCPlayerController::StartJumpscarePreview(int32 Style)
{
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	AHCJumpscareDirector::Start(this, ViewLocation + ViewRotation.Vector() * 200.f, true, Style);
}

void AHCPlayerController::ServerJumpscareFinished_Implementation()
{
	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		GM->NotifyJumpscareFinished(this);
	}
}

void AHCPlayerController::ClientRevealMonstersToGhost_Implementation(const TArray<int32>& MonsterPlayerIds)
{
	// Only used to mark the Wandler in the own ghost view; never sent to the living (docs/02 §2.5)
	RevealedMonsterIds = MonsterPlayerIds;
}

bool AHCPlayerController::IsMonsterRevealed(int32 PlayerId) const
{
	return RevealedMonsterIds.Contains(PlayerId);
}

void AHCPlayerController::ClientFaceLocation_Implementation(FVector_NetQuantize Location)
{
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	FRotator Wanted = (FVector(Location) - ViewLocation).Rotation();
	Wanted.Roll = 0.f;
	SetControlRotation(Wanted);
}

void AHCPlayerController::ServerCastVote_Implementation(int32 TargetPlayerId)
{
	AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	FText Error;
	if (GM && PS && !GM->GetMeeting()->CastVote(PS, TargetPlayerId, Error))
	{
		ClientShowMessage(Error);
	}
}

void AHCPlayerController::ServerSendChat_Implementation(const FString& Text)
{
	AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	// Anything far longer than a line is not from the chat box; the meeting manager cuts the rest
	if (GM && PS && Text.Len() <= HCChat::MaxLength * 8)
	{
		GM->GetMeeting()->PostChat(PS, Text);
	}
}

void AHCPlayerController::ClientReceiveChat_Implementation(const FHCChatLine& Line)
{
	// The first line of a new round starts an empty chat
	if (!ChatLines.IsEmpty() && ChatLines.Last().MatchSerial != Line.MatchSerial)
	{
		ChatLines.Reset();
		++ChatEpoch;
	}
	FHCChatLine& Added = ChatLines.Add_GetRef(Line);
	Added.LocalSerial = ++ChatLineSerial;
	if (ChatLines.Num() > HCChat::MaxLines)
	{
		ChatLines.RemoveAt(0, ChatLines.Num() - HCChat::MaxLines);
	}
#if !UE_BUILD_SHIPPING
	const FString Shown = Line.IsMeetingStart() ? FString::Printf(TEXT("--- meeting %d ---"), static_cast<int32>(Line.MeetingNumber))
		: FString::Printf(TEXT("%s%s: %s"), *Line.SenderName, Line.bFromGhost ? TEXT(" (ghost)") : TEXT(""), *Line.Text);
	UE_LOG(LogHC, Display, TEXT("[HCSmoke] Chat in: %s"), *Shown);
#endif
}

void AHCPlayerController::ServerRequestSabotage_Implementation(uint8 SabotageType, FName TargetId)
{
	AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>();
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	if (!GM || !PS || SabotageType > static_cast<uint8>(EHCSabotageType::DoorLock))
	{
		return;
	}
	switch (GM->GetSabotage()->RequestSabotage(PS, static_cast<EHCSabotageType>(SabotageType), TargetId, this))
	{
	case EHCSabotageReject::TooEarly:
		ClientShowMessage(NSLOCTEXT("HC", "SabotageEarly", "Sabotage ist noch nicht möglich."));
		break;
	case EHCSabotageReject::OnCooldown:
		ClientShowMessage(NSLOCTEXT("HC", "SabotageCooldown", "Diese Sabotage ist noch nicht bereit."));
		break;
	case EHCSabotageReject::AlreadyActive:
		ClientShowMessage(NSLOCTEXT("HC", "SabotageActive", "Es läuft schon eine Sabotage dieser Art."));
		break;
	case EHCSabotageReject::TargetInvalid:
	case EHCSabotageReject::WouldCutOff:
		ClientShowMessage(NSLOCTEXT("HC", "SabotageTarget", "Dieses Ziel kann nicht sabotiert werden."));
		break;
	default:
		break; // crew and the dead get no hint at all
	}
}

void AHCPlayerController::ClientShowMessage_Implementation(const FText& Message)
{
	ClientMessage = Message;
	ClientMessageUntil = FPlatformTime::Seconds() + 4.0;
}

FText AHCPlayerController::GetClientMessage() const
{
	return FPlatformTime::Seconds() < ClientMessageUntil ? ClientMessage : FText::GetEmpty();
}

void AHCPlayerController::TickRoundSmokeTest()
{
#if !UE_BUILD_SHIPPING
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	if (!GS)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const EHCMatchPhase Phase = GS->GetMatchPhase();
	if (static_cast<uint8>(Phase) != SmokeLastPhase)
	{
		SmokeLastPhase = static_cast<uint8>(Phase);
		SmokePhaseSince = Now;
		bSmokePhaseShotTaken = false;
		SmokeStoryShot = 0;
		bSmokeSkipVoted = false;
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Phase %s on %s"), HCIdentity::PhaseName(Phase), *GetWorld()->GetMapName());
		if (Phase == EHCMatchPhase::GameOver)
		{
			if (UHCGameInstance* SmokeGI = Cast<UHCGameInstance>(GetGameInstance()))
			{
				SmokeGI->Smoke.bSawGameOver = true;
			}
		}
	}
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	if (!GI)
	{
		return;
	}
	UHCGameInstance::FSmokeState& Smoke = GI->Smoke;
	const double InPhase = Now - SmokePhaseSince;
	const bool bClient = GetNetMode() == NM_Client;

	if (bSmokePhaseShots && !bSmokePhaseShotTaken && InPhase > 1.5)
	{
		bSmokePhaseShotTaken = true;
		FString Dir;
		if (!FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
		{
			Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HCSmoke"));
		}
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%02d_%s.png"), bClient ? TEXT("client") : TEXT("host"), Smoke.ShotIndex++, HCIdentity::PhaseName(Phase)));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		if (const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>())
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Shot %s: role %s serial %d/%d"), *File,
				PS->GetRoleInfo().Role == EHCRole::Monster ? TEXT("Monster") : TEXT("Crew"), PS->GetRoleInfo().MatchSerial, GS->GetMatchSerial());
		}
	}

	// -HCStoryShots: pictures of the story and the role card at fixed moments; -HCAutoSkipIntro: vote to skip the story
	if (Phase == EHCMatchPhase::Intro || Phase == EHCMatchPhase::RoleReveal)
	{
		static const TArray<double> IntroTimes = { 1.0, 4.5, 7.2, 10.5, 14.0, 19.0, 27.0, 31.75, 33.5 };
		static const TArray<double> RevealTimes = { 0.35, 0.75, 1.4, 2.1, 3.5, 6.4 };
		const TArray<double>& Times = Phase == EHCMatchPhase::Intro ? IntroTimes : RevealTimes;
		if (SmokeStoryShot < Times.Num() && InPhase > Times[SmokeStoryShot] && FParse::Param(FCommandLine::Get(), TEXT("HCStoryShots")))
		{
			FString Dir;
			if (!FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
			{
				Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HCSmoke"));
			}
			IFileManager::Get().MakeDirectory(*Dir, true);
			const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("%s_story_%s_%02d.png"), bClient ? TEXT("client") : TEXT("host"), HCIdentity::PhaseName(Phase), SmokeStoryShot));
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Story shot %s at %.2f s"), *File, InPhase);
			++SmokeStoryShot;
		}
		if (Phase == EHCMatchPhase::Intro && !bSmokeSkipVoted && InPhase > 2.0 && FParse::Param(FCommandLine::Get(), TEXT("HCAutoSkipIntro")))
		{
			bSmokeSkipVoted = true;
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Voting to skip the story"));
			ServerVoteSkipIntro();
		}
	}

	if (AHCGameMode* GM = GetWorld()->GetAuthGameMode<AHCGameMode>())
	{
		int32 MinPlayersForStart = 2;
		FParse::Value(FCommandLine::Get(), TEXT("HCMinPlayers="), MinPlayersForStart);
		if (SmokeAutoStart > 0.f && !Smoke.bStarted && !Smoke.bSawGameOver && Phase == EHCMatchPhase::Lobby && InPhase > SmokeAutoStart && GM->GetNumPlayers() >= FMath::Max(2, MinPlayersForStart))
		{
			Smoke.bStarted = true;
			FString ForcedRole;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCForceOwnRole="), ForcedRole) && PlayerState)
			{
				GM->ExecuteDebugCommand(TEXT("ForceRole"), { PlayerState->GetPlayerName(), ForcedRole }, nullptr);
			}
			FText Error;
			GM->RequestStartMatch(this, Error);
		}
		if (SmokeAutoEnd > 0.f && !Smoke.bEnded && Phase == EHCMatchPhase::Playing && InPhase > SmokeAutoEnd)
		{
			Smoke.bEnded = true;
			GM->ExecuteDebugCommand(TEXT("EndMatch"), { TEXT("Monster") }, nullptr);
		}
	}

	if (Phase == EHCMatchPhase::Playing)
	{
		if (SmokeAutoKillMe > 0.f && !bSmokeKillMeDone && InPhase > SmokeAutoKillMe)
		{
			bSmokeKillMeDone = true;
			ServerDebugCommand(TEXT("KillMe"), {});
		}
		// Killing and reporting come before the task list
		const bool bBusy = TickAutoMapShots(Now, InPhase) || TickAutoDevices(Now, InPhase) || TickAutoButton(Now, InPhase) || TickAutoRepair(Now) || TickAutoSabotage(Now) || TickAutoShift(Now) || (bSmokeAutoKill && TickAutoKill(Now)) || (bSmokeAutoReport && TickAutoReport(Now));
		if (bSmokeAutoTasks && !bBusy)
		{
			TickAutoTasks(Now);
		}
		if (bSmokeGhostCheck)
		{
			TickGhostCheck(Now);
		}
	}
	else
	{
		SmokeTaskState = 0;
		SmokeKillState = 0;
		if (Phase == EHCMatchPhase::Meeting)
		{
			TickAutoMeeting(Now);
		}
		else if (Phase == EHCMatchPhase::Lobby && bSmokePhaseShots && !bClient && !Smoke.bSawGameOver && SmokeLookShotState < 2
			&& FParse::Param(FCommandLine::Get(), TEXT("HCBadgeShot")))
		{
			// -HCBadgeShot: lobby picture of a name badge, face to face with the first other player
			const AHCCharacter* Own = Cast<AHCCharacter>(GetPawn());
			if (SmokeLookShotState == 0)
			{
				for (TActorIterator<AHCCharacter> It(GetWorld()); Own && It; ++It)
				{
					if (*It != Own && It->GetPlayerState() && !It->GetAppearance().DisplayName.IsEmpty())
					{
						SmokeLookTarget = *It;
						SmokeLookShotState = 1;
						SmokeLookShotSince = Now;
						ServerDebugCommand(TEXT("GotoPlayer"), { It->GetPlayerState()->GetPlayerName(), TEXT("front") });
						break;
					}
				}
			}
			else if (const AHCCharacter* Target = SmokeLookTarget.Get(); Target && Own)
			{
				SetControlRotation((Target->GetActorLocation() + FVector(0.f, 0.f, 88.f) - Own->GetCamera()->GetComponentLocation()).Rotation());
				if (Now - SmokeLookShotSince > 0.9)
				{
					SmokeLookShotState = 2;
					{
						// Which way does the model face? Toe in front of the ankle and right hand on +Y = it looks along the actor
						const FTransform ToActor = Target->GetActorTransform();
						const FVector Toe = ToActor.InverseTransformPosition(Target->GetMesh()->GetBoneLocation(TEXT("ball_r")));
						const FVector Ankle = ToActor.InverseTransformPosition(Target->GetMesh()->GetBoneLocation(TEXT("foot_r")));
						const FVector Hand = ToActor.InverseTransformPosition(Target->GetMesh()->GetBoneLocation(TEXT("hand_r")));
						const FVector Me = ToActor.InverseTransformPosition(Own->GetActorLocation());
						UE_LOG(LogHC, Display, TEXT("[HCSmoke] Facing check: camera at %s, toe-ankle X %.1f, right hand Y %.1f"), *Me.ToCompactString(), Toe.X - Ankle.X, Hand.Y);
					}
					FString Dir;
					if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
					{
						FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, TEXT("badge_lobby.png")), true, false);
					}
				}
			}
		}
	}

	// Host leaves after the round (tests the "host left" path); the main menu then takes the last screenshot and quits
	if (bSmokeQuitAfterRound && !bClient && Smoke.bSawGameOver && Phase == EHCMatchPhase::Lobby && !Smoke.bQuitting && InPhase > 6.0)
	{
		Smoke.bQuitting = true;
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] Round done, host leaves the session"));
		if (UHCSessionSubsystem* Sessions = GI->GetSubsystem<UHCSessionSubsystem>())
		{
			Sessions->LeaveToMainMenu();
		}
	}
#endif
}

void AHCPlayerController::StartSmokeTestIfRequested()
{
	bSmokePhaseShots = FParse::Param(FCommandLine::Get(), TEXT("HCPhaseShots"));
	bSmokeQuitAfterRound = FParse::Param(FCommandLine::Get(), TEXT("HCQuitAfterRound"));
	FParse::Value(FCommandLine::Get(), TEXT("HCAutoStart="), SmokeAutoStart);
	FParse::Value(FCommandLine::Get(), TEXT("HCAutoEnd="), SmokeAutoEnd);
	bSmokeAutoTasks = FParse::Param(FCommandLine::Get(), TEXT("HCAutoTasks"));
	bSmokeAutoKill = FParse::Param(FCommandLine::Get(), TEXT("HCAutoKill"));
	bSmokeAutoReport = FParse::Param(FCommandLine::Get(), TEXT("HCAutoReport"));
	bSmokeGhostCheck = FParse::Param(FCommandLine::Get(), TEXT("HCGhostCheck"));
	FParse::Value(FCommandLine::Get(), TEXT("HCAutoKillMe="), SmokeAutoKillMe);
	bSmokeRoundFlags = bSmokePhaseShots || bSmokeQuitAfterRound || SmokeAutoStart > 0.f || SmokeAutoEnd > 0.f || bSmokeAutoTasks
		|| bSmokeAutoKill || bSmokeAutoReport || bSmokeGhostCheck || SmokeAutoKillMe > 0.f
		|| FParse::Param(FCommandLine::Get(), TEXT("HCStoryShots")) || FParse::Param(FCommandLine::Get(), TEXT("HCAutoSkipIntro"));

	int32 Shots = 0;
	if (!FParse::Value(FCommandLine::Get(), TEXT("HCShots="), Shots) || Shots <= 0)
	{
		return;
	}
	if (!FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), SmokeShotDir))
	{
		SmokeShotDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("HCSmoke"));
	}
	IFileManager::Get().MakeDirectory(*SmokeShotDir, true);
	SmokeShotsLeft = Shots;
	SmokeShotIndex = 0;
	UE_LOG(LogHC, Display, TEXT("[HCSmoke] %d screenshots into %s"), Shots, *SmokeShotDir);
	// Give the world 4 s to settle (shaders, Lumen), then one step every 1.5 s
	GetWorldTimerManager().SetTimer(SmokeTimer, this, &AHCPlayerController::SmokeTestStep, 1.5f, true, 4.f);
}

void AHCPlayerController::SmokeTestStep()
{
	// -HCCleanShots: pictures without any HUD (used for the launcher artwork)
	if (AHUD* CleanHUD = FParse::Param(FCommandLine::Get(), TEXT("HCCleanShots")) ? GetHUD() : nullptr)
	{
		CleanHUD->bShowHUD = false;
	}
	AHCCharacter* HCChar = Cast<AHCCharacter>(GetPawn());
	if (SmokeShotIndex == 0 && HCChar)
	{
		HCChar->ServerSetFlashlight(true);
	}
	if (SmokeShotsLeft > 0)
	{
		const FString File = FPaths::Combine(SmokeShotDir, FString::Printf(TEXT("%s_%02d.png"), IsNetMode(NM_Client) ? TEXT("client") : TEXT("host"), SmokeShotIndex));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		if (HCChar)
		{
			UE_LOG(LogHC, Display, TEXT("[HCSmoke] Shot %d at %s yaw %.0f battery %.2f flashlight %d"), SmokeShotIndex,
				*HCChar->GetActorLocation().ToCompactString(), GetControlRotation().Yaw, HCChar->GetBatteryFraction(), HCChar->IsFlashlightOn() ? 1 : 0);
		}
		++SmokeShotIndex;
		--SmokeShotsLeft;
		// Turn for the next picture
		SetControlRotation(GetControlRotation() + FRotator(0.f, 90.f, 0.f));
		if (SmokeShotsLeft == 0 && HCChar)
		{
			// Face south (+Y, towards the door of the command center) and walk for the last part
			SetControlRotation(FRotator(0.f, 90.f, 0.f));
		}
		return;
	}
	// Walk forward a few steps, then quit
	if (HCChar && SmokeShotIndex < 100)
	{
		SmokeShotIndex = 100;
		GetWorldTimerManager().ClearTimer(SmokeTimer);
		bSmokeWalking = true; // input is added every frame in PlayerTick, like a held key
		FTimerHandle QuitTimer;
		GetWorldTimerManager().SetTimer(QuitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			bSmokeWalking = false;
			if (AHCCharacter* C = Cast<AHCCharacter>(GetPawn()))
			{
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] Walked to %s"), *C->GetActorLocation().ToCompactString());
			}
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(SmokeShotDir, IsNetMode(NM_Client) ? TEXT("client_walk.png") : TEXT("host_walk.png")), true, false);
			FTimerHandle ExitTimer;
			GetWorldTimerManager().SetTimer(ExitTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] Done"));
				ConsoleCommand(TEXT("quit"));
			}), 1.5f, false);
		}), 4.f, false);
	}
}

void AHCPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	// A key changed in the settings menu: map it right away, also in the middle of a round
	if (InputConfig && InputConfig->AppliedRevision != HCKeys::GetRevision() && IsLocalController())
	{
		ApplyMappingContext();
	}
	if (bSmokeRoundFlags)
	{
		TickRoundSmokeTest();
	}
	if (!bSmokeWalking)
	{
		return;
	}
	AHCCharacter* C = Cast<AHCCharacter>(GetPawn());
	if (!C)
	{
		return;
	}
	// Smoke test walk: through door Kommando_SO (center x = 50.5 m, y = 12 m) into the corridor behind it
	const FVector Target(5050.f, 1700.f, C->GetActorLocation().Z);
	const FVector ToTarget = Target - C->GetActorLocation();
	if (ToTarget.Size2D() > 30.f)
	{
		SetControlRotation(ToTarget.Rotation());
		C->AddMovementInput(ToTarget.GetSafeNormal2D(), 1.f);
	}
	if ((++SmokeWalkTicks % 60) == 0)
	{
		const UCharacterMovementComponent* Move = C->GetCharacterMovement();
		UE_LOG(LogHC, Display, TEXT("[HCSmoke] walk frame %d loc %s speed %.0f"), SmokeWalkTicks,
			*C->GetActorLocation().ToCompactString(), Move->Velocity.Size2D());
	}
}

void AHCPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (IsLocalController())
	{
		ApplyMappingContext();
	}
}

void AHCPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);
	ApplyMappingContext();
}

void AHCPlayerController::ApplyMappingContext()
{
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		UInputMappingContext* Context = GetInputConfig()->DefaultContext;
		if (GetInputConfig()->AppliedRevision != HCKeys::GetRevision())
		{
			GetInputConfig()->RebuildMappings();
			if (Subsystem->HasMappingContext(Context))
			{
				Subsystem->RequestRebuildControlMappings();
			}
		}
		if (!Subsystem->HasMappingContext(Context))
		{
			Subsystem->AddMappingContext(Context, 0);
		}
	}
}
