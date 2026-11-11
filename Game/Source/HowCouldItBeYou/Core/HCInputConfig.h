#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HCInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Enhanced Input actions + default key mapping, created in code (docs/02 §3.3 key table),
 * so the prototype needs no IA_/IMC_ assets. Later the assets can replace this one by one.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCInputConfig : public UObject
{
	GENERATED_BODY()

public:
	static UHCInputConfig* Create(UObject* Outer);

	UPROPERTY() TObjectPtr<UInputMappingContext> DefaultContext;

	UPROPERTY() TObjectPtr<UInputAction> Move;
	UPROPERTY() TObjectPtr<UInputAction> Look;
	UPROPERTY() TObjectPtr<UInputAction> Sprint;
	UPROPERTY() TObjectPtr<UInputAction> Flashlight;
	UPROPERTY() TObjectPtr<UInputAction> Interact;
	/** Left mouse button: kill (only does something for the Wandler). */
	UPROPERTY() TObjectPtr<UInputAction> Kill;
	/** Q: shapeshift menu / end the disguise (only does something for the Wandler). */
	UPROPERTY() TObjectPtr<UInputAction> Shapeshift;
	/** M: station map. */
	UPROPERTY() TObjectPtr<UInputAction> Map;
	UPROPERTY() TObjectPtr<UInputAction> TrueForm;
	/** Space / Ctrl: ghost flies up / down. */
	UPROPERTY() TObjectPtr<UInputAction> GhostUp;
	UPROPERTY() TObjectPtr<UInputAction> GhostDown;
	/** Esc: pause overlay (no real pause in multiplayer). */
	UPROPERTY() TObjectPtr<UInputAction> Pause;
	/** Tab in the lobby: open/close the lobby panel (later the task list in a round). */
	UPROPERTY() TObjectPtr<UInputAction> LobbyMenu;
};
