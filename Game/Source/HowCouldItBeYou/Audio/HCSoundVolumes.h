#pragma once

#include "CoreMinimal.h"
#include "Audio/HCProceduralAudio.h"

class UAudioComponent;
class UObject;

/**
 * The player's own volume for each kind of sound (settings, page "Ton"). The total volume works on the whole game
 * (UHCGameInstance::SetMasterVolume); these work on top of it, per group. One-shot sounds take the group's volume when
 * they start; the station's background sounds follow a change at once (AHCStationBuilder::UpdateFlicker).
 */
namespace HCSoundVolumes
{
	enum class EGroup : uint8
	{
		/** Doors, switches, shapeshifts: what happens in the station. */
		Effects,
		/** Jumpscares and screams. */
		Scares,
		/** Ventilation and crackling lamps. */
		Ambience,
		/** Sabotage alarm, end of the round. */
		Signals,
		Count
	};

	/** The group a sound belongs to. */
	HOWCOULDITBEYOU_API EGroup GroupOf(HCProceduralAudio::ESound Sound);

	/** Name of the setting that keeps a group's volume (UHCGameInstance::GetOption). */
	HOWCOULDITBEYOU_API const TCHAR* OptionKey(EGroup Group);

	/** The player's volume for a group, 0..1 (1 without a game instance). */
	HOWCOULDITBEYOU_API float Of(const UObject* WorldContext, EGroup Group);
	HOWCOULDITBEYOU_API float Of(const UObject* WorldContext, HCProceduralAudio::ESound Sound);

	/** Name of the setting for the volume while the game window is in the background (0 = silent, the engine's default). */
	HOWCOULDITBEYOU_API extern const TCHAR* const BackgroundKey;

	/** A short sample of a group at the chosen volume, for the settings menu; null if there is nothing to play. */
	HOWCOULDITBEYOU_API UAudioComponent* PlaySample(UObject* WorldContext, EGroup Group);
}
