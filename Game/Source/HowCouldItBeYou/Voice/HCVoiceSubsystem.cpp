#include "Voice/HCVoiceSubsystem.h"

#include "Components/AudioComponent.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameState.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerState.h"
#include "Voice/HCAudioDevices.h"
#include "Voice/HCVoiceRules.h"
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
#include "UObject/UObjectIterator.h"
#include "VoipListenerSynthComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"

namespace HCVoicePlayback
{
	FDelegateHandle WorldCleanupHandle;
	int32 LiveSubsystems = 0;
	/** Other players this machine registered as talkers (UHCVoiceSubsystem::UpdateTalkers). */
	TArray<FUniqueNetIdRepl> KnownTalkers;
	/** Released voice playbacks, kept alive until the given time. */
	TArray<TPair<TWeakObjectPtr<UVoipListenerSynthComponent>, double>> Kept;
	FTSTicker::FDelegateHandle KeptTicker;
	constexpr double KeepSeconds = 2.0;

	bool ReleaseKept(float /*DeltaTime*/)
	{
		const double Now = FPlatformTime::Seconds();
		for (int32 Index = Kept.Num() - 1; Index >= 0; --Index)
		{
			if (Now >= Kept[Index].Value)
			{
				if (UVoipListenerSynthComponent* Voice = Kept[Index].Key.Get())
				{
					Voice->RemoveFromRoot();
				}
				Kept.RemoveAtSwap(Index);
			}
		}
		if (Kept.IsEmpty())
		{
			KeptTicker.Reset();
			return false;
		}
		return true;
	}

	/**
	 * The engine plays every voice of another player through a UVoipListenerSynthComponent that it registers with the
	 * world, and lets go of it only after a map load (FCoreUObjectDelegates::PostLoadMapWithWorld). With seamless
	 * travel that comes after the old world was already destroyed: a voice that was still playing kept its audio
	 * component registered to a dead world. That tripped an ensure in the renderer ("ActorComponent->GetScene() !=
	 * this") and a few seconds later crashed a worker thread, mostly with three or more players talking during the
	 * change from the lobby to the station.
	 *
	 * So when a world is cleaned up (before it is destroyed), its voices get what the engine's own
	 * FRemoteTalkerDataImpl::Reset does, just in time:
	 * - stopped and unregistered (that also destroys the audio component and clears the component's world);
	 * - the voice engine forgets them: each known talker is unregistered and registered again (mutes are kept in a list
	 *   of their own), so the next voice packet gets a fresh playback in the world that is current then. A playback the
	 *   engine kept would be started again without a world, and its sounds trip ensures in the audio mixer;
	 * - kept alive for two more seconds: a voice that started a moment ago may still be on its way to the audio thread,
	 *   which expects the component to exist (ensure in USynthSound::OnBeginGenerate). Marking it as garbage, or letting
	 *   the garbage collection of the map change take it at once, would trip that.
	 */
	void ReleaseVoicesOf(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
	{
		if (!World)
		{
			return;
		}
		int32 Released = 0;
		for (TObjectIterator<UVoipListenerSynthComponent> It; It; ++It)
		{
			UVoipListenerSynthComponent* Voice = *It;
			if (!IsValid(Voice) || Voice->GetWorld() != World)
			{
				continue;
			}
			Voice->Stop();
			if (UAudioComponent* AudioComponent = Voice->GetAudioComponent(); AudioComponent && AudioComponent->IsRegistered())
			{
				AudioComponent->UnregisterComponent();
			}
			if (Voice->IsRegistered())
			{
				Voice->UnregisterComponent();
			}
			if (!Voice->IsRooted())
			{
				Voice->AddToRoot();
				Kept.Emplace(Voice, FPlatformTime::Seconds() + KeepSeconds);
			}
			++Released;
		}
		if (Released == 0)
		{
			return;
		}
		if (!KeptTicker.IsValid())
		{
			KeptTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ReleaseKept), 0.5f);
		}
		int32 Renewed = 0;
		if (const IOnlineVoicePtr VoiceInterface = Online::GetVoiceInterface(World))
		{
			for (int32 Index = KnownTalkers.Num() - 1; Index >= 0; --Index)
			{
				const FUniqueNetIdRepl& Id = KnownTalkers[Index];
				if (Id.IsValid() && VoiceInterface->UnregisterRemoteTalker(*Id))
				{
					VoiceInterface->RegisterRemoteTalker(*Id);
					++Renewed;
				}
				else
				{
					KnownTalkers.RemoveAtSwap(Index); // no longer registered: the engine has let go of it already
				}
			}
		}
		UE_LOG(LogHC, Log, TEXT("Voice chat: %d voice playback(s) of %s released before the map change (%d talker(s) renewed)"), Released, *World->GetMapName(), Renewed);
	}
}

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
	if (HCVoicePlayback::LiveSubsystems++ == 0)
	{
		HCVoicePlayback::WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&HCVoicePlayback::ReleaseVoicesOf);
	}
}

