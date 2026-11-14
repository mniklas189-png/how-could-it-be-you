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
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Interfaces/VoiceInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/Optional.h"
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

	/**
	 * Ids of the players whose voice arrives right now, from the voice interface's talking-state events. Not
	 * IOnlineVoice::IsRemotePlayerTalking: that stays true for as long as a player who once spoke is connected (the
	 * engine never drops their buffer). Kept for the whole process and listened to all the time, like the voice
	 * interface itself: an event that comes during a map change, while no world's subsystem exists, would otherwise
	 * be lost, and somebody who talks on without a pause would never show up as talking on the new map.
	 */
	TSet<FString> TalkingNow;
	FDelegateHandle TalkingStateHandle;
	TWeakPtr<IOnlineVoice, ESPMode::ThreadSafe> BoundVoice;

	void HandleTalkingState(FUniqueNetIdRef TalkerId, bool bIsTalking)
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

	/** Listens to the talking-state events of this voice interface (once; again if the interface was replaced). */
	void ListenToTalkingState(const IOnlineVoicePtr& Voice)
	{
		if (!Voice.IsValid() || BoundVoice.Pin() == Voice)
		{
			return;
		}
		if (const IOnlineVoicePtr Old = BoundVoice.Pin())
		{
			Old->ClearOnPlayerTalkingStateChangedDelegate_Handle(TalkingStateHandle);
		}
		TalkingNow.Reset();
		TalkingStateHandle = Voice->AddOnPlayerTalkingStateChangedDelegate_Handle(FOnPlayerTalkingStateChangedDelegate::CreateStatic(&HandleTalkingState));
		BoundVoice = Voice;
	}

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

void UHCVoiceTalker::Adopt(UAudioComponent* AudioComponent)
{
	Playing = AudioComponent;
	SetLoudness(Loudness);
}

/** What belongs to the player at this machine and not to a map: there is one UHCVoiceSubsystem per map. */
namespace HCVoiceLocal
{
	/** One for the whole game: a talk key that is held through a map change stays held. */
	TSharedPtr<FHCVoiceKeyWatcher> KeyWatcher;

	/** Starts watching the talk key, until the game ends (not until the map ends: between two maps there is none). */
	void WatchTalkKey()
	{
		if (KeyWatcher.IsValid() || !FSlateApplication::IsInitialized())
		{
			return;
		}
		KeyWatcher = MakeShared<FHCVoiceKeyWatcher>();
		FSlateApplication::Get().RegisterInputPreProcessor(KeyWatcher);
		FCoreDelegates::OnPreExit.AddLambda([]()
		{
			if (KeyWatcher.IsValid() && FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().UnregisterInputPreProcessor(KeyWatcher);
			}
			KeyWatcher.Reset();
		});
	}

	/** Microphone the engine's capture was given last (unset: none yet since the game started). */
	TOptional<FString> AppliedMic;
	double NextMicRetry = 0.0;
	double NextRegisterTry = 0.0;
	/** Push-to-talk: the microphone sends until then (the key is down, or was a moment ago). */
	double SendUntil = 0.0;
	/**
	 * How long the microphone goes on sending after the talk key was let go. The end of the last word is still on its
	 * way through the capture then (it delivers in pieces of about 30 ms), and fingers are a little faster than mouths.
	 */
	constexpr double ReleaseTail = 0.25;

#if !UE_BUILD_SHIPPING
	/** Development: -HCVoiceTest keeps the microphone sending and logs what this machine sends and hears. */
	bool IsSendTest()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("HCVoiceTest"));
		return bOn;
	}

	/**
	 * Development: -HCVoiceKey=<period>:<from>:<to> holds the talk key by the clock, from second <from> to second <to>
	 * of every <period> seconds. The clock is the same for all instances on one PC, so each gets a slot of its own and
	 * the logs show who was heard outside their slot. Every change is logged ("[HCVoice] change").
	 */
	struct FKeyTest
	{
		bool bOn = false;
		double Period = 0.0;
		double From = 0.0;
		double To = 0.0;
	};

	const FKeyTest& KeyTest()
	{
		static const FKeyTest Test = []()
		{
			FKeyTest Parsed;
			FString Text;
			TArray<FString> Parts;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCVoiceKey="), Text) && Text.ParseIntoArray(Parts, TEXT(":")) == 3)
			{
				Parsed.Period = FCString::Atod(*Parts[0]);
				Parsed.From = FCString::Atod(*Parts[1]);
				Parsed.To = FCString::Atod(*Parts[2]);
				Parsed.bOn = Parsed.Period > 0.0;
			}
			return Parsed;
		}();
		return Test;
	}

	bool IsTestKeyDown()
	{
		const FKeyTest& Test = KeyTest();
		const double Clock = FMath::Fmod((FDateTime::UtcNow() - FDateTime(2026, 1, 1)).GetTotalSeconds(), Test.Period);
		return Clock >= Test.From && Clock < Test.To;
	}
