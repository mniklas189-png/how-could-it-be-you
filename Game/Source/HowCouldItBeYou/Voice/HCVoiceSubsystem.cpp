#include "Voice/HCVoiceSubsystem.h"

#include "Components/AudioComponent.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameState.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerState.h"
#include "Voice/HCAudioDevices.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/VoiceInterface.h"
#include "Misc/CommandLine.h"
#include "OnlineSubsystemUtils.h"
#include "Camera/PlayerCameraManager.h"

/** Sees every key and mouse button before any widget does, so the talk key works in menus and in the meeting too. */
class FHCVoiceKeyWatcher : public IInputProcessor
{
public:
	FKey TalkKey = EKeys::V;
	bool bDown = false;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
		if (!SlateApp.IsActive())
		{
			bDown = false; // the window lost the focus: the key-up would never arrive
		}
	}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		bDown |= InKeyEvent.GetKey() == TalkKey;
		return false; // never swallowed: the key keeps its other uses
	}
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		bDown &= InKeyEvent.GetKey() != TalkKey;
		return false;
	}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		bDown |= MouseEvent.GetEffectingButton() == TalkKey;
		return false;
	}
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		bDown &= MouseEvent.GetEffectingButton() != TalkKey;
		return false;
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("HCVoiceKey"); }
};

void UHCVoiceTalker::OnTalkingBegin(UAudioComponent* AudioComponent)
{
	Super::OnTalkingBegin(AudioComponent);
	Playing = AudioComponent;
	SetLoudness(Loudness);
}

void UHCVoiceTalker::OnTalkingEnd()
{
	Super::OnTalkingEnd();
	Playing.Reset();
}

void UHCVoiceTalker::SetLoudness(float InLoudness)
{
#if !UE_BUILD_SHIPPING
	// Development: -HCVoiceNoVolume leaves the playback volume to the engine (to tell its faults from ours)
	static const bool bHandsOff = FParse::Param(FCommandLine::Get(), TEXT("HCVoiceNoVolume"));
	if (bHandsOff)
	{
		return;
	}
#endif
	const bool bChanged = !FMath::IsNearlyEqual(Loudness, InLoudness, 0.02f);
	Loudness = InLoudness;
	if (UAudioComponent* AudioComponent = Playing.Get(); AudioComponent && (bChanged || !FMath::IsNearlyEqual(AudioComponent->VolumeMultiplier, 1.5f * Loudness, 0.03f)))
	{
		// The engine's own default for voices is 1.5
		AudioComponent->SetVolumeMultiplier(FMath::Max(0.001f, 1.5f * Loudness));
	}
}

bool UHCVoiceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && !IsRunningDedicatedServer();
}

void UHCVoiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (FSlateApplication::IsInitialized())
	{
		KeyWatcher = MakeShared<FHCVoiceKeyWatcher>();
		FSlateApplication::Get().RegisterInputPreProcessor(KeyWatcher);
	}
	HudHandle = AHUD::OnHUDPostRender.AddUObject(this, &UHCVoiceSubsystem::DrawHud);
}

void UHCVoiceSubsystem::Deinitialize()
{
	AHUD::OnHUDPostRender.Remove(HudHandle);
	if (KeyWatcher.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(KeyWatcher);
	}
	KeyWatcher.Reset();
	if (bTalking)
	{
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			PC->StopTalking();
		}
		bTalking = false;
	}
	Super::Deinitialize();
}

void UHCVoiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || !World->GetGameState() || World->GetNetMode() == NM_Standalone || !Online::GetVoiceInterface(World).IsValid())
	{
		return; // main menu (nobody to talk to), or voice switched off in the engine configuration
	}
	ApplySettings();
	// The talk key is one of the player's own keys (settings menu, page "Steuerung")
	if (KeyWatcher.IsValid() && KeyWatcher->TalkKey != HCKeys::Get(EHCBinding::Talk))
	{
		KeyWatcher->TalkKey = HCKeys::Get(EHCBinding::Talk);
		KeyWatcher->bDown = false;
	}
	UpdateTalkers();
	UpdateMicrophone();
	UpdateTalking();
	UpdateMutes();
#if !UE_BUILD_SHIPPING
	// Development: -HCVoiceTest keeps the microphone open and logs what this machine sends and hears
	if (FParse::Param(FCommandLine::Get(), TEXT("HCVoiceTest")) && World->GetTimeSeconds() >= NextTestLog)
	{
		NextTestLog = World->GetTimeSeconds() + 3.0;
		const IOnlineVoicePtr Voice = Online::GetVoiceInterface(World);
		TArray<FString> Heard;
		for (const TPair<TObjectPtr<APlayerState>, TObjectPtr<UHCVoiceTalker>>& Entry : Talkers)
		{
			if (Entry.Key && Entry.Key->GetUniqueId().IsValid() && Voice->IsRemotePlayerTalking(*Entry.Key->GetUniqueId()))
			{
				Heard.Add(Entry.Key->GetPlayerName());
			}
		}
		UE_LOG(LogHC, Display, TEXT("[HCVoice] sending %d (microphone picks up %d, device %d), %d other players known, hearing: %s"), bTalking ? 1 : 0,
			Voice->IsLocalPlayerTalking(0) ? 1 : 0, Voice->IsHeadsetPresent(0) ? 1 : 0, Talkers.Num(), Heard.Num() > 0 ? *FString::Join(Heard, TEXT(", ")) : TEXT("nobody"));
	}
