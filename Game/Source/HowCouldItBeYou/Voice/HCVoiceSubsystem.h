#pragma once

#include "CoreMinimal.h"
#include "Net/VoiceConfig.h"
#include "Online/CoreOnline.h"
#include "Subsystems/WorldSubsystem.h"
#include "HCVoiceSubsystem.generated.h"

class AHUD;
class APlayerState;
class FHCVoiceKeyWatcher;
class UCanvas;

/** One per other player on this machine: where that voice comes from and how loud it is. */
UCLASS()
class HOWCOULDITBEYOU_API UHCVoiceTalker : public UVOIPTalker
{
	GENERATED_BODY()

public:
	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;

	/** 0..2: distance to the listener times the listener's own volume setting. */
	void SetLoudness(float InLoudness);

private:
	TWeakObjectPtr<UAudioComponent> Playing;
	float Loudness = 1.f;
};

/**
 * Voice chat on top of the engine's own voice system (capture, Opus, transport over the game connection).
 *
 * - Push-to-talk or open microphone, chosen in the settings menu; the key (HCKeys, EHCBinding::Talk) works everywhere,
 *   also while a menu or the meeting screen has the keyboard (but not while you type into a text field).
 * - When who hears whom is the lobby option of the host (EHCVoiceChatRule, rules in Voice/HCVoiceRules.h): only in
 *   meetings, near + meetings, or always. The host's mute lists enforce it; the own microphone stays closed while
 *   nobody could hear it.
 * - Proximity: a voice gets quieter with the distance to the figure of its player (the volume is set here; the
 *   engine's own 3D voice playback trips an engine check when it attaches its audio component). In a meeting and
 *   with "always" every voice is equally loud.
 * - The dead are only heard by the dead; they still hear the living.
 * - Map changes: the engine's voice playback of a world is released before that world goes away (seamless travel
 *   would otherwise leave it registered to a dead world and crash; HCVoicePlayback in the .cpp).
 * - The microphone chosen in the settings menu (MicDevice) is given to the engine's capture.
 * - A small display on the left edge shows your microphone and who is talking.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCVoiceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UHCVoiceSubsystem, STATGROUP_Tickables); }

	/** Is this machine sending the own voice right now? */
	bool IsTalking() const { return bTalking; }

private:
	void ApplySettings();
	void UpdateTalkers();
	void UpdateMicrophone();
	void UpdateTalking();
	void UpdateMutes();
	void DrawHud(AHUD* HUD, UCanvas* Canvas);
	/** Does the voice of this player arrive right now? (HCVoicePlayback::TalkingNow in the .cpp) */
	bool IsHeardNow(const APlayerState* PlayerState) const;

	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, TObjectPtr<UHCVoiceTalker>> Talkers;

	TSharedPtr<FHCVoiceKeyWatcher> KeyWatcher;
	FDelegateHandle HudHandle;
	int32 AppliedRevision = -1;
	/** Settings changed: check whether the microphone did. */
	bool bMicDirty = true;
	/** Microphone the engine's capture listens to (empty = Windows default, where it starts). */
	FString AppliedMic;
	bool bLocalRegistered = false;
	bool bTalking = false;
	/** The voice chat rule lets somebody hear this player now. */
	bool bSendAllowed = true;
	/** Server: listener and talker (player ids) whose voice connection is cut. */
	TSet<uint64> Muted;
	bool bMutesReset = false;
	double NextMuteCheck = 0.0;
	double NextTestLog = 0.0;
};
