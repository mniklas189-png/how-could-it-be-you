#include "Audio/HCProceduralAudio.h"

#include "Components/AudioComponent.h"
#include "Core/HCLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"

namespace HCProceduralAudio
{
	constexpr int32 SampleRate = 22050;

	static float Saw(double Phase)
	{
		return static_cast<float>(2.0 * (Phase - FMath::FloorToDouble(Phase + 0.5)));
	}

	TArray<int16> Generate(ESound Sound, float& OutSeconds)
	{
		OutSeconds = Sound == ESound::Stinger ? 1.4f : Sound == ESound::Thump ? 0.9f : Sound == ESound::Shapeshift ? 1.5f : Sound == ESound::Alarm ? 0.9f : 1.6f;
		const int32 Count = FMath::RoundToInt(OutSeconds * SampleRate);
		TArray<int16> Samples;
		Samples.SetNumUninitialized(Count);
		FRandomStream Random(Sound == ESound::Stinger ? 1337 : 4242);
		double PhaseA = 0.0, PhaseB = 0.0, PhaseC = 0.0;
		float Lowpass = 0.f;
		for (int32 i = 0; i < Count; ++i)
		{
			const float T = static_cast<float>(i) / SampleRate;
			const float U = T / OutSeconds;
			float Value = 0.f;
			switch (Sound)
			{
			case ESound::Stinger:
			{
				// Hard attack: noise burst plus a dissonant cluster that bends upwards
				const float Attack = FMath::Clamp(T / 0.01f, 0.f, 1.f);
				const float Decay = FMath::Exp(-3.2f * T);
				const float Bend = 1.f + 0.35f * U;
				PhaseA += 185.0 * Bend / SampleRate;
				PhaseB += 196.5 * Bend / SampleRate;
				PhaseC += 277.2 * Bend / SampleRate;
				const float Noise = Random.FRandRange(-1.f, 1.f) * FMath::Exp(-9.f * T);
				Value = Attack * Decay * (0.34f * Saw(PhaseA) + 0.3f * Saw(PhaseB) + 0.3f * Saw(PhaseC)) + 0.55f * Noise;
				break;
			}
			case ESound::Thump:
			{
				// Dull, low: a falling sine
				PhaseA += (70.0 - 30.0 * U) / SampleRate;
				Value = FMath::Sin(2.f * PI * static_cast<float>(PhaseA)) * FMath::Exp(-4.5f * T) * FMath::Clamp(T / 0.02f, 0.f, 1.f);
				break;
			}
			case ESound::Shapeshift:
			{
				// Wet crackling: bursts of filtered noise over a low gurgle
				const float Burst = FMath::Pow(FMath::Abs(FMath::Sin(2.f * PI * (7.f + 9.f * U) * T)), 6.f);
				Lowpass += 0.45f * (Random.FRandRange(-1.f, 1.f) - Lowpass);
				PhaseA += (70.f + 40.f * FMath::Sin(2.f * PI * 3.f * T)) / SampleRate;
				const float Envelope = FMath::Clamp(T / 0.03f, 0.f, 1.f) * FMath::Clamp((OutSeconds - T) / 0.3f, 0.f, 1.f);
				Value = Envelope * (0.8f * Burst * Lowpass + 0.3f * Saw(PhaseA));
				break;
			}
			case ESound::Alarm:
			{
				// Two short beeps, the second one lower
				const float Gate = FMath::Fmod(T, 0.45f) < 0.28f ? 1.f : 0.f;
				PhaseA += (T < 0.45f ? 880.f : 660.f) / SampleRate;
				Value = 0.45f * Gate * FMath::Clamp((OutSeconds - T) / 0.05f, 0.f, 1.f) * static_cast<float>(FMath::Sin(2.0 * PI * PhaseA));
				break;
			}
			case ESound::Scream:
			{
				// Rises, wobbles and breaks off; some breath noise through a simple low-pass
				const float Pitch = 520.f + 380.f * FMath::Sin(PI * FMath::Min(1.f, U * 1.6f)) + 25.f * FMath::Sin(2.f * PI * 11.f * T);
				PhaseA += Pitch / SampleRate;
				PhaseB += Pitch * 2.02 / SampleRate;
				Lowpass += 0.25f * (Random.FRandRange(-1.f, 1.f) - Lowpass);
				const float Envelope = FMath::Clamp(T / 0.04f, 0.f, 1.f) * FMath::Clamp((OutSeconds - T) / 0.5f, 0.f, 1.f);
				Value = Envelope * (0.5f * Saw(PhaseA) + 0.22f * Saw(PhaseB) + 0.35f * Lowpass);
				break;
			}
			}
			Samples[i] = static_cast<int16>(FMath::Clamp(Value, -1.f, 1.f) * 30000.f);
		}
		return Samples;
	}

	static USoundWaveProcedural* MakeWave(ESound Sound, float& OutSeconds)
	{
		const TArray<int16> Samples = Generate(Sound, OutSeconds);
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>();
		Wave->SetSampleRate(SampleRate);
		Wave->NumChannels = 1;
		Wave->Duration = OutSeconds;
		Wave->SoundGroup = SOUNDGROUP_Default;
		Wave->bLooping = false;
		Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
		return Wave;
	}

	static void StopLater(UWorld* World, UAudioComponent* Component, float Seconds)
	{
		// A procedural wave has no natural end: stop the voice ourselves once the samples are played
		if (!World || !Component)
		{
			return;
		}
		TWeakObjectPtr<UAudioComponent> Weak(Component);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak]()
		{
			if (UAudioComponent* Still = Weak.Get())
			{
				Still->Stop();
			}
		}), Seconds + 0.1f, false);
	}

	UAudioComponent* Play2D(UObject* WorldContext, ESound Sound, float Volume)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer || Volume <= 0.f || !World->GetAudioDeviceRaw())
		{
			return nullptr;
		}
		float Seconds = 0.f;
		USoundWaveProcedural* Wave = MakeWave(Sound, Seconds);
		UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, Wave, Volume);
		UE_LOG(LogHC, Log, TEXT("Placeholder sound %d (2D, %.1f s) %s"), static_cast<int32>(Sound), Seconds, Component ? TEXT("started") : TEXT("NOT started"));
		StopLater(World, Component, Seconds);
		return Component;
	}

	UAudioComponent* PlayAtLocation(UObject* WorldContext, ESound Sound, const FVector& Location, float Volume, float Range)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		if (!World || World->GetNetMode() == NM_DedicatedServer || Volume <= 0.f || !World->GetAudioDeviceRaw())
		{
			return nullptr;
		}
		float Seconds = 0.f;
		USoundWaveProcedural* Wave = MakeWave(Sound, Seconds);
		USoundAttenuation* Attenuation = NewObject<USoundAttenuation>();
		Attenuation->Attenuation.bAttenuate = true;
		Attenuation->Attenuation.bSpatialize = true;
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation->Attenuation.AttenuationShapeExtents = FVector(150.f);
		Attenuation->Attenuation.FalloffDistance = FMath::Max(100.f, Range - 150.f);
		UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(World, Wave, Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, Attenuation);
		UE_LOG(LogHC, Log, TEXT("Placeholder sound %d (3D, %.1f s) %s"), static_cast<int32>(Sound), Seconds, Component ? TEXT("started") : TEXT("NOT started"));
		StopLater(World, Component, Seconds);
		return Component;
	}
}