#endif
}

void UHCVoiceSubsystem::ApplySettings()
{
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetWorld()->GetGameInstance());
	if (!GI || GI->GetOptionsRevision() == AppliedRevision)
	{
		return;
	}
	AppliedRevision = GI->GetOptionsRevision();
	bMicDirty = true;
	if (IConsoleVariable* Gain = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicInputGain")))
	{
		Gain->Set(GI->GetOption(TEXT("MicGain"), 1.f), ECVF_SetByGameSetting);
	}
	// Open microphone: how loud a sound must be to count as speech (step 1 = everything, step 10 = only loud voices)
	float Threshold = 0.01f + 0.022f * (GI->GetOption(TEXT("MicThreshold"), 4.f) - 1.f);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("HCVoiceTest")))
	{
		Threshold = 0.f;
	}
#endif
	UVOIPStatics::SetMicThreshold(Threshold);
}

void UHCVoiceSubsystem::UpdateTalkers()
{
	UWorld* World = GetWorld();
	const IOnlineVoicePtr Voice = Online::GetVoiceInterface(World);
	const APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || !PC->PlayerState)
	{
		return;
	}
	if (!bLocalRegistered)
	{
		bLocalRegistered = true;
		const bool bOk = Voice->RegisterLocalTalker(0);
		UE_LOG(LogHC, Log, TEXT("Voice chat: own microphone %s"), bOk ? TEXT("registered") : TEXT("NOT registered (no microphone?)"));
	}
	for (auto It = Talkers.CreateIterator(); It; ++It)
	{
		if (!IsValid(It.Key()) || !IsValid(It.Value()))
		{
			It.RemoveCurrent();
		}
	}
	const UHCGameInstance* GI = Cast<UHCGameInstance>(World->GetGameInstance());
	const float VoiceVolume = GI ? GI->GetOption(TEXT("VoiceVolume"), 1.f) : 1.f;
	for (APlayerState* PS : World->GetGameState()->PlayerArray)
	{
		// By id, not by object: after a map change an old copy of the own player state can still be listed
		if (!PS || PS == PC->PlayerState || !PS->GetUniqueId().IsValid() || PS->GetUniqueId() == PC->PlayerState->GetUniqueId())
		{
			continue;
		}
		TObjectPtr<UHCVoiceTalker>& Talker = Talkers.FindOrAdd(PS);
		if (!Talker)
		{
			Voice->RegisterRemoteTalker(*PS->GetUniqueId());
			Talker = NewObject<UHCVoiceTalker>(PS);
			Talker->RegisterComponent();
			Talker->RegisterWithPlayerState(PS);
			UE_LOG(LogHC, Log, TEXT("Voice chat: can hear %s"), *PS->GetPlayerName());
		}
		// Clear up to about 4 m, gone at about 27 m: you hear who stands with you, and faintly who is down the corridor.
		// Without a figure on either side (loading, menus) the voice is simply heard.
		float Nearness = 1.f;
		const APawn* Pawn = PS->GetPawn();
		if (Pawn && PC->PlayerCameraManager)
		{
			const float Distance = FVector::Dist(Pawn->GetActorLocation(), PC->PlayerCameraManager->GetCameraLocation());
			Nearness = FMath::Square(1.f - FMath::Clamp((Distance - 400.f) / 2300.f, 0.f, 1.f));
		}
		Talker->SetLoudness(Nearness * VoiceVolume);
	}
}

void UHCVoiceSubsystem::UpdateMicrophone()
{
	// The microphone chosen in the settings menu; the engine starts with the Windows default
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetWorld()->GetGameInstance());
	if (!bMicDirty || !bLocalRegistered || !GI)
	{
		return;
	}
	bMicDirty = false;
	const FString Mic = GI->GetTextOption(TEXT("MicDevice"));
	if (Mic == AppliedMic)
	{
		return;
	}
	AppliedMic = Mic;
	if (HCAudioDevices::SetVoiceMicrophone(GetWorld(), Mic) && bTalking)
	{
		// A new capture does not run yet: stop talking, the next tick starts again
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			PC->StopTalking();
		}
		bTalking = false;
	}
}

void UHCVoiceSubsystem::UpdateTalking()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World->GetFirstPlayerController();
	const UHCGameInstance* GI = Cast<UHCGameInstance>(World->GetGameInstance());
	if (!PC || !GI)
	{
		return;
	}
	bool bWant = false;
	if (GI->GetOption(TEXT("VoiceEnabled"), 1.f) > 0.5f)
	{
		if (GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f)
		{
			bWant = true; // open microphone: the engine only sends when it hears something
		}
		else if (KeyWatcher.IsValid() && KeyWatcher->bDown && FSlateApplication::IsInitialized())
		{
			// Not while typing: the talk key may be a letter
			const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
			bWant = !(Focus.IsValid() && Focus->GetType().ToString().Contains(TEXT("Editable")));
		}
	}