#endif

	/** Is the talk key held (and not as a letter in a text field)? */
	bool IsTalkKeyHeld()
	{
#if !UE_BUILD_SHIPPING
		if (KeyTest().bOn)
		{
			return IsTestKeyDown();
		}
#endif
		if (!KeyWatcher.IsValid() || !KeyWatcher->bDown || !FSlateApplication::IsInitialized())
		{
			return false;
		}
		// Not while typing: the talk key may be a letter
		const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
		return !(Focus.IsValid() && Focus->GetType().ToString().Contains(TEXT("Editable")));
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
	HudHandle = AHUD::OnHUDPostRender.AddUObject(this, &UHCVoiceSubsystem::DrawHud);
	if (HCVoicePlayback::LiveSubsystems++ == 0)
	{
		HCVoicePlayback::WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&HCVoicePlayback::ReleaseVoicesOf);
	}
	HCVoiceLocal::WatchTalkKey();
}

void UHCVoiceSubsystem::Deinitialize()
{
	AHUD::OnHUDPostRender.Remove(HudHandle);
	// The world's own cleanup (which releases its voices) runs before this
	if (--HCVoicePlayback::LiveSubsystems == 0)
	{
		FWorldDelegates::OnWorldCleanup.Remove(HCVoicePlayback::WorldCleanupHandle);
		HCVoicePlayback::WorldCleanupHandle.Reset();
	}
	// The microphone is left as it is: the subsystem of the next map goes on with it (in the main menu: closes it)
	bTalking = false;
	Super::Deinitialize();
}

APlayerController* UHCVoiceSubsystem::LocalController() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetFirstLocalPlayerController(World) : nullptr;
}

void UHCVoiceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	const IOnlineVoicePtr Voice = World ? Online::GetVoiceInterface(World) : nullptr;
	if (!Voice.IsValid())
	{
		return; // voice switched off in the engine configuration
	}
	if (World->GetNetMode() == NM_Standalone)
	{
		// Main menu: nobody to talk to. The engine may have opened the microphone by itself (it does when a session
		// registers the local player once more).
		HCVoiceMic::Set(World, false, false);
		bTalking = false;
		return;
	}
	// From the first frame on a map, also before its game state has arrived: until the microphone is set here, it
	// does what the engine last did with it
	HCVoicePlayback::ListenToTalkingState(Voice);
	const APlayerController* PC = LocalController();
	ApplySettings();
	// The talk key is one of the player's own keys (settings menu, page "Steuerung")
	if (HCVoiceLocal::KeyWatcher.IsValid() && HCVoiceLocal::KeyWatcher->TalkKey != HCKeys::Get(EHCBinding::Talk))
	{
		HCVoiceLocal::KeyWatcher->TalkKey = HCKeys::Get(EHCBinding::Talk);
		HCVoiceLocal::KeyWatcher->bDown = false;
	}
	RegisterMicrophone();
	UpdateTalkers(PC);
	UpdateMicrophone();
	UpdateTalking(PC);
	UpdateMutes();
	LogTestState();
}

bool UHCVoiceSubsystem::IsHeardNow(const APlayerState* PlayerState) const
{
	return PlayerState && PlayerState->GetUniqueId().IsValid() && HCVoicePlayback::TalkingNow.Contains(PlayerState->GetUniqueId()->ToString());
}

