#include "Voice/HCVoiceBoost.h"

#include "Core/HCGameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundEffectSource.h"

namespace HCVoiceVolumes
{
	namespace
	{
		const TCHAR* PlayerSection = TEXT("HowCouldItBeYou.VoicePlayers");
		const TCHAR* PlayerKey = TEXT("Player");

		/** Every player with a volume of their own; loaded from GameUserSettings.ini on first use. */
		TMap<FString, float>& Players()
		{
			static TMap<FString, float> Loaded = []()
			{
				TMap<FString, float> Result;
				TArray<FString> Lines;
				if (GConfig)
				{
					GConfig->GetArray(PlayerSection, PlayerKey, Lines, GGameUserSettingsIni);
				}
				// "1.50 Name": the number first, the name may hold anything (spaces too)
				for (const FString& Line : Lines)
				{
					FString Number, Name;
					if (Line.Split(TEXT(" "), &Number, &Name) && !Name.IsEmpty())
					{
						Result.Add(Name, FMath::Clamp(FCString::Atof(*Number), 0.f, MaxVolume));
					}
				}
				return Result;
			}();
			return Loaded;
		}

		int32 Revision = 0;
	}

	float Step(float Value, int32 Delta)
	{
		// In hundredths, so that 0.1 + 0.1 + ... lands on the steps exactly
		int32 Percent = FMath::RoundToInt32(FMath::Clamp(Value, 0.f, MaxVolume) * 100.f);
		if (Delta > 0)
		{
			Percent = Percent < 100 ? (Percent / 10 + 1) * 10 : (Percent / 25 + 1) * 25;
		}
		else if (Delta < 0)
		{
			Percent = Percent <= 100 ? ((Percent + 9) / 10 - 1) * 10 : ((Percent + 24) / 25 - 1) * 25;
		}
		return FMath::Clamp(Percent / 100.f, 0.f, MaxVolume);
	}

	float OfAll(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const UHCGameInstance* GI = World ? Cast<UHCGameInstance>(World->GetGameInstance()) : nullptr;
		return GI ? FMath::Clamp(GI->GetOption(TEXT("VoiceVolume"), 1.f), 0.f, MaxVolume) : 1.f;
	}

	float OfPlayer(const FString& PlayerName)
	{
		const float* Volume = Players().Find(PlayerName);
		return Volume ? *Volume : 1.f;
	}

	void SetPlayer(const FString& PlayerName, float Volume)
	{
		if (PlayerName.IsEmpty())
		{
			return;
		}
		Volume = FMath::Clamp(Volume, 0.f, MaxVolume);
		TMap<FString, float>& All = Players();
		if (FMath::IsNearlyEqual(Volume, 1.f))
		{
			All.Remove(PlayerName); // the normal volume needs no line
		}
		else
		{
			All.Add(PlayerName, Volume);
		}
		++Revision;
		if (GConfig)
		{
			TArray<FString> Lines;
			for (const TPair<FString, float>& Entry : All)
			{
				Lines.Add(FString::Printf(TEXT("%.2f %s"), Entry.Value, *Entry.Key));
			}
			Lines.Sort();
			GConfig->SetArray(PlayerSection, PlayerKey, Lines, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}

	int32 PlayerRevision()
	{
		return Revision;
	}

	float GainOf(float AllVolume, float PlayerVolume)
	{
		return FMath::Clamp(AllVolume * PlayerVolume, 0.f, MaxGain);
	}
}

namespace HCVoiceBoostDsp
{
	FStats& Stats()
	{
		static FStats Counted;
		return Counted;
	}

	namespace
	{
		void RaisePeak(std::atomic<float>& Peak, float Value)
		{
			float Seen = Peak.load(std::memory_order_relaxed);
			while (Value > Seen && !Peak.compare_exchange_weak(Seen, Value, std::memory_order_relaxed))
			{
			}
		}

		float PeakOf(const float* Samples, int32 NumSamples)
		{
			float Peak = 0.f;
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				Peak = FMath::Max(Peak, FMath::Abs(Samples[Index]));
			}
			return Peak;
		}
	}

	void Process(FState& State, const float* In, float* Out, int32 NumSamples, int32 NumChannels, float SampleRate, float TargetGain)
	{
		NumChannels = FMath::Max(NumChannels, 1);
		SampleRate = FMath::Max(SampleRate, 8000.f);
		TargetGain = FMath::Clamp(TargetGain, 0.f, HCVoiceVolumes::MaxGain);
		// The gain glides over about 30 ms; the limiter lets go over about 120 ms
		const float Glide = 1.f - FMath::Exp(-1.f / (0.03f * SampleRate));
		const float Release = FMath::Exp(-1.f / (0.12f * SampleRate));
		const int32 NumFrames = NumSamples / NumChannels;
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			State.Gain += (TargetGain - State.Gain) * Glide;
			const int32 First = Frame * NumChannels;
			float Peak = 0.f;
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				Peak = FMath::Max(Peak, FMath::Abs(In[First + Channel] * State.Gain));
			}
			// Up at once: the envelope is never below the frame's own peak, so nothing gets past the ceiling
			State.Envelope = Peak > State.Envelope ? Peak : Peak + (State.Envelope - Peak) * Release;
			const float Limit = State.Envelope > Ceiling ? Ceiling / State.Envelope : 1.f;
			const float Factor = State.Gain * Limit;
			for (int32 Channel = 0; Channel < NumChannels; ++Channel)
			{
				Out[First + Channel] = In[First + Channel] * Factor;
			}
		}
		// A last partial frame (never with whole frames, but the buffer is the engine's)
		for (int32 Index = NumFrames * NumChannels; Index < NumSamples; ++Index)
		{
			Out[Index] = FMath::Clamp(In[Index] * State.Gain, -Ceiling, Ceiling);
		}
		if (State.Envelope < 1.0e-6f)
		{
			State.Envelope = 0.f; // no denormals in the silence between words
		}
	}
}