#if !UE_BUILD_SHIPPING
	bWant |= FParse::Param(FCommandLine::Get(), TEXT("HCVoiceTest"));
#endif
	if (bWant != bTalking)
	{
		bTalking = bWant;
		if (bWant)
		{
			PC->StartTalking();
		}
		else
		{
			PC->StopTalking();
		}
	}
}

void UHCVoiceSubsystem::UpdateMutes()
{
	// The host cuts the line from the dead to the living while a round runs; ghosts hear everybody
	UWorld* World = GetWorld();
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	if (!GS || World->GetNetMode() == NM_Client || World->GetNetMode() == NM_Standalone || World->GetTimeSeconds() < NextMuteCheck)
	{
		return;
	}
	NextMuteCheck = World->GetTimeSeconds() + 0.5;
	const EHCMatchPhase Phase = GS->GetMatchPhase();
	const bool bRound = Phase != EHCMatchPhase::Lobby && Phase != EHCMatchPhase::Loading && Phase != EHCMatchPhase::GameOver;
	TSet<uint64> Wanted;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Listener = It->Get();
		const AHCPlayerState* ListenerState = Listener ? Listener->GetPlayerState<AHCPlayerState>() : nullptr;
		if (!ListenerState)
		{
			continue;
		}
		for (APlayerState* Other : GS->PlayerArray)
		{
			const AHCPlayerState* Talker = Cast<AHCPlayerState>(Other);
			if (!Talker || Talker == ListenerState || !Talker->GetUniqueId().IsValid())
			{
				continue;
			}
			const uint64 Key = (static_cast<uint64>(static_cast<uint32>(ListenerState->GetPlayerId())) << 32) | static_cast<uint32>(Talker->GetPlayerId());
			const bool bMute = bRound && !Talker->IsAlive() && ListenerState->IsAlive();
			if (bMute)
			{
				Wanted.Add(Key);
			}
			if (bMute && !Muted.Contains(Key))
			{
				Listener->GameplayMutePlayer(Talker->GetUniqueId());
				UE_LOG(LogHC, Log, TEXT("Voice chat: %s no longer hears %s (dead)"), *ListenerState->GetPlayerName(), *Talker->GetPlayerName());
			}
			else if (!bMute && (Muted.Contains(Key) || !bMutesReset))
			{
				// Also once after every map change: the players keep their mute lists across the change, this list does not
				Listener->GameplayUnmutePlayer(Talker->GetUniqueId());
			}
		}
	}
	Muted = MoveTemp(Wanted);
	bMutesReset = true;
}

void UHCVoiceSubsystem::DrawHud(AHUD* HUD, UCanvas* Canvas)
{
	UWorld* World = GetWorld();
	const UHCGameInstance* GI = World ? Cast<UHCGameInstance>(World->GetGameInstance()) : nullptr;
	if (!HUD || !Canvas || HUD->GetWorld() != World || !GI || !World->GetGameState() || World->GetNetMode() == NM_Standalone || !GEngine || GI->GetOption(TEXT("VoiceEnabled"), 1.f) < 0.5f)
	{
		return;
	}
	const IOnlineVoicePtr Voice = Online::GetVoiceInterface(World);
	if (!Voice.IsValid())
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float X = 30.f;
	float Y = Canvas->ClipY - 96.f;
	// Your own microphone
	const bool bOpen = GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f;
	const bool bHeard = bTalking && (!bOpen || Voice->IsLocalPlayerTalking(0));
	const FText Own = bHeard ? NSLOCTEXT("HC", "VoiceTalking", "MIKROFON AN - du sprichst")
		: bOpen ? NSLOCTEXT("HC", "VoiceOpen", "MIKROFON OFFEN") : FText::Format(NSLOCTEXT("HC", "VoiceHoldFmt", "SPRECHEN  [{0}] halten"), HCKeys::ShortName(EHCBinding::Talk));
	Canvas->SetDrawColor(bHeard ? FColor(110, 240, 140) : FColor(190, 196, 205, 150));
	Canvas->DrawText(Font, Own.ToString(), X, Y);
	// Who is talking (only the voices this machine really receives)
	for (const TPair<TObjectPtr<APlayerState>, TObjectPtr<UHCVoiceTalker>>& Entry : Talkers)
	{
		if (Entry.Key && Entry.Key->GetUniqueId().IsValid() && Voice->IsRemotePlayerTalking(*Entry.Key->GetUniqueId()))
		{
			Y -= 18.f;
			Canvas->SetDrawColor(FColor(240, 228, 160));
			Canvas->DrawText(Font, FText::Format(NSLOCTEXT("HC", "VoiceSpeaksFmt", "((  {0}  spricht"), FText::FromString(Entry.Key->GetPlayerName())).ToString(), X, Y);
		}
	}
}
