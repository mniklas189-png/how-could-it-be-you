#pragma once

#include "CoreMinimal.h"

class IVoiceCapture;
class UWorld;

/** The player's own sound devices: which microphone the voice chat listens to, where the game's sound goes. */
namespace HCAudioDevices
{
	struct FDevice
	{
		/** What the engine needs to pick the device; empty for the Windows default. */
		FString Id;
		/** What the player reads. */
		FString Name;
	};

	/** Microphones in the order Windows lists them, with the names the engine's voice capture uses. */
	HOWCOULDITBEYOU_API TArray<FDevice> GetMicrophones();

	/** Headsets and speakers. Asked on the audio thread; the answer comes back on the game thread. */
	HOWCOULDITBEYOU_API void RequestOutputs(TFunction<void(const TArray<FDevice>&)> OnDone);

	/** Sends the game's whole sound to that device (empty = Windows default). Nothing happens if it is there already. */
	HOWCOULDITBEYOU_API void SetOutput(const FString& DeviceId);

	/**
	 * Name of the microphone Windows uses for calls (sound settings, "default communication device"), else of the
	 * general default one. Empty if unknown. The engine itself would take the first microphone of its list.
	 */
	HOWCOULDITBEYOU_API FString GetDefaultMicrophone();

	/**
	 * Lets the engine's voice chat listen to this microphone (empty = Windows default). A microphone that cannot be
	 * opened (unplugged, or switched on after the game started: the engine only knows those of that moment) is replaced
	 * by the default one. False if no microphone listens afterwards, or there is no voice chat.
	 */
	HOWCOULDITBEYOU_API bool SetVoiceMicrophone(UWorld* World, const FString& DeviceName);
}

/**
 * The player's own microphone inside the engine's voice chat.
 *
 * The engine ties "the microphone is open" and "what it picks up is sent" together, and switches both by itself:
 * registering the local player opens and sends (again with every registration, i.e. every session and every map
 * change), and the server tells every player to stop after each map change. With push-to-talk that meant a microphone
 * that sent without the key until the key was pressed once. So the game sets both itself, every frame, from the
 * engine's real state (reached through its protected members, like the capture in SetVoiceMicrophone):
 * - listening: the capture runs. It keeps running while the voice chat is on, so the first syllable after the talk key
 *   is not lost to the capture starting up (about 70 ms, measured), and the key can let go a moment late.
 * - sending: what the capture delivers goes to the other players. Without it the sound is read and thrown away.
 */
namespace HCVoiceMic
{
	struct FState
	{
		/** The engine's voice chat knows the local player (has a capture for them). */
		bool bRegistered = false;
		/** What the microphone picks up is sent. */
		bool bSending = false;
		/** Something was really sent a moment ago (the engine's own talking state, about 0.2 s of hold). */
		bool bTalking = false;
		/** The microphone has no capture, or its capture failed (unplugged, switched off). */
		bool bFailed = false;
	};

	HOWCOULDITBEYOU_API FState Get(UWorld* World);
	/** Makes the engine listen and send like this, whatever it did by itself since the last call. */
	HOWCOULDITBEYOU_API void Set(UWorld* World, bool bListen, bool bSend);
}

/** Listens to a microphone for the level display in the settings menu (independent of the voice chat). */
class HOWCOULDITBEYOU_API FHCMicMeter
{
public:
	~FHCMicMeter();

	/** Starts listening to this microphone, or switches to it; empty = Windows default. */
	void Start(const FString& DeviceName);
	void Stop();
	bool IsRunning() const { return Capture.IsValid(); }

	/** Takes what arrived and returns the loudness (the scale of the open-microphone threshold, speech about 0.02..0.3). */
	float Update();

private:
	TSharedPtr<IVoiceCapture> Capture;
	FString Device;
	/** Start was tried for Device (a microphone that cannot be opened is not tried again every frame). */
	bool bTried = false;
	TArray<uint8> Buffer;
};