void UHCVoiceSubsystem::Deinitialize()
{
	AHUD::OnHUDPostRender.Remove(HudHandle);
	if (const IOnlineVoicePtr Voice = BoundVoice.Pin())
	{
		Voice->ClearOnPlayerTalkingStateChangedDelegate_Handle(TalkingStateHandle);
	}
	BoundVoice.Reset();
	TalkingNow.Reset();
	// The world's own cleanup (which releases its voices) runs before this
	if (--HCVoicePlayback::LiveSubsystems == 0)
	{
		FWorldDelegates::OnWorldCleanup.Remove(HCVoicePlayback::WorldCleanupHandle);
		HCVoicePlayback::WorldCleanupHandle.Reset();
	}
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
	if (!TalkingStateHandle.IsValid())
	{
		const IOnlineVoicePtr Voice = Online::GetVoiceInterface(World);
		TalkingStateHandle = Voice->AddOnPlayerTalkingStateChangedDelegate_Handle(FOnPlayerTalkingStateChangedDelegate::CreateUObject(this, &UHCVoiceSubsystem::HandleTalkingState));
		BoundVoice = Voice;
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
			if (IsHeardNow(Entry.Key))
			{
				Heard.Add(Entry.Key->GetPlayerName());
			}
		}
		UE_LOG(LogHC, Display, TEXT("[HCVoice] sending %d (microphone picks up %d, device %d), %d other players known, hearing: %s"), bTalking ? 1 : 0,
			Voice->IsLocalPlayerTalking(0) ? 1 : 0, Voice->IsHeadsetPresent(0) ? 1 : 0, Talkers.Num(), Heard.Num() > 0 ? *FString::Join(Heard, TEXT(", ")) : TEXT("nobody"));
	}
#endif
}

void UHCVoiceSubsystem::HandleTalkingState(FUniqueNetIdRef TalkerId, bool bIsTalking)
{
	if (bIsTalking)
	{
		TalkingNow.Add(TalkerId->ToString());
	}
	else
	{
		TalkingNow.Remove(TalkerId->ToString());
	}
}

