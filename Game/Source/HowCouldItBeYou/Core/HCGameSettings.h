#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Tasks/HCTaskTypes.h"
#include "HCGameSettings.generated.h"

/**
 * All tunable start values from docs/01 §14. Edited in Project Settings -> Game -> How Could It Be You,
 * stored in Config/DefaultGame.ini.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "How Could It Be You"))
class HOWCOULDITBEYOU_API UHCGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHCGameSettings();

	static const UHCGameSettings* Get() { return GetDefault<UHCGameSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** Task definitions (docs/02 §9.1). Defaults = FHCTaskCatalog; entries in DefaultGame.ini replace them. */
	UPROPERTY(Config, EditAnywhere, Category = "Tasks")
	TArray<FHCTaskDefinition> TaskDefinitions;

	const FHCTaskDefinition* FindTask(FName TaskId) const;

	// --- Movement ---
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (Units = "cm/s"))
	float WalkSpeed = 300.f;

	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (Units = "cm/s"))
	float SprintSpeed = 500.f;

	/** Seconds of sprinting from full stamina. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (Units = "s"))
	float StaminaSeconds = 6.f;

	/** Seconds to recover from empty to full stamina. */
	UPROPERTY(Config, EditAnywhere, Category = "Movement", meta = (Units = "s"))
	float StaminaRecoverySeconds = 10.f;

	// --- Flashlight ---
	UPROPERTY(Config, EditAnywhere, Category = "Flashlight", meta = (Units = "s"))
	float FlashlightBatterySeconds = 120.f;

	/** Seconds to recharge from empty while switched off. */
	UPROPERTY(Config, EditAnywhere, Category = "Flashlight", meta = (Units = "s"))
	float FlashlightRechargeSeconds = 90.f;

	/** Hold duration at a charging station for a full battery. */
	UPROPERTY(Config, EditAnywhere, Category = "Flashlight", meta = (Units = "s"))
	float ChargingStationSeconds = 10.f;

	UPROPERTY(Config, EditAnywhere, Category = "Flashlight", meta = (Units = "cm"))
	float FlashlightRange = 1500.f;

	UPROPERTY(Config, EditAnywhere, Category = "Flashlight", meta = (Units = "cm"))
	float HelmetLightRange = 300.f;

	// --- Interaction ---
	UPROPERTY(Config, EditAnywhere, Category = "Interaction", meta = (Units = "cm"))
	float InteractionRange = 180.f;

	// --- Players / flow (docs/02 §17) ---
	/** Recommended minimum; fewer players get a hint in the lobby. */
	UPROPERTY(Config, EditAnywhere, Category = "Flow")
	int32 MinPlayers = 4;

	UPROPERTY(Config, EditAnywhere, Category = "Flow")
	int32 MaxPlayers = 8;

	/** Hard minimum for the start button (2 = test rounds possible). */
	UPROPERTY(Config, EditAnywhere, Category = "Flow")
	int32 MinPlayersToStart = 2;

	UPROPERTY(Config, EditAnywhere, Category = "Flow", meta = (Units = "s"))
	float RoleRevealSeconds = 7.f;

	/** The story at the start of a round (UI/HCIntroWidget, its length comes from the story pages). */
	UPROPERTY(Config, EditAnywhere, Category = "Flow")
	bool bIntroEnabled = true;

	UPROPERTY(Config, EditAnywhere, Category = "Flow", meta = (Units = "s"))
	float GameOverScreenSeconds = 15.f;

	UPROPERTY(Config, EditAnywhere, Category = "Flow", meta = (Units = "s"))
	float AllPlayersLoadedTimeoutSeconds = 60.f;

	UPROPERTY(Config, EditAnywhere, Category = "Flow")
	FName DefaultModeId = TEXT("Infiltration");

	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FString MainMenuMap = TEXT("/Game/HowCouldItBeYou/Maps/Map_MainMenu");

	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FString LobbyMap = TEXT("/Game/HowCouldItBeYou/Maps/Map_Lobby");

	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FString MatchMap = TEXT("/Game/HowCouldItBeYou/Maps/Kepler7/Map_Kepler7");

	/** Clients may run state-changing hc.* commands in packaged Development builds (always allowed in the editor). */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bAllowClientDebugCommands = false;

	// --- Kill, report, ghosts (docs/02 §7, §17) ---
	UPROPERTY(Config, EditAnywhere, Category = "Kill", meta = (Units = "cm"))
	float KillRange = 200.f;

	UPROPERTY(Config, EditAnywhere, Category = "Kill", meta = (Units = "cm"))
	float ReportRange = 250.f;

	/** Extra distance the server allows for ping when it checks a request. */
	UPROPERTY(Config, EditAnywhere, Category = "Kill", meta = (Units = "cm"))
	float ServerRangeTolerance = 50.f;

	UPROPERTY(Config, EditAnywhere, Category = "Kill", meta = (Units = "s"))
	float JumpscareTurnTime = 0.2f;

	/**
	 * After this time the server always turns the victim into a ghost, even without an answer. Must be longer than
	 * the longest scare (AHCJumpscareDirector::DurationOf, 2.75 s) plus the ping.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Kill", meta = (Units = "s"))
	float JumpscareServerFailsafeSeconds = 4.f;

	/** After this time at the latest the victim's PC ends the jumpscare by itself. */
	UPROPERTY(Config, EditAnywhere, Category = "Kill", meta = (Units = "s"))
	float JumpscareClientFailsafeSeconds = 5.f;

	UPROPERTY(Config, EditAnywhere, Category = "Kill", meta = (Units = "cm/s"))
	float GhostFlySpeed = 450.f;

	// --- Doors ---
	UPROPERTY(Config, EditAnywhere, Category = "Doors", meta = (Units = "s"))
	float DoorOpenSeconds = 0.4f;

	/** Delay after the last player left the trigger before a door closes. */
	UPROPERTY(Config, EditAnywhere, Category = "Doors", meta = (Units = "s"))
	float DoorCloseDelay = 0.5f;
};
