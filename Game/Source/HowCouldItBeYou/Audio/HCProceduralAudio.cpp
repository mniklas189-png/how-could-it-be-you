#include "Audio/HCProceduralAudio.h"

#include "Abilities/HCJumpscareDirector.h"
#include "Audio/HCSoundVolumes.h"
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
		case ESound::Click: return TEXT("Click");
		case ESound::Hit: return TEXT("Hit");
		case ESound::Thud: return TEXT("Thud");
		case ESound::Heartbeat: return TEXT("Heartbeat");
		case ESound::PowerDown: return TEXT("PowerDown");
		case ESound::Breath: return TEXT("Breath");
		case ESound::Ringing: return TEXT("Ringing");
		case ESound::Static: return TEXT("Static");
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

	/** One-pole low-pass, fine for shaping noise: Alpha = 1 - exp(-2 pi f / rate). */
	struct FOnePole
	{
		float Alpha = 1.f;
		float State = 0.f;
		explicit FOnePole(float Frequency) : Alpha(1.f - FMath::Exp(-2.f * PI * Frequency / SampleRate)) {}
		float Low(float X) { State += Alpha * (X - State); return State; }
		float High(float X) { return X - Low(X); }
	};

	/** The punch of a strike, Since seconds after it landed. */
	struct FHitVoice
	{
		double Phase = 0.0;
		FOnePole CrackLow = FOnePole(900.f);
		FOnePole CrackHigh = FOnePole(4500.f);
		float Run(float Since, float White)
		{
			if (Since < 0.f)
			{
				return 0.f;
			}
			Phase += (45.0 + 80.0 * FMath::Exp(-25.f * Since)) / SampleRate;
			const float Body = FMath::Sin(2.f * PI * static_cast<float>(FMath::Frac(Phase))) * FMath::Exp(-7.f * Since);
			const float Crack = CrackHigh.Low(CrackLow.High(1.7f * White)) * FMath::Exp(-45.f * Since);
			return FMath::Tanh((Body * 1.4f + Crack * 0.8f) * 1.5f);
		}
	};

	/** A body on the floor, Since seconds after it landed. */
	struct FThudVoice
	{
		double Phase = 0.0;
		FOnePole Knock = FOnePole(600.f);
		float Run(float Since, float White)
		{
			if (Since < 0.f)
			{
				return 0.f;
			}
			Phase += (30.0 + 55.0 * FMath::Exp(-14.f * Since)) / SampleRate;
			const float Body = FMath::Sin(2.f * PI * static_cast<float>(FMath::Frac(Phase))) * FMath::Exp(-4.5f * Since);
			return FMath::Tanh((Body * 1.6f + Knock.Low(1.7f * White) * FMath::Exp(-30.f * Since) * 1.2f) * 1.4f);
		}
	};

	/** One thump of the heart. */
	struct FBeatVoice
	{
		double Phase = 0.0;
		float Run(float Since)
		{
			if (Since < 0.f)
			{
				return 0.f;
			}
			Phase += (40.0 + 28.0 * FMath::Exp(-30.f * Since)) / SampleRate;
			return FMath::Sin(2.f * PI * static_cast<float>(FMath::Frac(Phase))) * FMath::Exp(-15.f * Since);
		}
	};

	/**
	 * "Signal lost" in one piece, in step with the picture (HCScareTimeline): a crackle at the first strike, a buzzing
	 * digital stutter while the picture freezes, a sharp burst with every single-frame close-up, the whine of the tube
	 * and the dull pop when it switches off.
	 */
	static void FillStatic(TArray<int16>& Samples, float Seconds)
	{
		struct FBurst { float Start; float Length; float Gain; float Tone; };
		TArray<FBurst, TInlineAllocator<12>> Bursts;
		FRandomStream Random(777);
		Bursts.Add({ HCScareTimeline::SignalFirstBurst, 0.12f, 0.35f, float(Random.FRandRange(1800.f, 3200.f)) });
		for (const float (&Hold)[3] : HCScareTimeline::SignalHolds)
		{
			Bursts.Add({ Hold[0], Hold[1] - Hold[0], 0.22f, float(Random.FRandRange(1800.f, 3200.f)) });
		}
		for (const float Flash : HCScareTimeline::SignalFlashes)
		{
			Bursts.Add({ Flash, 0.045f, 0.6f, float(Random.FRandRange(1800.f, 3200.f)) });
		}
		const float WhineStart = HCScareTimeline::SignalCollapseStart;
		const float WhineEnd = HCScareTimeline::SignalCollapseEnd;
		FThudVoice Pop;
		float Held = 0.f;
		double WhinePhase = 0.0;
		for (int32 i = 0; i < Samples.Num(); ++i)
		{
			const float T = static_cast<float>(i) / SampleRate;
			const float White = Random.FRandRange(-1.f, 1.f);
			if (i % 11 == 0)
			{
				Held = White; // sample and hold: the crushed, digital sound of a broken signal
			}
			float Value = 0.f;
			for (const FBurst& Burst : Bursts)
			{
				const float Since = T - Burst.Start;
				if (Since >= 0.f && Since < Burst.Length)
				{
					const float Edge = FMath::Clamp(Since / 0.001f, 0.f, 1.f) * FMath::Clamp((Burst.Length - Since) / 0.001f, 0.f, 1.f);
					const float Square = FMath::Sin(2.f * PI * Burst.Tone * Since) >= 0.f ? 1.f : -1.f;
					Value += Burst.Gain * Edge * (0.35f * Square + 0.6f * Held);
				}
			}
			if (T >= WhineStart && T < WhineEnd)
			{
				const float U = (T - WhineStart) / (WhineEnd - WhineStart);
				WhinePhase += FMath::Lerp(6000.0, 10000.0, static_cast<double>(U)) / SampleRate;
				Value += 0.08f * FMath::Sin(2.f * PI * static_cast<float>(FMath::Frac(WhinePhase))) * FMath::Clamp((WhineEnd - T) / 0.005f, 0.f, 1.f);
			}
			Value += 0.5f * Pop.Run(T - WhineEnd, White);
			Value *= FMath::Clamp((Seconds - T) / 0.25f, 0.f, 1.f);
			Samples[i] = static_cast<int16>(FMath::Clamp(Value, -1.f, 1.f) * 30000.f);
		}
	}

	static float LengthOf(ESound Sound)
	{
		switch (Sound)
		{
		case ESound::Stinger: return 1.4f;
		case ESound::Thump: return 0.9f;
		case ESound::Shapeshift: return 1.5f;
		case ESound::Alarm: return 0.9f;
		case ESound::Door: return 0.9f;
		case ESound::Victory: return 1.8f;
		case ESound::Click: return 0.14f;
		case ESound::Hit: return 0.7f;
		case ESound::Thud: return 1.2f;
		case ESound::Heartbeat: return 0.6f;
		case ESound::PowerDown: return 0.9f;
		case ESound::Breath: return 0.6f;
		case ESound::Ringing: return 2.5f;
		case ESound::Static: return 2.9f;
		default: return 2.f; // Scream
		}
	}

	TArray<int16> Generate(ESound Sound, float& OutSeconds)
	{
		OutSeconds = LengthOf(Sound);
		const int32 Count = FMath::RoundToInt(OutSeconds * SampleRate);
		TArray<int16> Samples;
		Samples.SetNumUninitialized(Count);
		if (Sound == ESound::Scream)
		{
			FillScream(Samples, OutSeconds);
			return Samples;
		}
		if (Sound == ESound::Static)
		{
			FillStatic(Samples, OutSeconds);
			return Samples;
		}
		FRandomStream Random(Sound == ESound::Stinger ? 1337 : 4242);
		double PhaseA = 0.0, PhaseB = 0.0, PhaseC = 0.0;
		float Lowpass = 0.f;
		FHitVoice HitVoice;
		FThudVoice ThudVoice;
		FBeatVoice BeatA, BeatB;
		FOnePole BuzzHigh(3000.f), BreathHigh(250.f), BreathLow(1400.f);
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
			case ESound::Click:
			{
				// Flashlight switch: the spring contact snaps, a moment later the button lands. Each is a crack of
				// bright noise with a small, quickly dying ring of the plastic housing, over a soft knock of the body.
				const float White = Random.FRandRange(-1.f, 1.f);
				Lowpass += 0.5f * (White - Lowpass);
				const float Bright = White - Lowpass;
				auto Tick = [Bright](float Since, float Ring, float Gain)
				{
					return Since < 0.f ? 0.f : Gain * (0.8f * Bright * FMath::Exp(-700.f * Since)
						+ 0.45f * FMath::Sin(2.f * PI * Ring * Since) * FMath::Exp(-170.f * Since)
						+ 0.3f * FMath::Sin(2.f * PI * 190.f * Since) * FMath::Exp(-70.f * Since));
				};
				Value = 0.55f * (Tick(T, 3100.f, 1.f) + Tick(T - 0.034f, 2300.f, 0.55f));
				break;
			}
			case ESound::Hit:
			{
				Value = 0.9f * HitVoice.Run(T, Random.FRandRange(-1.f, 1.f));
				break;
			}
			case ESound::Thud:
			{
				Value = 0.9f * ThudVoice.Run(T, Random.FRandRange(-1.f, 1.f));
				break;
			}
			case ESound::Heartbeat:
			{
				// Lub-dub: two dull thumps, the second one weaker
				Value = 0.95f * FMath::Tanh((BeatA.Run(T) + 0.7f * BeatB.Run(T - 0.17f)) * 1.5f);
				break;
			}
			case ESound::PowerDown:
			{
				// The tubes: mains buzz in three stutters, the last one the loudest; then the breaker drops
				const float White = Random.FRandRange(-1.f, 1.f);
				auto Gate = [T](float From, float To, float Gain)
				{
					return T < From || T >= To ? 0.f : Gain * FMath::Clamp((T - From) / 0.004f, 0.f, 1.f) * FMath::Clamp((To - T) / 0.004f, 0.f, 1.f);
				};
				const float Stutter = Gate(0.06f, 0.11f, 0.35f) + Gate(0.17f, 0.20f, 0.35f) + Gate(0.23f, 0.30f, 0.6f);
				const float Mains = FMath::Sin(2.f * PI * 100.f * T) >= 0.f ? 0.3f : -0.3f;
				const float Hum = Mains + 0.85f * BuzzHigh.High(White);
				Value = Stutter * Hum + 0.3f * HitVoice.Run(T - 0.30f, White);
				break;
			}
			case ESound::Breath:
			{
				// A slow breath right next to the ear: band-limited noise that swells and dies
				const float Band = BreathLow.Low(BreathHigh.High(Random.FRandRange(-1.f, 1.f)));
				Value = 0.7f * FMath::Tanh(2.2f * Band) * FMath::Square(FMath::Sin(PI * U));
				break;
			}
			case ESound::Ringing:
			{
				// Tinnitus: one high, pure tone that slowly comes up
				Value = 0.5f * FMath::Sin(2.f * PI * 4000.f * T) * FMath::Clamp(T / 0.6f, 0.f, 1.f) * FMath::Clamp((OutSeconds - T) / 0.6f, 0.f, 1.f);
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
	// A sound may have several recordings ("Scream", "Scream2", "Scream3").
	// - The kill scream plays ALL of them at once, on top of each other. That mix of the two screams is what makes a
	//   kill so frightening; the author of the game asked for exactly this. Do not turn it back into "one of them".
	// - Every other sound plays one recording, chosen by Pick.
	using FRealSounds = TArray<USoundBase*, TInlineAllocator<3>>;
	static FRealSounds FindReal(ESound Sound, int32 Pick)
	{
		const UHCModelSet* Models = HCModels::Get();
		if (!Models)
		{
			return FRealSounds();
		}
		FRealSounds Variants;
		for (int32 Number = 1; Number <= 3; ++Number)
		{
			const FString Key = Number == 1 ? FString(Name(Sound)) : FString::Printf(TEXT("%s%d"), Name(Sound), Number);
			if (USoundBase* Variant = Models->Sounds.FindRef(*Key).Get())
			{
				Variants.Add(Variant);
			}
		}
		if (Variants.Num() > 1 && Sound != ESound::Scream)
		{
			USoundBase* Chosen = Variants[FMath::Abs(Pick) % Variants.Num()];
			Variants.Reset();
			Variants.Add(Chosen);
		}
		for (const USoundBase* Real : Variants)
		{
			UE_LOG(LogHC, Log, TEXT("Real sound %s"), *Real->GetName());
		}
		return Variants;
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

	UAudioComponent* Play2D(UObject* WorldContext, ESound Sound, float Volume, TArray<UAudioComponent*>* OutVoices)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		// The player's own volume for this kind of sound (settings, page "Ton")
		Volume *= HCSoundVolumes::Of(World, Sound);
		if (!World || World->GetNetMode() == NM_DedicatedServer || Volume <= 0.f || !World->GetAudioDeviceRaw())
		{
			return nullptr;
		}
		const FRealSounds RealSounds = FindReal(Sound, FMath::Rand());
		if (RealSounds.Num() > 0)
		{
			UAudioComponent* First = nullptr;
			for (USoundBase* Real : RealSounds)
			{
				UAudioComponent* Voice = UGameplayStatics::SpawnSound2D(World, Real, Volume);
				First = First ? First : Voice;
				if (Voice && OutVoices)
				{
					OutVoices->Add(Voice);
				}
			}
			return First;
		}
		float Seconds = 0.f;
		USoundWaveProcedural* Wave = MakeWave(Sound, Seconds);
		UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, Wave, Volume);
		UE_LOG(LogHC, Log, TEXT("Placeholder sound %d (2D, %.1f s) %s"), static_cast<int32>(Sound), Seconds, Component ? TEXT("started") : TEXT("NOT started"));
		StopLater(World, Component, Seconds);
		if (Component && OutVoices)
		{
			OutVoices->Add(Component);
		}
		return Component;
	}

	UAudioComponent* PlayAtLocation(UObject* WorldContext, ESound Sound, const FVector& Location, float Volume, float Range)
	{
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		// The player's own volume for this kind of sound (settings, page "Ton")
		Volume *= HCSoundVolumes::Of(World, Sound);
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
		// Where one recording is chosen, the place decides which, so everybody hears the same one
		const FRealSounds RealSounds = FindReal(Sound, FMath::RoundToInt32(Location.X / 50.0) * 31 + FMath::RoundToInt32(Location.Y / 50.0) * 17);
		if (RealSounds.Num() > 0)
		{
			UAudioComponent* First = nullptr;
			for (USoundBase* Real : RealSounds)
			{
				UAudioComponent* Voice = UGameplayStatics::SpawnSoundAtLocation(World, Real, Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, Attenuation);
				First = First ? First : Voice;
			}
			return First;
		}
		float Seconds = 0.f;
		USoundWaveProcedural* Wave = MakeWave(Sound, Seconds);
		UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(World, Wave, Location, FRotator::ZeroRotator, Volume, 1.f, 0.f, Attenuation);
		UE_LOG(LogHC, Log, TEXT("Placeholder sound %d (3D, %.1f s) %s"), static_cast<int32>(Sound), Seconds, Component ? TEXT("started") : TEXT("NOT started"));
		StopLater(World, Component, Seconds);
		return Component;
	}
}