bool UHCVoiceSubsystem::IsHeardNow(const APlayerState* PlayerState) const
{
	return PlayerState && PlayerState->GetUniqueId().IsValid() && TalkingNow.Contains(PlayerState->GetUniqueId()->ToString());
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
	// The host's rule: in meetings and with "always" every voice is equally loud
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	const bool bDistance = !GS || HCVoiceRules::UsesDistance(GS->GetVoiceChatRule(), GS->GetMatchPhase());
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
			HCVoicePlayback::KnownTalkers.AddUnique(PS->GetUniqueId());
			Talker = NewObject<UHCVoiceTalker>(PS);
			Talker->RegisterComponent();
			Talker->RegisterWithPlayerState(PS);
			UE_LOG(LogHC, Log, TEXT("Voice chat: can hear %s"), *PS->GetPlayerName());
		}
		// Clear up to about 4 m, gone at about 27 m: you hear who stands with you, and faintly who is down the corridor.
		// Without a figure on either side (loading, menus) the voice is simply heard.
		float Nearness = 1.f;
		const APawn* Pawn = PS->GetPawn();
		if (bDistance && Pawn && PC->PlayerCameraManager)
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
	// The host's rule: with "only in meetings" nobody hears the living outside a meeting, so nothing is sent
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	const AHCPlayerState* Own = PC->GetPlayerState<AHCPlayerState>();
	bSendAllowed = !GS || !Own || HCVoiceRules::CanSend(GS->GetVoiceChatRule(), GS->GetMatchPhase(), Own->IsAlive());
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
	bWant &= bSendAllowed;
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
	// The host cuts every line the voice chat rule does not allow (HCVoiceRules::CanHear): from the dead to the living
	// while a round runs, and with "only in meetings" from the living outside a meeting
	UWorld* World = GetWorld();
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	if (!GS || World->GetNetMode() == NM_Client || World->GetNetMode() == NM_Standalone || World->GetTimeSeconds() < NextMuteCheck)
	{
		return;
	}
	NextMuteCheck = World->GetTimeSeconds() + 0.5;
	const EHCMatchPhase Phase = GS->GetMatchPhase();
	const EHCVoiceChatRule Rule = GS->GetVoiceChatRule();
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
			const bool bMute = !HCVoiceRules::CanHear(Rule, Phase, Talker->IsAlive(), ListenerState->IsAlive());
			if (bMute)
			{
				Wanted.Add(Key);
			}
			if (bMute && !Muted.Contains(Key))
			{
				Listener->GameplayMutePlayer(Talker->GetUniqueId());
				UE_LOG(LogHC, Verbose, TEXT("Voice chat: %s no longer hears %s"), *ListenerState->GetPlayerName(), *Talker->GetPlayerName());
			}
			else if (!bMute && (Muted.Contains(Key) || !bMutesReset))
			{
				// Also once after every map change: the players keep their mute lists across the change, this list does not
				Listener->GameplayUnmutePlayer(Talker->GetUniqueId());
			}
		}
	}
	if (Wanted.Num() != Muted.Num() || !Wanted.Includes(Muted))
	{
		UE_LOG(LogHC, Log, TEXT("Voice chat: %d line(s) cut (rule %s, phase %s)"), Wanted.Num(), *UEnum::GetValueAsString(Rule), HCIdentity::PhaseName(Phase));
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
		: !bSendAllowed ? NSLOCTEXT("HC", "VoiceMeetingsOnly", "MIKROFON AUS - Sprachchat nur in Meetings")
		: bOpen ? NSLOCTEXT("HC", "VoiceOpen", "MIKROFON OFFEN") : FText::Format(NSLOCTEXT("HC", "VoiceHoldFmt", "SPRECHEN  [{0}] halten"), HCKeys::ShortName(EHCBinding::Talk));
	Canvas->SetDrawColor(bHeard ? FColor(110, 240, 140) : FColor(190, 196, 205, 150));
	Canvas->DrawText(Font, Own.ToString(), X, Y);
	// Who is talking (only the voices this machine really receives)
	for (const TPair<TObjectPtr<APlayerState>, TObjectPtr<UHCVoiceTalker>>& Entry : Talkers)
	{
		if (IsHeardNow(Entry.Key))
		{
			Y -= 18.f;
			Canvas->SetDrawColor(FColor(240, 228, 160));
			Canvas->DrawText(Font, FText::Format(NSLOCTEXT("HC", "VoiceSpeaksFmt", "((  {0}  spricht"), FText::FromString(Entry.Key->GetPlayerName())).ToString(), X, Y);
		}
	}
}
