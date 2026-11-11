#include "Core/HCGameInstance.h"

#include "Core/HCLog.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Online/HCSessionSubsystem.h"

namespace
{
	const TCHAR* UserSection = TEXT("HowCouldItBeYou.User");
}

void UHCGameInstance::Init()
{
	Super::Init();
	LoadUserSettings();
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UHCGameInstance::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UHCGameInstance::HandleTravelFailure);
	}
}

void UHCGameInstance::Shutdown()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Super::Shutdown();
}

void UHCGameInstance::SetMenuMessage(const FText& Message)
{
	// Keep the first, most specific message (e.g. "host left" before the generic network error)
	if (PendingMenuMessage.IsEmpty())
	{
		PendingMenuMessage = Message;
	}
}

FText UHCGameInstance::ConsumeMenuMessage()
{
	FText Message = PendingMenuMessage;
	PendingMenuMessage = FText::GetEmpty();
	if (!Message.IsEmpty())
	{
		UE_LOG(LogHCNet, Display, TEXT("Main menu message: %s"), *Message.ToString());
	}
	return Message;
}

FString UHCGameInstance::GetPlayerName() const
{
	FString Name;
	if (GConfig)
	{
		GConfig->GetString(UserSection, TEXT("PlayerName"), Name, GGameUserSettingsIni);
	}
	return Name;
}

void UHCGameInstance::SetPlayerName(const FString& Name)
{
	if (GConfig)
	{
		GConfig->SetString(UserSection, TEXT("PlayerName"), *Name, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

EHCJumpscareIntensity UHCGameInstance::GetJumpscareIntensity() const
{
	int32 Value = 0;
	if (GConfig)
	{
		GConfig->GetInt(UserSection, TEXT("JumpscareIntensity"), Value, GGameUserSettingsIni);
	}
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("HCIntensity="), Value);
#endif
	return static_cast<EHCJumpscareIntensity>(FMath::Clamp(Value, 0, 2));
}

void UHCGameInstance::SetJumpscareIntensity(EHCJumpscareIntensity Intensity)
{
	if (GConfig)
	{
		GConfig->SetInt(UserSection, TEXT("JumpscareIntensity"), static_cast<int32>(Intensity), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

EHCJumpscareIntensity UHCGameInstance::CycleJumpscareIntensity()
{
	const EHCJumpscareIntensity Next = static_cast<EHCJumpscareIntensity>((static_cast<int32>(GetJumpscareIntensity()) + 1) % 3);
	SetJumpscareIntensity(Next);
	return Next;
}

FText UHCGameInstance::GetJumpscareIntensityLabel(EHCJumpscareIntensity Intensity)
{
	switch (Intensity)
	{
	case EHCJumpscareIntensity::Reduced: return NSLOCTEXT("HC", "ScareReduced", "SCHRECK-INTENSITÄT: REDUZIERT");
	case EHCJumpscareIntensity::Minimal: return NSLOCTEXT("HC", "ScareMinimal", "SCHRECK-INTENSITÄT: MINIMAL");
	default: return NSLOCTEXT("HC", "ScareFull", "SCHRECK-INTENSITÄT: VOLL");
	}
}

bool UHCGameInstance::HasAcceptedEpilepsyWarning() const
{
	bool bAccepted = false;
	if (GConfig)
	{
		GConfig->GetBool(UserSection, TEXT("bEpilepsyWarningAccepted"), bAccepted, GGameUserSettingsIni);
	}
	return bAccepted;
}

void UHCGameInstance::AcceptEpilepsyWarning()
{
	if (GConfig)
	{
		GConfig->SetBool(UserSection, TEXT("bEpilepsyWarningAccepted"), true, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UHCGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogHCNet, Warning, TEXT("Network failure %s: %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
	if (bLeavingOnPurpose)
	{
		return;
	}
	// The engine itself returns to the default map (main menu); only remember why (docs/03 §7.6).
	if (FailureType == ENetworkFailure::ConnectionLost || FailureType == ENetworkFailure::ConnectionTimeout || FailureType == ENetworkFailure::FailureReceived)
	{
		SetMenuMessage(NSLOCTEXT("HC", "HostLeft", "Der Host hat das Spiel verlassen oder die Verbindung ist abgebrochen."));
	}
	else if (FailureType == ENetworkFailure::OutdatedClient || FailureType == ENetworkFailure::OutdatedServer)
	{
		SetMenuMessage(NSLOCTEXT("HC", "WrongVersion", "Falsche Spielversion. Starte den Launcher und installiere das Update."));
	}
	else
	{
		SetMenuMessage(FText::Format(NSLOCTEXT("HC", "NetError", "Verbindung fehlgeschlagen: {0}"), FText::FromString(ErrorString)));
	}
	if (UHCSessionSubsystem* Sessions = GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->DestroySessionQuietly();
	}
}

void UHCGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogHCNet, Warning, TEXT("Travel failure %s: %s"), ETravelFailure::ToString(FailureType), *ErrorString);
	if (!bLeavingOnPurpose)
	{
		SetMenuMessage(FText::Format(NSLOCTEXT("HC", "TravelError", "Beitritt fehlgeschlagen: {0}"), FText::FromString(ErrorString)));
	}
}

void UHCGameInstance::LoadUserSettings()
{
	if (GConfig)
	{
		GConfig->GetFloat(UserSection, TEXT("MasterVolume"), MasterVolume, GGameUserSettingsIni);
		GConfig->GetFloat(UserSection, TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
		GConfig->GetFloat(UserSection, TEXT("FieldOfView"), FieldOfView, GGameUserSettingsIni);
	}
	MasterVolume = FMath::Clamp(MasterVolume, 0.f, 1.f);
	MouseSensitivity = FMath::Clamp(MouseSensitivity, 0.2f, 3.f);
	FieldOfView = FMath::Clamp(FieldOfView, 70.f, 110.f);
}

static void SaveUserFloat(const TCHAR* Section, const TCHAR* Key, float Value)
{
	if (GConfig)
	{
		GConfig->SetFloat(Section, Key, Value, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UHCGameInstance::SetMasterVolume(float Volume)
{
	MasterVolume = FMath::Clamp(FMath::RoundToFloat(Volume * 10.f) / 10.f, 0.f, 1.f);
	SaveUserFloat(UserSection, TEXT("MasterVolume"), MasterVolume);
	ApplyMasterVolume();
}

void UHCGameInstance::SetMouseSensitivity(float Factor)
{
	MouseSensitivity = FMath::Clamp(FMath::RoundToFloat(Factor * 10.f) / 10.f, 0.2f, 3.f);
	SaveUserFloat(UserSection, TEXT("MouseSensitivity"), MouseSensitivity);
}

void UHCGameInstance::SetFieldOfView(float Degrees)
{
	FieldOfView = FMath::Clamp(FMath::RoundToFloat(Degrees / 5.f) * 5.f, 70.f, 110.f);
	SaveUserFloat(UserSection, TEXT("FieldOfView"), FieldOfView);
}

void UHCGameInstance::ApplyMasterVolume()
{
	UWorld* World = GetWorld();
	FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
	if (!Device)
	{
		return;
	}
	float Volume = MasterVolume;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("HCMute")))
	{
		Volume = 0.f; // test runs
	}
#endif
	Device->SetTransientPrimaryVolume(Volume);
}
