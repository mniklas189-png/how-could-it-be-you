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

	/** DSDEVID_DefaultCapture and DSDEVID_DefaultVoiceCapture of dsound.h, written out (their definitions live in a library this module does not link). */
	const GUID HCDefaultCaptureId = { 0xdef00001, 0x9c6d, 0x47ed, { 0xaa, 0xf1, 0x4d, 0xda, 0x8f, 0x2b, 0x5c, 0x03 } };
	const GUID HCDefaultVoiceCaptureId = { 0xdef00003, 0x9c6d, 0x47ed, { 0xaa, 0xf1, 0x4d, 0xda, 0x8f, 0x2b, 0x5c, 0x03 } };

	struct FHCCaptureDeviceQuery
	{
		GUID Id;
		FString Name;
	};

	BOOL CALLBACK FindCaptureDeviceName(LPGUID Guid, LPCWSTR Description, LPCWSTR Module, LPVOID Context)
	{
		FHCCaptureDeviceQuery* Query = static_cast<FHCCaptureDeviceQuery*>(Context);
		if (Guid && Description && FMemory::Memcmp(Guid, &Query->Id, sizeof(GUID)) == 0)
		{
			Query->Name = FString(Description);
			return 0; // found
		}
		return 1;
	}
#endif

	/**
	 * The engine keeps its voice capture and the state of the local talker to itself. These two reach them the way
	 * C++ allows for protected members (through a derived class); they are never created.
	 */
	struct FHCVoiceAccess : public FOnlineVoiceImpl
	{
		static IVoiceEnginePtr EngineOf(FOnlineVoiceImpl& Voice)
		{
			return Voice.*(&FHCVoiceAccess::VoiceEngine);
		}
		static FLocalTalker* LocalTalkerOf(FOnlineVoiceImpl& Voice, int32 Index)
		{
			TArray<FLocalTalker>& All = Voice.*(&FHCVoiceAccess::LocalTalkers);
			return All.IsValidIndex(Index) ? &All[Index] : nullptr;
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

	/** The engine's own voice chat. Only the Null subsystem (the one this game uses) is known to be a FOnlineVoiceImpl. */
	FOnlineVoiceImpl* HCVoiceImplOf(UWorld* World)
	{
		IOnlineSubsystem* Subsystem = World ? Online::GetSubsystem(World) : nullptr;
		if (!Subsystem || Subsystem->GetSubsystemName() != FName(TEXT("NULL")))
		{
			return nullptr;
		}
		const IOnlineVoicePtr Voice = Subsystem->GetVoiceInterface();
		return Voice.IsValid() ? static_cast<FOnlineVoiceImpl*>(Voice.Get()) : nullptr;
	}

	/** Its voice engine (capture, codec, playback); none if the voice chat is switched off in the configuration. */
	FVoiceEngineImpl* HCVoiceEngineOf(FOnlineVoiceImpl* Voice)
	{
		const IVoiceEnginePtr Engine = Voice ? FHCVoiceAccess::EngineOf(*Voice) : nullptr;
		return Engine.IsValid() ? static_cast<FVoiceEngineImpl*>(Engine.Get()) : nullptr;
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

	FString GetDefaultMicrophone()
	{
#if PLATFORM_WINDOWS
		FHCCaptureDeviceQuery Query;
		FMemory::Memzero(&Query.Id, sizeof(GUID));
		if (SUCCEEDED(GetDeviceID(&HCDefaultVoiceCaptureId, &Query.Id)) || SUCCEEDED(GetDeviceID(&HCDefaultCaptureId, &Query.Id)))
		{
			DirectSoundCaptureEnumerateW(&FindCaptureDeviceName, &Query);
		}
		return Query.Name;
#else
		return FString();
#endif
	}

	bool SetVoiceMicrophone(UWorld* World, const FString& DeviceName)
	{
		FVoiceEngineImpl* Engine = HCVoiceEngineOf(HCVoiceImplOf(World));
		IVoiceCapture* Capture = Engine ? FHCVoiceEngineAccess::CaptureOf(*Engine) : nullptr;
		if (!Capture)
		{
			return false; // no voice chat, or the local player is not registered yet
		}
		if (!PLATFORM_WINDOWS)
		{
			return true; // the capture of the other platforms cannot change its microphone: it listens to the system's
		}
		const int32 Rate = UVOIPStatics::GetVoiceSampleRate();
		const int32 Channels = UVOIPStatics::GetVoiceNumChannels();
		const FString Wanted = DeviceName.IsEmpty() ? GetDefaultMicrophone() : DeviceName;
		FString Used = Wanted;
		bool bOk = Capture->ChangeDevice(Used, Rate, Channels);
		if (!bOk && !Used.IsEmpty())
		{
			// A failed change leaves the capture without any microphone: rather the default one than silence
			Used.Reset();
			bOk = Capture->ChangeDevice(Used, Rate, Channels);
		}
		// The new capture does not run yet, while the engine believes the old one still does. Told to stop here;
		// HCVoiceMic::Set starts it again with its next call.
		Engine->StopLocalVoiceProcessing(0);
		UE_LOG(LogHC, Log, TEXT("Voice chat: microphone %s%s%s"), Used.IsEmpty() ? TEXT("first one of the engine's list") : *Used,
			DeviceName.IsEmpty() ? TEXT(" (Windows default)") : Used != Wanted ? TEXT(" (the chosen one could not be opened)") : TEXT(""),
			bOk ? TEXT("") : TEXT(" - NOT opened, no microphone listens"));
		return bOk;
	}
}

namespace HCVoiceMic
{
	FState Get(UWorld* World)
	{
		FState State;
		FOnlineVoiceImpl* Voice = HCVoiceImplOf(World);
		FVoiceEngineImpl* Engine = HCVoiceEngineOf(Voice);
		const FLocalTalker* Talker = Engine ? FHCVoiceAccess::LocalTalkerOf(*Voice, 0) : nullptr;
		if (!Talker)
		{
			return State;
		}
		State.bRegistered = Talker->bIsRegistered;
		State.bSending = Talker->bHasNetworkedVoice;
		State.bTalking = Talker->bIsRegistered && Talker->bIsTalking;
		if (IVoiceCapture* Capture = FHCVoiceEngineAccess::CaptureOf(*Engine))
		{
			uint32 Unused = 0;
			State.bFailed = Capture->GetCaptureState(Unused) == EVoiceCaptureState::Error || Capture->GetBufferSize() <= 0;
		}
		return State;
	}

	void Set(UWorld* World, bool bListen, bool bSend)
	{
		FOnlineVoiceImpl* Voice = HCVoiceImplOf(World);
		FVoiceEngineImpl* Engine = HCVoiceEngineOf(Voice);
		FLocalTalker* Talker = Engine ? FHCVoiceAccess::LocalTalkerOf(*Voice, 0) : nullptr;
		if (!Talker)
		{
			return;
		}
		// Both do nothing when the capture is in that state already (and for a player who is not registered)
		if (bListen)
		{
			Engine->StartLocalVoiceProcessing(0);
		}
		else
		{
			Engine->StopLocalVoiceProcessing(0);
		}
		Talker->bHasNetworkedVoice = bListen && bSend;
	}
}

FHCMicMeter::~FHCMicMeter()
{
	Stop();
}

void FHCMicMeter::Start(const FString& DeviceName)
{
	if (bTried && DeviceName == Device)
	{
		return;
	}
	Stop();
	Device = DeviceName;
	bTried = true;
	if (FVoiceModule::Get().IsVoiceEnabled())
	{
		// The same microphone the voice chat takes (HCAudioDevices::SetVoiceMicrophone)
		const FString Wanted = DeviceName.IsEmpty() ? HCAudioDevices::GetDefaultMicrophone() : DeviceName;
		Capture = FVoiceModule::Get().CreateVoiceCapture(Wanted);
		if (!Capture.IsValid() && !Wanted.IsEmpty())
		{
			Capture = FVoiceModule::Get().CreateVoiceCapture(FString());
		}
	}
	if (Capture.IsValid() && !Capture->Start())
	{
		Capture->Shutdown();
		Capture.Reset();
	}
}

void FHCMicMeter::Stop()
{
	bTried = false;
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