void UHCVoiceSubsystem::ApplySettings()
{
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetWorld()->GetGameInstance());
	if (!GI || GI->GetOptionsRevision() == AppliedRevision)
	{
		return;
	}
	AppliedRevision = GI->GetOptionsRevision();
	const float Gain = FMath::Max(GI->GetOption(TEXT("MicGain"), 1.f), 0.1f);
	if (IConsoleVariable* GainVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicInputGain")))
	{
		GainVariable->Set(Gain, ECVF_SetByGameSetting);
	}
	// Push-to-talk: the key decides. Everything the microphone picks up is sent while it is held; with a threshold a
	// quiet microphone sent nothing at all, or only the loudest bits of every word.
	// Open microphone: how loud a sound must be to count as speech (step 1 = everything, step 10 = only loud voices),
	// on the scale of the level display in the settings menu. That one shows the sound after the gain; the engine
	// compares before the gain.
	float Threshold = 0.f;
	if (GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f)
	{
		Threshold = (0.01f + 0.022f * (GI->GetOption(TEXT("MicThreshold"), 4.f) - 1.f)) / Gain;
	}
#if !UE_BUILD_SHIPPING
	if (HCVoiceLocal::IsSendTest() || HCVoiceLocal::KeyTest().bOn)
	{
		Threshold = 0.f;
	}
#endif
	UVOIPStatics::SetMicThreshold(Threshold);
	// The engine has a second gate behind the threshold, fixed at 0.08: quieter sound is still sent, but as silence
	// (the others saw you talk and heard nothing). The threshold above is the only gate.
	if (IConsoleVariable* NoiseGate = IConsoleManager::Get().FindConsoleVariable(TEXT("voice.MicNoiseGateThreshold")))
	{
		NoiseGate->Set(0.f, ECVF_SetByGameSetting);
	}
}

void UHCVoiceSubsystem::RegisterMicrophone()
{
	UWorld* World = GetWorld();
	const double Now = FPlatformTime::Seconds();
	if (HCVoiceMic::Get(World).bRegistered || Now < HCVoiceLocal::NextRegisterTry)
	{
		return; // usually the session has registered the player already
	}
	HCVoiceLocal::NextRegisterTry = Now + 30.0; // without any microphone: not every frame
	// The engine starts to send with this call; UpdateTalking, later in this frame, sets what the player wants
	const bool bOk = Online::GetVoiceInterface(World)->RegisterLocalTalker(0);
	UE_LOG(LogHC, Log, TEXT("Voice chat: own microphone %s"), bOk ? TEXT("registered") : TEXT("NOT registered (no microphone?)"));
}

