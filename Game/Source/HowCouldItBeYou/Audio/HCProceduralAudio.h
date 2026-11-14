#pragma once

#include "CoreMinimal.h"

class UAudioComponent;
class UObject;

/**
 * Placeholder sounds generated in code, so the prototype needs no audio assets:
 * a jumpscare stinger and the other sounds of the scares, a dull thump for the "minimal" intensity and a scream for witnesses.
 * Real sound design replaces these in the audio pass (docs/06 M7 / M12).
 */
namespace HCProceduralAudio
{
	enum class ESound : uint8
	{
		Stinger,
		Thump,
		Scream,
		/** Wet crackling of a shapeshift. */
		Shapeshift,
		/** Two-tone alarm for sabotage announcements. */
		Alarm,
		/** Pneumatic door: hiss, motor, the leaves hitting their stops. */
		Door,
		/** Three soft rising bells for the winners. */
		Victory,
		/** The switch of a flashlight: a short plastic click. */
		Click,
		// --- Jumpscare (AHCJumpscareDirector)
		/** A strike of the monster: a low punch with a wet crack on top. */
		Hit,
		/** The body hits the floor ("dragged down"). */
		Thud,
		/** One heartbeat, lub-dub. */
		Heartbeat,
		/** "Lights out": the tubes buzz and flicker, then the breaker drops. */
		PowerDown,
		/** Something breathes right next to you. */
		Breath,
		/** Ringing in the ears, rising and fading. */
		Ringing,
		/** "Signal lost": the whole broken signal in step with the picture (crackle, freezes, the tube switching off). */
		Static
	};

	/** Name of a sound; a real recording with this name replaces the built-in one (Tools\SetupSounds.bat). */
	HOWCOULDITBEYOU_API const TCHAR* Name(ESound Sound);

	/**
	 * Plays a 2D sound for the local player only. Safe to call without an audio device.
	 * @param OutVoices gets every voice that was started (the kill scream plays all its recordings at once).
	 */
	HOWCOULDITBEYOU_API UAudioComponent* Play2D(UObject* WorldContext, ESound Sound, float Volume, TArray<UAudioComponent*>* OutVoices = nullptr);

	/** Plays a sound in the world; audible up to roughly Range cm. */
	HOWCOULDITBEYOU_API UAudioComponent* PlayAtLocation(UObject* WorldContext, ESound Sound, const FVector& Location, float Volume, float Range);

	/** Raw 16-bit mono samples at 22.05 kHz (exposed for the unit test). */
	HOWCOULDITBEYOU_API TArray<int16> Generate(ESound Sound, float& OutSeconds);
}
