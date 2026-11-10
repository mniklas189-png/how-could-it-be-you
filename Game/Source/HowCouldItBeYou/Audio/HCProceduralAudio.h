#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class UObject;

/**
 * Placeholder sounds generated in code, so the prototype needs no audio assets:
 * a jumpscare stinger, a dull thump for the "minimal" intensity and a scream for witnesses.
 * Real sound design replaces these in the audio pass (docs/06 M7 / M12).
 */
namespace HCProceduralAudio
{
	enum class ESound : uint8
	{
		Stinger,
		Thump,
		Scream
	};

	/** Plays a 2D sound for the local player only. Safe to call without an audio device. */
	HOWCOULDITBEYOU_API UAudioComponent* Play2D(UObject* WorldContext, ESound Sound, float Volume);

	/** Plays a sound in the world; audible up to roughly Range cm. */
	HOWCOULDITBEYOU_API UAudioComponent* PlayAtLocation(UObject* WorldContext, ESound Sound, const FVector& Location, float Volume, float Range);

	/** Raw 16-bit mono samples at 22.05 kHz (exposed for the unit test). */
	HOWCOULDITBEYOU_API TArray<int16> Generate(ESound Sound, float& OutSeconds);
}