void UHCVoiceSubsystem::UpdateTalkers(const APlayerController* PC)
{
	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World->GetGameState();
	if (!PC || !GameState)
	{
		return;
	}
	// Who am I: by id, not by object. After a map change an old copy of the own player state can still be listed,
	// and on a client the new one arrives seconds after the others (until then the local player's id says it;
	// waiting for it left every voice without its volume and its name for that long).
	const FUniqueNetIdRepl OwnId = PC->PlayerState ? PC->PlayerState->GetUniqueId() : FUniqueNetIdRepl();
	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	const FUniqueNetIdRepl LocalId = LocalPlayer ? LocalPlayer->GetPreferredUniqueNetId() : FUniqueNetIdRepl();
	if (!OwnId.IsValid() && !LocalId.IsValid())
	{
		return;
	}
	// One voice per player. While a map change settles, a player can be listed twice: the copy from the previous map
	// and the new one. The new one counts.
	TMap<FString, APlayerState*> Listed;
	for (APlayerState* PS : GameState->PlayerArray)
	{
		if (!IsValid(PS) || PS == PC->PlayerState || !PS->GetUniqueId().IsValid())
		{
			continue;
		}
		const FUniqueNetIdRepl& Id = PS->GetUniqueId();
		if ((OwnId.IsValid() && Id == OwnId) || (LocalId.IsValid() && Id == LocalId))
		{
			continue;
		}
		APlayerState*& Chosen = Listed.FindOrAdd(Id->ToString());
		if (!Chosen || (Chosen->IsFromPreviousLevel() && !PS->IsFromPreviousLevel()))
		{
			Chosen = PS;
		}
	}
	// Talkers of players who left, and of copies that are no longer the one that counts. A voice that is heard right
	// now goes on to the talker that follows (the engine tells a talker about its voice only when the voice starts).
	TMap<FString, TWeakObjectPtr<UAudioComponent>> Heard;
	for (auto It = Talkers.CreateIterator(); It; ++It)
	{
		APlayerState* PS = It.Key();
		UHCVoiceTalker* Talker = It.Value();
		const bool bHasId = PS && PS->GetUniqueId().IsValid();
		if (IsValid(PS) && IsValid(Talker) && bHasId && Listed.FindRef(PS->GetUniqueId()->ToString()) == PS)
		{
			continue;
		}
		if (Talker && bHasId && Talker->GetPlaying())
		{
			Heard.Add(PS->GetUniqueId()->ToString(), Talker->GetPlaying());
		}
		if (IsValid(Talker))
		{
			Talker->DestroyComponent();
		}
		It.RemoveCurrent();
	}
	const IOnlineVoicePtr Voice = Online::GetVoiceInterface(World);
	const UHCGameInstance* GI = Cast<UHCGameInstance>(World->GetGameInstance());
	const float VoiceVolume = GI ? GI->GetOption(TEXT("VoiceVolume"), 1.f) : 1.f;
	// The host's rule: in meetings and with "always" every voice is equally loud
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	const bool bDistance = !GS || HCVoiceRules::UsesDistance(GS->GetVoiceChatRule(), GS->GetMatchPhase());
	for (const TPair<FString, APlayerState*>& Entry : Listed)
	{
		APlayerState* PS = Entry.Value;
		TObjectPtr<UHCVoiceTalker>& Talker = Talkers.FindOrAdd(PS);
		if (!Talker)
		{
			Voice->RegisterRemoteTalker(*PS->GetUniqueId());
			HCVoicePlayback::KnownTalkers.AddUnique(PS->GetUniqueId());
			Talker = NewObject<UHCVoiceTalker>(PS);
			Talker->RegisterComponent();
			if (const TWeakObjectPtr<UAudioComponent>* Playing = Heard.Find(Entry.Key))
			{
				Talker->Adopt(Playing->Get());
			}
			UE_LOG(LogHC, Log, TEXT("Voice chat: can hear %s"), *PS->GetPlayerName());
		}
		// The engine looks a player's talker up by the player's id, and every talker that goes away takes that entry
		// with it, even when it has long belonged to a newer talker of the same player (the copy from the previous
		// map going away after the new one came). Without its entry a voice plays at full volume everywhere.
		if (UVOIPStatics::GetVOIPTalkerForPlayer(PS->GetUniqueId()) != Talker.Get())
		{
			Talker->RegisterWithPlayerState(PS);
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
	// The microphone chosen in the settings menu. Given to the engine once per run of the game and when the choice
	// changes: its capture outlives the maps.
	UWorld* World = GetWorld();
	const UHCGameInstance* GI = Cast<UHCGameInstance>(World->GetGameInstance());
	const HCVoiceMic::FState Mic = HCVoiceMic::Get(World);
	if (!GI || !Mic.bRegistered)
	{
		return;
	}
	const FString Wanted = GI->GetTextOption(TEXT("MicDevice"));
	const double Now = FPlatformTime::Seconds();
	const bool bChanged = !HCVoiceLocal::AppliedMic.IsSet() || HCVoiceLocal::AppliedMic.GetValue() != Wanted;
	// A microphone that was unplugged or switched off is tried again every few seconds: it may be back
	const bool bRetry = Mic.bFailed && Now >= HCVoiceLocal::NextMicRetry;
	if (!bChanged && !bRetry)
	{
		return;
	}
	HCVoiceLocal::AppliedMic = Wanted;
	HCVoiceLocal::NextMicRetry = Now + 5.0;
	HCAudioDevices::SetVoiceMicrophone(World, Wanted);
}

void UHCVoiceSubsystem::UpdateTalking(const APlayerController* PC)
{
	UWorld* World = GetWorld();
	const UHCGameInstance* GI = Cast<UHCGameInstance>(World->GetGameInstance());
	// The host's rule: with "only in meetings" nobody hears the living outside a meeting, so nothing is sent
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	const AHCPlayerState* Own = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	bSendAllowed = !GS || !Own || HCVoiceRules::CanSend(GS->GetVoiceChatRule(), GS->GetMatchPhase(), Own->IsAlive());
	// Listening: for as long as the voice chat is on (and the engine's voice chat runs: without a session it stands
	// still and nobody would take the sound from the microphone). Sending: by the key, or all the time with the open
	// microphone (the engine then only passes on what is louder than the threshold).
	const IOnlineSessionPtr Sessions = Online::GetSessionInterface(World);
	bool bListen = GI && GI->GetOption(TEXT("VoiceEnabled"), 1.f) > 0.5f && Sessions.IsValid() && Sessions->GetNumSessions() > 0;
	bool bSend = false;
	const double Now = FPlatformTime::Seconds();
	if (bListen && GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f)
	{
		bSend = true;
	}
	else if (bListen)
	{
		if (HCVoiceLocal::IsTalkKeyHeld())
		{
			HCVoiceLocal::SendUntil = Now + HCVoiceLocal::ReleaseTail;
		}
		bSend = Now < HCVoiceLocal::SendUntil;
	}
#if !UE_BUILD_SHIPPING
	if (HCVoiceLocal::IsSendTest())
	{
		bListen = bSend = true;
	}
#endif
	if (!bSendAllowed)
	{
		bSend = false;
		HCVoiceLocal::SendUntil = 0.0;
	}
	// Every frame, not only on a change: the engine switches the microphone by itself (HCVoiceMic)
	HCVoiceMic::Set(World, bListen, bSend);
	bTalking = bListen && bSend;
}

void UHCVoiceSubsystem::UpdateMutes()
{
	UWorld* World = GetWorld();
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	if (!GS || World->GetTimeSeconds() < NextMuteCheck)
	{
		return;
	}
	NextMuteCheck = World->GetTimeSeconds() + 0.5;
	const IOnlineVoicePtr Voice = Online::GetVoiceInterface(World);
	if (World->GetNetMode() == NM_Client)
	{
		// What reaches this machine is the host's decision: it does not pass on a voice this player must not hear.
		// The engine also keeps a list of muted players on each machine, and that one gets stuck: an "unmute" that
		// comes while its player is not known here is dropped, and that player is never heard again. Kept empty.
		for (const TPair<TObjectPtr<APlayerState>, TObjectPtr<UHCVoiceTalker>>& Entry : Talkers)
		{
			if (IsValid(Entry.Key) && Entry.Key->GetUniqueId().IsValid() && Voice->IsMuted(0, *Entry.Key->GetUniqueId()))
			{
				Voice->UnmuteRemoteTalker(0, *Entry.Key->GetUniqueId(), false);
				UE_LOG(LogHC, Verbose, TEXT("Voice chat: own mute of %s lifted (the host decides)"), *Entry.Key->GetPlayerName());
			}
		}
		return;
	}
	// The host cuts every line the voice chat rule does not allow (HCVoiceRules::CanHear): from the dead to the living
	// while a round runs, and with "only in meetings" from the living outside a meeting. Compared with what each
	// controller really has, every time: controllers are created anew at a map change and start with an empty list.
	const EHCMatchPhase Phase = GS->GetMatchPhase();
	const EHCVoiceChatRule Rule = GS->GetVoiceChatRule();
	int32 Cut = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Listener = It->Get();
		const AHCPlayerState* ListenerState = Listener ? Listener->GetPlayerState<AHCPlayerState>() : nullptr;
		if (!ListenerState)
		{
			continue;
		}
		const bool bOwnEars = Listener->IsLocalController();
		for (APlayerState* Other : GS->PlayerArray)
		{
			const AHCPlayerState* Talker = Cast<AHCPlayerState>(Other);
			if (!Talker || Talker == ListenerState || !Talker->GetUniqueId().IsValid() || Talker->GetUniqueId() == ListenerState->GetUniqueId())
			{
				continue;
			}
			const FUniqueNetIdRepl& Id = Talker->GetUniqueId();
			const bool bMute = !HCVoiceRules::CanHear(Rule, Phase, Talker->IsAlive(), ListenerState->IsAlive());
			Cut += bMute ? 1 : 0;
			const bool bIsMuted = Listener->IsPlayerMuted(*Id);
			if (bMute && !bIsMuted)
			{
				Listener->GameplayMutePlayer(Id);
				UE_LOG(LogHC, Verbose, TEXT("Voice chat: %s no longer hears %s"), *ListenerState->GetPlayerName(), *Talker->GetPlayerName());
			}
			else if (!bMute && bIsMuted)
			{
				Listener->GameplayUnmutePlayer(Id);
				if (bOwnEars && Listener->IsPlayerMuted(*Id))
				{
					// The host's own controller is server and client in one, and keeps the reasons of both sides in
					// one list. The engine then finds "still muted for another reason" and never tells the client
					// side: the host heard nobody in a meeting ("only in meetings"), and no dead player after the
					// round, until some later map change happened to clear it.
					Listener->ClientUnmutePlayer(Id);
				}
			}
			if (bOwnEars && Voice->IsMuted(0, *Id) != bMute)
			{
				// What the host itself hears is decided by the list of its own machine alone
				if (bMute)
				{
					Voice->MuteRemoteTalker(0, *Id, false);
				}
				else
				{
					Voice->UnmuteRemoteTalker(0, *Id, false);
				}
			}
		}
	}
	if (Cut != CutLines)
	{
		CutLines = Cut;
		UE_LOG(LogHC, Log, TEXT("Voice chat: %d line(s) cut (rule %s, phase %s)"), Cut, *UEnum::GetValueAsString(Rule), HCIdentity::PhaseName(Phase));
	}
}

void UHCVoiceSubsystem::LogTestState()
{
#if !UE_BUILD_SHIPPING
	if (!HCVoiceLocal::IsSendTest() && !HCVoiceLocal::KeyTest().bOn)
	{
		return;
	}
	UWorld* World = GetWorld();
	TArray<FString> Heard;
	for (const TPair<TObjectPtr<APlayerState>, TObjectPtr<UHCVoiceTalker>>& Entry : Talkers)
	{
		if (IsHeardNow(Entry.Key))
		{
			Heard.Add(Entry.Key->GetPlayerName());
		}
	}
	Heard.Sort();
	const FString HeardText = Heard.Num() > 0 ? FString::Join(Heard, TEXT(", ")) : FString(TEXT("nobody"));
	const HCVoiceMic::FState Mic = HCVoiceMic::Get(World);
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	const int32 KeyDown = HCVoiceLocal::KeyTest().bOn && HCVoiceLocal::IsTestKeyDown() ? 1 : 0;
	const FString State = FString::Printf(TEXT("key %d, sending %d, microphone delivers %d, hearing: %s"), KeyDown, Mic.bSending ? 1 : 0, Mic.bTalking ? 1 : 0, *HeardText);
	if (State != TestState)
	{
		TestState = State;
		UE_LOG(LogHC, Display, TEXT("[HCVoice] change (%s): %s"), GS ? HCIdentity::PhaseName(GS->GetMatchPhase()) : TEXT("?"), *State);
	}
	if (World->GetTimeSeconds() >= NextTestLog)
	{
		NextTestLog = World->GetTimeSeconds() + 3.0;
		int32 States = 0;
		int32 StatesWithId = 0;
		if (const AGameStateBase* GameState = World->GetGameState())
		{
			for (const APlayerState* PS : GameState->PlayerArray)
			{
				++States;
				StatesWithId += PS && PS->GetUniqueId().IsValid() ? 1 : 0;
			}
		}
		const APlayerController* PC = LocalController();
		UE_LOG(LogHC, Display, TEXT("[HCVoice] sending %d (microphone picks up %d, registered %d), %d other players known (%d player states, %d with an id, own state %d), hearing: %s"),
			bTalking ? 1 : 0, Mic.bTalking ? 1 : 0, Mic.bRegistered ? 1 : 0, Talkers.Num(), States, StatesWithId, PC && PC->PlayerState ? 1 : 0, *HeardText);
	}
#endif
}

void UHCVoiceSubsystem::DrawHud(AHUD* HUD, UCanvas* Canvas)
{
	UWorld* World = GetWorld();
	const UHCGameInstance* GI = World ? Cast<UHCGameInstance>(World->GetGameInstance()) : nullptr;
	if (!HUD || !Canvas || HUD->GetWorld() != World || !GI || !World->GetGameState() || World->GetNetMode() == NM_Standalone || !GEngine || GI->GetOption(TEXT("VoiceEnabled"), 1.f) < 0.5f)
	{
		return;
	}
	if (!Online::GetVoiceInterface(World).IsValid())
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float X = 30.f;
	float Y = Canvas->ClipY - 96.f;
	// Your own microphone. Push-to-talk: it sends while the key is held. Open: while it really picks you up (asked
	// from the engine's talker state; IOnlineVoice::IsLocalPlayerTalking is always false for the first player).
	const bool bOpen = GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f;
	const bool bHeard = bTalking && (!bOpen || HCVoiceMic::Get(World).bTalking);
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
