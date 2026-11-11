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

	/** Lets the engine's voice chat listen to this microphone (empty = Windows default). False if there is no voice chat. */
	HOWCOULDITBEYOU_API bool SetVoiceMicrophone(UWorld* World, const FString& DeviceName);
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
	TArray<uint8> Buffer;
};
