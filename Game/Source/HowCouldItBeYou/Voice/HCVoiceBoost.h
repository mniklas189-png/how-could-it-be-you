#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundEffectSource.h"
#include <atomic>
#include "HCVoiceBoost.generated.h"

class USoundEffectSourcePresetChain;

/**
 * How loud the other players' voices are on this machine, and the sound processing that lets them get louder than the
 * engine allows.
 *
 * The engine caps every sound at four times its own level (MAX_VOLUME, applied to a source's volume), and voices
 * already start at 1.5 of it. So "louder" is done here as a gain on the voice's samples, behind the volume, with a peak
 * limiter after it: a quiet voice gets louder, a loud one does not clip (it would crackle at the speakers).
 *
 * Two settings on the voice page multiply: the volume of all voices (option VoiceVolume) and one per player (by name,
 * GameUserSettings.ini [HowCouldItBeYou.VoicePlayers]). 0 % mutes.
 */
namespace HCVoiceVolumes
{
	/** Highest setting of each of the two volumes (4 = 400 %). */
	constexpr float MaxVolume = 4.f;
	/** Highest gain the two give together (the limiter would only squeeze the voice harder beyond that). */
	constexpr float MaxGain = 8.f;

	/** One step up or down: 10 % below 100 %, 25 % from 100 % up (so that 400 % is not 40 clicks away). */
	HOWCOULDITBEYOU_API float Step(float Value, int32 Delta);

	/** Volume of all voices, 0..MaxVolume (default 1). */
	HOWCOULDITBEYOU_API float OfAll(const UObject* WorldContext);
	/** Volume of this one player (by player name), 0..MaxVolume (default 1). */
	HOWCOULDITBEYOU_API float OfPlayer(const FString& PlayerName);
	HOWCOULDITBEYOU_API void SetPlayer(const FString& PlayerName, float Volume);
	/** Goes up with every SetPlayer (the voice subsystem looks at it). */
	HOWCOULDITBEYOU_API int32 PlayerRevision();

	/** The gain on a voice: both volumes, capped at MaxGain. */
	HOWCOULDITBEYOU_API float GainOf(float AllVolume, float PlayerVolume);
}

/** The sound processing itself, apart from the engine so that it can be tested. */
namespace HCVoiceBoostDsp
{
	/** Highest level a voice gets after the limiter (just under full scale). */
	constexpr float Ceiling = 0.9f;

	struct FState
	{
		/** The gain right now; it glides towards the wanted one (no clicks when a setting changes). */
		float Gain = 1.f;
		/** Peak level the limiter follows: up at once, down over about a tenth of a second. */
		float Envelope = 0.f;
	};

	/** Interleaved samples (NumSamples counts all channels), In and Out may be the same buffer. */
	HOWCOULDITBEYOU_API void Process(FState& State, const float* In, float* Out, int32 NumSamples, int32 NumChannels, float SampleRate, float TargetGain);

	/** What the playbacks did, for the test logs (-HCVoiceTest): counted on the audio thread. */
	struct FStats
	{
		/** Playbacks that went through the gain stage since the game started. */
		std::atomic<int32> Playbacks{ 0 };
		/** Highest level before and after the gain stage since the last TakePeaks. */
		std::atomic<float> PeakIn{ 0.f };
		std::atomic<float> PeakOut{ 0.f };
	};
	HOWCOULDITBEYOU_API FStats& Stats();
}

/** One player's voice gain. Each player has a preset of their own; all their playbacks follow it. */
UCLASS()
class HOWCOULDITBEYOU_API UHCVoiceBoostPreset : public USoundEffectSourcePreset
{
	GENERATED_BODY()

public:
	UHCVoiceBoostPreset();

	/** Read on the audio thread every block. */
	void SetGain(float InGain);
	float GetGain() const { return Gain.IsValid() ? Gain->load() : 1.f; }

	/** A chain with only this preset in it, for the player's UVOIPTalker (FVoiceSettings::SourceEffectChain). */
	USoundEffectSourcePresetChain* GetChain();

	virtual FText GetAssetActionName() const override { return FText::FromString(TEXT("HCVoiceBoost")); }
	virtual UClass* GetSupportedClass() const override { return UHCVoiceBoostPreset::StaticClass(); }
	virtual USoundEffectPreset* CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const override;
	virtual FSoundEffectBase* CreateNewEffect() const override;
	virtual void Init() override {}
	virtual bool CanFilter() const override { return false; }

private:
	/** Shared with the playbacks (their effects outlive a change of the setting, not the preset). Never null. */
	TSharedPtr<std::atomic<float>, ESPMode::ThreadSafe> Gain;

	UPROPERTY(Transient)
	TObjectPtr<USoundEffectSourcePresetChain> Chain;
};
