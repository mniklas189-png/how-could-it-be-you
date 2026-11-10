#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "HCInteractable.generated.h"

class AHCCharacter;

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class HOWCOULDITBEYOU_API UHCInteractable : public UInterface
{
	GENERATED_BODY()
};

/** Everything a player can use with [E] (docs/02 §10.2). */
class HOWCOULDITBEYOU_API IHCInteractable
{
	GENERATED_BODY()

public:
	/** May this player interact right now? Runs on client AND server. */
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const = 0;

	/** Hint text for the HUD, e.g. "Taschenlampe laden". */
	virtual FText GetInteractionText(const AHCCharacter* Who) const = 0;

	/** 0 = instant, > 0 = hold for that many seconds. */
	virtual float GetHoldDuration(const AHCCharacter* Who) const { return 0.f; }

	/** Server only: the interaction was completed. */
	virtual void ExecuteInteraction(AHCCharacter* Who) = 0;

	/** True if CanInteract checks the distance itself (e.g. remains: 250 cm around them instead of the 180 cm ray). */
	virtual bool ChecksRangeItself() const { return false; }

	/** Mesh that gets the highlight outline. */
	virtual UPrimitiveComponent* GetHighlightComponent() const { return nullptr; }
};
