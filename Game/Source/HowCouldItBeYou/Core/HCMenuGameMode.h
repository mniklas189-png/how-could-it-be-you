#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HCMenuGameMode.generated.h"

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

	UPROPERTY(Transient)
	TObjectPtr<UHCMainMenuWidget> Menu;

	int32 AutoJoinAttempts = 0;
};
