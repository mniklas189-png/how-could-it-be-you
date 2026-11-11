#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HCMenuGameMode.generated.h"

class ACameraActor;
class UHCMainMenuWidget;

/** Game mode of Map_MainMenu: no pawn, only the main menu widget (set as override in the map's World Settings). */
UCLASS()
class HOWCOULDITBEYOU_API AHCMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AHCMenuGameMode();
};

/** Shows the main menu and switches to mouse input. */
UCLASS()
class HOWCOULDITBEYOU_API AHCMenuHUD : public AHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** A quiet room of the station with a few figures behind the menu, built at runtime (the menu map stays empty). */
	void BuildBackdrop(APlayerController* PC);

	TWeakObjectPtr<ACameraActor> MenuCamera;

	UPROPERTY(Transient)
	TObjectPtr<UHCMainMenuWidget> Menu;

	int32 AutoJoinAttempts = 0;
	/** -HCSettingsFlipLanguage: the saved language to put back before quitting. */
	FString FlipBackLanguage;
};