/** One playback of a voice: the gain of its player's preset, then the limiter. Runs on the audio render thread. */
class FHCVoiceBoost : public FSoundEffectSource
{
public:
	explicit FHCVoiceBoost(const TSharedPtr<std::atomic<float>, ESPMode::ThreadSafe>& InGain)
		: Gain(InGain)
	{
	}

	virtual void ProcessAudio(const FSoundEffectSourceInputData& InData, float* OutAudioBufferData) override
	{
		using namespace HCVoiceBoostDsp;
		RaisePeak(Stats().PeakIn, PeakOf(InData.InputSourceEffectBufferPtr, InData.NumSamples));
		Process(State, InData.InputSourceEffectBufferPtr, OutAudioBufferData, InData.NumSamples, NumChannels, SampleRate, Gain->load());
		RaisePeak(Stats().PeakOut, PeakOf(OutAudioBufferData, InData.NumSamples));
	}

private:
	virtual void Init(const FSoundEffectSourceInitData& InInitData) override
	{
		SampleRate = InInitData.SampleRate;
		NumChannels = FMath::Max(InInitData.NumSourceChannels, 1);
		State = HCVoiceBoostDsp::FState();
		State.Gain = Gain->load(); // a new playback starts at the gain of the moment
		++HCVoiceBoostDsp::Stats().Playbacks;
	}

	TSharedPtr<std::atomic<float>, ESPMode::ThreadSafe> Gain;
	HCVoiceBoostDsp::FState State;
	float SampleRate = 48000.f;
	int32 NumChannels = 1;
};

UHCVoiceBoostPreset::UHCVoiceBoostPreset()
	: Gain(MakeShared<std::atomic<float>, ESPMode::ThreadSafe>(1.f))
{
}

void UHCVoiceBoostPreset::SetGain(float InGain)
{
	Gain->store(FMath::Clamp(InGain, 0.f, HCVoiceVolumes::MaxGain));
}

USoundEffectSourcePresetChain* UHCVoiceBoostPreset::GetChain()
{
	if (!Chain)
	{
		Chain = NewObject<USoundEffectSourcePresetChain>(this);
		FSourceEffectChainEntry Entry;
		Entry.Preset = this;
		Chain->Chain.Add(Entry);
	}
	return Chain;
}

USoundEffectPreset* UHCVoiceBoostPreset::CreateNewPreset(UObject* InParent, FName Name, EObjectFlags Flags) const
{
	return NewObject<UHCVoiceBoostPreset>(InParent, Name, Flags);
}

FSoundEffectBase* UHCVoiceBoostPreset::CreateNewEffect() const
{
	return new FHCVoiceBoost(Gain);
}
