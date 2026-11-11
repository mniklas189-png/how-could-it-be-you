#include "Audio/HCProceduralAudio.h"

#include "Components/AudioComponent.h"
#include "Core/HCLog.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Math/RandomStream.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"
#include "World/HCModelAssets.h"

namespace HCProceduralAudio
{
	constexpr int32 SampleRate = 22050;

	static float Saw(double Phase)
	{
		return static_cast<float>(2.0 * (Phase - FMath::FloorToDouble(Phase + 0.5)));
	}

	const TCHAR* Name(ESound Sound)
	{
		switch (Sound)
		{
		case ESound::Stinger: return TEXT("Stinger");
		case ESound::Scream: return TEXT("Scream");
		case ESound::Shapeshift: return TEXT("Shapeshift");
		case ESound::Alarm: return TEXT("Alarm");
		case ESound::Door: return TEXT("Door");
		case ESound::Victory: return TEXT("Victory");
		default: return TEXT("Thump");
		}
	}

	// A human scream, roughly: a rough voice whose pitch shoots up, breaks and falls, shaped by the three resonances
	// of a wide open mouth, with breath on top and driven hard into saturation. Loud and ugly on purpose.
	static void FillScream(TArray<int16>& Samples, float Seconds)
	{
		struct FResonance
		{
			double B0 = 0.0, A1 = 0.0, A2 = 0.0, X1 = 0.0, X2 = 0.0, Y1 = 0.0, Y2 = 0.0;
			FResonance(double Frequency, double Q)
			{
				const double W = 2.0 * PI * Frequency / SampleRate;
				const double Alpha = FMath::Sin(W) / (2.0 * Q);
				B0 = Alpha / (1.0 + Alpha);
				A1 = -2.0 * FMath::Cos(W) / (1.0 + Alpha);
				A2 = (1.0 - Alpha) / (1.0 + Alpha);
			}
			double Run(double X)
			{
				const double Y = B0 * X - B0 * X2 - A1 * Y1 - A2 * Y2;
				X2 = X1;
				X1 = X;
				Y2 = Y1;
				Y1 = Y;
				return Y;
			}
		};
		FResonance Mouth[3] = { FResonance(1050.0, 5.0), FResonance(1650.0, 7.0), FResonance(3000.0, 9.0) };
		const double Gains[3] = { 1.0, 0.8, 0.5 };
		FRandomStream Random(4242);
		double Phase = 0.0, Half = 0.0, Drift = 0.0, Breath = 0.0;
		for (int32 i = 0; i < Samples.Num(); ++i)
		{
			const double T = static_cast<double>(i) / SampleRate;
			const double U = T / Seconds;
			// Pitch: up within a moment, held high and trembling, a break in the middle, then down as the air runs out
			double Pitch = U < 0.08 ? FMath::Lerp(360.0, 760.0, U / 0.08)
				: U < 0.7 ? 760.0 + 60.0 * FMath::Sin(2.0 * PI * 6.5 * T) : FMath::Lerp(790.0, 430.0, (U - 0.7) / 0.3);
			if (U > 0.42 && U < 0.5)
			{
				Pitch *= 1.32; // the voice breaks
			}
			Drift += 0.02 * (Random.FRandRange(-1.f, 1.f) - Drift);
			Pitch *= 1.0 + 0.25 * Drift;
			Phase += Pitch / SampleRate;
			Half += Pitch * 0.5 / SampleRate;
			// Rough: every other cycle of the voice is weaker
			const double Rough = 1.0 - 0.38 * (0.5 + 0.5 * FMath::Sin(2.0 * PI * Half));
			const double White = Random.FRandRange(-1.f, 1.f);
			Breath += 0.5 * (White - Breath);
			const double Source = Rough * Saw(Phase) + 0.35 * (White - Breath);
			double Voice = 0.12 * Source;
			for (int32 k = 0; k < 3; ++k)
			{
				Voice += Gains[k] * Mouth[k].Run(Source);
			}
			const double Envelope = FMath::Clamp(T / 0.025, 0.0, 1.0) * FMath::Clamp((Seconds - T) / 0.3, 0.0, 1.0);
			Samples[i] = static_cast<int16>(FMath::Tanh(4.0 * Envelope * Voice) * 30000.0);
		}
	}

