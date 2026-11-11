#include "Voice/HCAudioDevices.h"

#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "AudioThread.h"
#include "Core/HCLog.h"
#include "Engine/Engine.h"
#include "Interfaces/VoiceCapture.h"
#include "Net/VoiceConfig.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "VoiceEngineImpl.h"
#include "VoiceInterfaceImpl.h"
#include "VoiceModule.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmsystem.h>
#include <dsound.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
#if PLATFORM_WINDOWS
	BOOL CALLBACK CollectCaptureDevice(LPGUID Guid, LPCWSTR Description, LPCWSTR Module, LPVOID Context)
	{
		// The first entry has no GUID: "primary sound capture driver", i.e. the default, which the list already has
		if (Guid && Description)
		{
			static_cast<TArray<HCAudioDevices::FDevice>*>(Context)->Add({ FString(Description), FString(Description) });
		}
		return 1; // go on listing
	}
#endif

	/**
	 * The engine keeps its voice capture to itself. These two reach it the way C++ allows for protected members
	 * (through a derived class); they are never created.
	 */
	struct FHCVoiceAccess : public FOnlineVoiceImpl
	{
		static IVoiceEnginePtr EngineOf(FOnlineVoiceImpl& Voice)
		{
			return Voice.*(&FHCVoiceAccess::VoiceEngine);
		}
	};

	struct FHCVoiceEngineAccess : public FVoiceEngineImpl
	{
		static IVoiceCapture* CaptureOf(FVoiceEngineImpl& Engine)
		{
			TSharedPtr<IVoiceCapture>& (FVoiceEngineImpl::*Getter)() = &FHCVoiceEngineAccess::GetVoiceCapture;
			return (Engine.*Getter)().Get();
		}
	};

	Audio::IAudioMixerPlatformInterface* MixerPlatform(FAudioDevice* Device)
	{
		// Every audio device of UE 5 is an audio mixer device
		return Device ? static_cast<Audio::FMixerDevice*>(Device)->GetAudioMixerPlatform() : nullptr;
	}
}

namespace HCAudioDevices
{
	TArray<FDevice> GetMicrophones()
	{
		TArray<FDevice> Devices;
#if PLATFORM_WINDOWS
		// The same list (DirectSound) and names the engine's voice capture picks its device from
		DirectSoundCaptureEnumerateW(&CollectCaptureDevice, &Devices);
#endif
		return Devices;
	}

	void RequestOutputs(TFunction<void(const TArray<FDevice>&)> OnDone)
	{
		FAudioDevice* Main = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr;
		FAudioThread::RunCommandOnAudioThread([Main, OnDone = MoveTemp(OnDone)]()
		{
			TArray<FDevice> Devices;
			if (Audio::IAudioMixerPlatformInterface* Platform = MixerPlatform(Main))
			{
				uint32 Num = 0;
				if (Platform->GetNumOutputDevices(Num))
				{
					for (uint32 Index = 0; Index < Num; ++Index)
					{
						Audio::FAudioPlatformDeviceInfo Info;
						if (Platform->GetOutputDeviceInfo(Index, Info) && !Info.DeviceId.IsEmpty())
						{
							Devices.Add({ Info.DeviceId, Info.Name });
						}
					}
				}
			}
			FAudioThread::RunCommandOnGameThread([OnDone, Devices = MoveTemp(Devices)]()
			{
				OnDone(Devices);
			});
		});
	}

	void SetOutput(const FString& DeviceId)
	{
		// The engine starts on the Windows default
		static FString Applied;
		FAudioDevice* Main = GEngine ? GEngine->GetMainAudioDeviceRaw() : nullptr;
		if (DeviceId == Applied || GIsEditor || !MixerPlatform(Main))
		{
			return; // in the editor the sound belongs to the editor
		}
		Applied = DeviceId;
		UE_LOG(LogHC, Log, TEXT("Sound output: %s"), DeviceId.IsEmpty() ? TEXT("Windows default") : *DeviceId);
		FAudioThread::RunCommandOnAudioThread([Main, DeviceId]()
		{
			if (Audio::IAudioMixerPlatformInterface* Platform = MixerPlatform(Main))
			{
				Platform->RequestDeviceSwap(DeviceId, /*bInForce*/ true, TEXT("How Could It Be You settings: output device"));
			}
		});
	}

	bool SetVoiceMicrophone(UWorld* World, const FString& DeviceName)
	{
		// Only the Null subsystem (the one this game uses) is known to use the engine's FOnlineVoiceImpl
		IOnlineSubsystem* Subsystem = Online::GetSubsystem(World);
		if (!Subsystem || Subsystem->GetSubsystemName() != FName(TEXT("NULL")))
		{
			return false;
		}
		const IOnlineVoicePtr Voice = Subsystem->GetVoiceInterface();
		const IVoiceEnginePtr Engine = Voice.IsValid() ? FHCVoiceAccess::EngineOf(static_cast<FOnlineVoiceImpl&>(*Voice)) : nullptr;
		IVoiceCapture* Capture = Engine.IsValid() ? FHCVoiceEngineAccess::CaptureOf(static_cast<FVoiceEngineImpl&>(*Engine)) : nullptr;
		const bool bOk = Capture && Capture->ChangeDevice(DeviceName, UVOIPStatics::GetVoiceSampleRate(), UVOIPStatics::GetVoiceNumChannels());
		UE_LOG(LogHC, Log, TEXT("Voice chat: microphone %s%s"), DeviceName.IsEmpty() ? TEXT("Windows default") : *DeviceName, bOk ? TEXT("") : TEXT(" (NOT switched)"));
		return bOk;
	}
}

FHCMicMeter::~FHCMicMeter()
{
	Stop();
}

void FHCMicMeter::Start(const FString& DeviceName)
{
	if (Capture.IsValid() && DeviceName == Device)
	{
		return;
	}
	Stop();
	Device = DeviceName;
	if (FVoiceModule::Get().IsVoiceEnabled())
	{
		Capture = FVoiceModule::Get().CreateVoiceCapture(DeviceName);
	}
	if (Capture.IsValid() && !Capture->Start())
	{
		Capture->Shutdown();
		Capture.Reset();
	}
}

void FHCMicMeter::Stop()
{
	if (Capture.IsValid())
	{
		Capture->Stop();
		Capture->Shutdown();
		Capture.Reset();
	}
}

float FHCMicMeter::Update()
{
	if (!Capture.IsValid())
	{
		return 0.f;
	}
	// Take the recorded sound away, otherwise the capture stops at a full buffer
	uint32 Available = 0;
	if (Capture->GetCaptureState(Available) == EVoiceCaptureState::Ok && Available > 0)
	{
		Buffer.SetNumUninitialized(Available, EAllowShrinking::No);
		uint32 Written = 0;
		Capture->GetVoiceData(Buffer.GetData(), Available, Written);
	}
	return FMath::Max(0.f, Capture->GetCurrentAmplitude());
}
