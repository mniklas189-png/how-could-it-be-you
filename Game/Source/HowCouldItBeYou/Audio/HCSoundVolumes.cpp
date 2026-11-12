#include "Audio/HCSoundVolumes.h"

#include "Components/AudioComponent.h"
#include "Core/HCGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "World/HCModelAssets.h"

namespace HCSoundVolumes
{
	const TCHAR* const BackgroundKey = TEXT("VolumeBackground");

	EGroup GroupOf(HCProceduralAudio::ESound Sound)
	{
		using HCProceduralAudio::ESound;
		switch (Sound)
		{
		case ESound::Stinger:
		case ESound::Thump:
		case ESound::Scream:
			return EGroup::Scares;
		case ESound::Alarm:
		case ESound::Victory:
			return EGroup::Signals;
		default:
			return EGroup::Effects; // Shapeshift, Door, Click
		}
	}

	const TCHAR* OptionKey(EGroup Group)
	{
		switch (Group)
		{
		case EGroup::Scares: return TEXT("VolumeScares");
		case EGroup::Ambience: return TEXT("VolumeAmbience");
		case EGroup::Signals: return TEXT("VolumeSignals");
		default: return TEXT("VolumeEffects");
		}
	}

	float Of(const UObject* WorldContext, EGroup Group)
	{
		const UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		const UHCGameInstance* GI = World ? Cast<UHCGameInstance>(World->GetGameInstance()) : nullptr;
		return GI ? FMath::Clamp(GI->GetOption(OptionKey(Group), 1.f), 0.f, 1.f) : 1.f;
	}

	float Of(const UObject* WorldContext, HCProceduralAudio::ESound Sound)
	{
		return Of(WorldContext, GroupOf(Sound));
	}

	UAudioComponent* PlaySample(UObject* WorldContext, EGroup Group)
	{
		using HCProceduralAudio::ESound;
		switch (Group)
		{
		case EGroup::Scares:
			return HCProceduralAudio::Play2D(WorldContext, ESound::Stinger, 1.f);
		case EGroup::Signals:
			return HCProceduralAudio::Play2D(WorldContext, ESound::Alarm, 0.5f);
		case EGroup::Effects:
			return HCProceduralAudio::Play2D(WorldContext, ESound::Door, 0.65f);
		default:
			break;
		}
		// Ambience: a moment of the ventilation hum (only a recording exists for it, Tools\SetupSounds.bat)
		UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
		const UHCModelSet* Models = HCModels::Get();
		USoundBase* Hum = Models ? Models->Sounds.FindRef(TEXT("Ventilation")).Get() : nullptr;
		const float Volume = 0.4f * Of(WorldContext, EGroup::Ambience); // about as loud as in the station
		if (!World || !Hum || Volume <= 0.f || !World->GetAudioDeviceRaw())
		{
			return nullptr;
		}
		UAudioComponent* Sample = UGameplayStatics::SpawnSound2D(World, Hum, Volume, 1.f, FMath::FRandRange(0.f, FMath::Max(0.f, Hum->GetDuration() - 3.f)));
		if (Sample)
		{
			TWeakObjectPtr<UAudioComponent> Weak(Sample);
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak]()
			{
				if (UAudioComponent* Still = Weak.Get())
				{
					Still->FadeOut(0.6f, 0.f);
				}
			}), 1.6f, false);
		}
		return Sample;
	}
}