	TArray<int16> Generate(ESound Sound, float& OutSeconds)
	{
		OutSeconds = Sound == ESound::Stinger ? 1.4f : Sound == ESound::Thump ? 0.9f : Sound == ESound::Shapeshift ? 1.5f : Sound == ESound::Alarm ? 0.9f : Sound == ESound::Door ? 0.9f : Sound == ESound::Victory ? 1.8f : 2.f;
		const int32 Count = FMath::RoundToInt(OutSeconds * SampleRate);
		TArray<int16> Samples;
		Samples.SetNumUninitialized(Count);
		if (Sound == ESound::Scream)
		{
			FillScream(Samples, OutSeconds);
			return Samples;
		}
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
			case ESound::Door:
			{
				// Pneumatics: a hiss that dies down, a motor underneath, and the leaves hitting their stops
				const float White = Random.FRandRange(-1.f, 1.f);
				Lowpass += 0.3f * (White - Lowpass);
				const float Hiss = (White - Lowpass) * FMath::Exp(-4.f * T) * FMath::Clamp(T / 0.015f, 0.f, 1.f);
				PhaseA += (150.0 - 60.0 * U) / SampleRate;
				const float Motor = T < 0.6f ? 0.16f * Saw(PhaseA) * FMath::Clamp(T / 0.05f, 0.f, 1.f) : 0.f;
				PhaseB += 62.0 / SampleRate;
				const float Hit = T > 0.6f ? FMath::Exp(-22.f * (T - 0.6f)) : 0.f;
				Value = 0.5f * Hiss + Motor + Hit * (0.8f * FMath::Sin(2.f * PI * static_cast<float>(PhaseB)) + 0.3f * White);
				break;
			}
			case ESound::Victory:
			{
				// Three soft bells, rising: relief rather than triumph
				const float Notes[3] = { 392.f, 493.9f, 587.3f };
				for (int32 Note = 0; Note < 3; ++Note)
				{
					const float Since = T - 0.22f * Note;
					if (Since >= 0.f)
					{
						Value += 0.3f * FMath::Sin(2.f * PI * Notes[Note] * Since) * FMath::Exp(-2.6f * Since) * FMath::Clamp(Since / 0.01f, 0.f, 1.f);
					}
				}
				break;
			}
			default:
				break;
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

	// A real recording set up with Tools\SetupSounds.bat wins over the built-in sound
	static USoundBase* FindReal(ESound Sound)
	{
		const UHCModelSet* Models = HCModels::Get();
		USoundBase* Real = Models ? Models->Sounds.FindRef(Name(Sound)).Get() : nullptr;
		if (Real)
		{
			UE_LOG(LogHC, Log, TEXT("Real sound %s"), *Real->GetName());
		}
		return Real;
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
		if (USoundBase* Real = FindReal(Sound))
		{
			return UGameplayStatics::SpawnSound2D(World, Real, Volume);
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
		USoundAttenuation* Attenuation = NewObject<USoundAttenuation>();
		Attenuation->Attenuation.bAttenuate = true;
		Attenuation->Attenuation.bSpatialize = true;
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation->Attenuation.AttenuationShapeExtents = FVector(150.f);
		Attenuation->Attenuation.FalloffDistance = FMath::Max(100.f, Range - 150.f);
		if (USoundBase* Real = FindReal(Sound))
		{
			return UGameplayStatics::SpawnSoundAtLocation(World, Real, Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, Attenuation);
		}
		float Seconds = 0.f;
		USoundWaveProcedural* Wave = MakeWave(Sound, Seconds);
		UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(World, Wave, Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, Attenuation);
		UE_LOG(LogHC, Log, TEXT("Placeholder sound %d (3D, %.1f s) %s"), static_cast<int32>(Sound), Seconds, Component ? TEXT("started") : TEXT("NOT started"));
		StopLater(World, Component, Seconds);
		return Component;
	}
}
