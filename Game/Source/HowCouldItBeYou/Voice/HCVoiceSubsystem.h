#pragma once

#include "CoreMinimal.h"
#include "Net/VoiceConfig.h"
#include "Online/CoreOnline.h"
#include "Subsystems/WorldSubsystem.h"
#include "HCVoiceSubsystem.generated.h"

class AHUD;
class APlayerController;
class APlayerState;
class UCanvas;

/** One per other player on this machine: where that voice comes from and how loud it is. */
UCLASS()
class HOWCOULDITBEYOU_API UHCVoiceTalker : public UVOIPTalker
{
	GENERATED_BODY()

public:
	virtual void OnTalkingBegin(UAudioComponent* AudioComponent) override;
	virtual void OnTalkingEnd() override;

	/** 0..1: how near this player's figure is to the listener (the volume settings are the gain stage's, HCVoiceBoost). */
	void SetLoudness(float InLoudness);

	/** The playback of this voice while it is heard. A talker that replaces another one of the same player takes it over. */
	UAudioComponent* GetPlaying() const { return Playing.Get(); }
	void Adopt(UAudioComponent* AudioComponent);

private:
	TWeakObjectPtr<UAudioComponent> Playing;
	float Loudness = 1.f;
};

/**
 * Voice chat on top of the engine's own voice system (capture, Opus, transport over the game connection).
 *
 * - Push-to-talk or open microphone, chosen in the settings menu; the key (HCKeys, EHCBinding::Talk) works everywhere,
 *   also while a menu or the meeting screen has the keyboard (but not while you type into a text field).
 * - The microphone is the game's alone (HCVoiceMic in Voice/HCAudioDevices.h): it listens while the voice chat is on
 *   and you are in a lobby or a round, and it sends only while the talk key is held (and a moment longer, so the last
 *   word is not cut), or with the open microphone while you are louder than the threshold. The engine opens and closes
 *   it by itself at every map change; that is undone every frame.
 * - Push-to-talk sends everything the microphone picks up: the threshold of the settings menu is for the open
 *   microphone only, and is measured like the level display there (after the gain). The engine's second, fixed gate
 *   is switched off; it muted quiet microphones whatever the threshold said.
 * - When who hears whom is the lobby option of the host (EHCVoiceChatRule, rules in Voice/HCVoiceRules.h): only in
 *   meetings, near + meetings, or always. The host's mute lists enforce it; the own microphone stays closed while
 *   nobody could hear it.
 * - Proximity: a voice gets quieter with the distance to the figure of its player (the volume is set here; the
 *   engine's own 3D voice playback trips an engine check when it attaches its audio component). In a meeting and
 *   with "always" every voice is equally loud.
 * - Volume: all voices and each player on their own (settings, page "Sprach-Chat"), up to 400 % each, as a gain on
 *   the voice with a limiter behind it (Voice/HCVoiceBoost.h): louder than the engine's own cap, without crackling.
 * - The dead are only heard by the dead; they still hear the living.
 * - Map changes: the engine's voice playback of a world is released before that world goes away (seamless travel
 *   would otherwise leave it registered to a dead world and crash; HCVoicePlayback in the .cpp).
 * - The microphone chosen in the settings menu (MicDevice) is given to the engine's capture.
 * - A small display on the left edge shows your microphone and who is talking.
 *
 * What belongs to the player and not to a map (talk key, microphone, the short hold after the key) is kept for the
 * whole game in the .cpp (HCVoiceLocal): there is one of these subsystems per map.
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

	/** Does the voice of this player arrive here right now? (HCVoicePlayback::TalkingNow in the .cpp) */
	static bool IsVoiceArriving(const APlayerState* PlayerState);

private:
	/**
	 * The controller of the player at this machine. Not the world's first controller: on the host that is another
	 * player's for as long as they are still loading the new map (the host's own is created anew and goes to the end).
	 */
	APlayerController* LocalController() const;
	void ApplySettings();
	void RegisterMicrophone();
	void UpdateTalkers(const APlayerController* PC);
	void UpdateMicrophone();
	void UpdateTalking(const APlayerController* PC);
	void UpdateMutes();
	void LogTestState();
	/** Development (-HCVoicePlayersShot): a picture of the players' volumes on the voice page of the settings. */
	void TickVolumeShot();
	void DrawHud(AHUD* HUD, UCanvas* Canvas);

	/** One talker per other player (a player listed twice while a map change settles gets one, not two). */
	UPROPERTY(Transient)
	TMap<TObjectPtr<APlayerState>, TObjectPtr<UHCVoiceTalker>> Talkers;

	FDelegateHandle HudHandle;
	int32 AppliedRevision = -1;
	/** The microphone listens and what it picks up is sent. */
	bool bTalking = false;
	/** The voice chat rule lets somebody hear this player now. */
	bool bSendAllowed = true;
	/** Server: how many voice connections the rule cuts right now (to log a change once). */
	int32 CutLines = 0;
	double NextMuteCheck = 0.0;
	double NextTestLog = 0.0;
	/** Development (-HCVoiceTest, -HCVoiceKey): what the last logged change said. */
	FString TestState;
};
