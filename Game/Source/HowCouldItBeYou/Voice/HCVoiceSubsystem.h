#pragma once

#include "CoreMinimal.h"
#include "Net/VoiceConfig.h"
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
 * - Proximity: a voice gets quieter with the distance to the figure of its player (the volume is set here; the
 *   engine's own 3D voice playback trips an engine check when it attaches its audio component). In a meeting
 *   everybody sits within a few metres, so everybody hears everybody.
 * - The dead are only heard by the dead; they still hear the living. The host decides who hears whom.
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
	/** Server: listener and talker (player ids) whose voice connection is cut. */
	TSet<uint64> Muted;
	bool bMutesReset = false;
	double NextMuteCheck = 0.0;
	double NextTestLog = 0.0;
};
