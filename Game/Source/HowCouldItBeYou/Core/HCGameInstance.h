#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "HCGameInstance.generated.h"

class UNetDriver;

/**
 * Lives for the whole game session (survives map changes): pending menu message after a disconnect,
 * "intro already seen", travel bookkeeping and roles forced with hc.ForceRole (docs/02 §4).
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	/** Shown once by the main menu, e.g. "Der Host hat das Spiel verlassen." */
	void SetMenuMessage(const FText& Message);
	FText ConsumeMenuMessage();

	/** Player name chosen in the main menu (saved in GameUserSettings.ini). */
	FString GetPlayerName() const;
	void SetPlayerName(const FString& Name);

	/** Personal jumpscare intensity (saved in GameUserSettings.ini). */
	EHCJumpscareIntensity GetJumpscareIntensity() const;
	void SetJumpscareIntensity(EHCJumpscareIntensity Intensity);
	/** Switches to the next level and returns it. */
	EHCJumpscareIntensity CycleJumpscareIntensity();
	static FText GetJumpscareIntensityLabel(EHCJumpscareIntensity Intensity);

	// --- Personal settings (settings menu; saved in GameUserSettings.ini, in effect at once) ---
	/** 0..1 */
	float GetMasterVolume() const { return MasterVolume; }
	void SetMasterVolume(float Volume);
	/** Factor on the mouse look, 0.2..3 */
	float GetMouseSensitivity() const { return MouseSensitivity; }
	void SetMouseSensitivity(float Factor);
	/** Horizontal field of view of the own camera in degrees, 70..110 */
	float GetFieldOfView() const { return FieldOfView; }
	void SetFieldOfView(float Degrees);
	/** Brings the volume into effect; called after every map change and after a change. */
	void ApplyMasterVolume();

	bool HasAcceptedEpilepsyWarning() const;
	void AcceptEpilepsyWarning();

	// --- Server bookkeeping ---
	bool bIntroSeenThisSession = false;

private:
	void LoadUserSettings();
	float MasterVolume = 1.f;
	float MouseSensitivity = 1.f;
	float FieldOfView = 90.f;

public:
	/** Players the host expects after travelling lobby -> match map (0 = not travelled). */
	int32 ExpectedTravellers = 0;
	/** Increases with every role assignment (FHCRoleInfo::MatchSerial). */
	uint8 MatchSerial = 0;
	/** hc.ForceRole: player name -> role for the next assignment. */
	TMap<FString, EHCRole> ForcedRoles;

	/** Set while the local host deliberately ends the session (no error message for ourselves). */
	bool bLeavingOnPurpose = false;

	/** Development smoke-test state; lives here because seamless travel recreates the player controller. */
	struct FSmokeState
	{
		bool bSawGameOver = false;
		bool bStarted = false;
		bool bEnded = false;
		bool bQuitting = false;
		int32 ShotIndex = 0;
	};
	FSmokeState Smoke;

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	FText PendingMenuMessage;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
