#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCWardrobe.generated.h"

class UStaticMeshComponent;

/**
 * The wardrobe in the lobby: [E] opens the own look (UI/HCWardrobeWidget) - the colour of the jacket, which no two
 * players share, and a few small things (trousers, hair, boots, backpack). The figure turns round and the camera looks
 * at it like a mirror. Only in the lobby; the choice is remembered on this PC and comes along to the next lobby.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCWardrobe : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCWardrobe();

	// IHCInteractable
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override { return Block; }

	/**
	 * Where the camera stands while somebody tries clothes: in front of the player, who has turned round to face the
	 * room, a little to the side so that the window on the right leaves the figure free. Closer (with a wider view)
	 * when a wall is in the way.
	 */
	FTransform GetMirrorCamera(const AActor* Player, float& OutFieldOfView) const;

protected:
	virtual void BeginPlay() override;

	/** Collision and interaction target (100 x 50 x 200 cm); the locker model stands in for it. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Block;
};
