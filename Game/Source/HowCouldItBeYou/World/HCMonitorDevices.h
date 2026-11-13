#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCMonitorDevices.generated.h"

class AHCCharacter;
class AHCSecurityCamera;
class AHCStationBuilder;
class APlayerController;
class UHCDeviceViewWidget;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UStaticMeshComponent;
class UTexture;

/** Unlit materials for screens and glowing parts (the engine's widget material, so no content asset is needed). */
namespace HCScreens
{
	/** A glowing surface in one colour (values above 1 bloom). */
	HOWCOULDITBEYOU_API UMaterialInstanceDynamic* MakeGlow(UObject* Outer, const FLinearColor& Color);
	HOWCOULDITBEYOU_API void SetGlowColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color);
	/** A screen that shows a texture (a camera picture); Tint multiplies it. */
	HOWCOULDITBEYOU_API void SetScreenTexture(UMaterialInstanceDynamic* Material, UTexture* Texture, const FLinearColor& Tint = FLinearColor::White);
}

/**
 * Something a living player uses with [E] to look at a screen instead of the world: the camera monitors and the
 * admin table (Among Us "Security" and "Admin"). While looking the player stands still - and is easy prey. The
 * server keeps the list of who looks (replicated as a count); it stops when the player closes the view, dies, is
 * pushed away, the round phase changes or the device's sector loses power.
 */
UCLASS(Abstract)
class HOWCOULDITBEYOU_API AHCMonitorDevice : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCMonitorDevice();

	/** How many players look at this device right now (all machines). */
	int32 GetViewerCount() const { return ViewerCount; }
	/** The sector of the device has power (all machines). */
	bool IsPowered() const;
	/** Where a player stands to use it. */
	FVector GetStandLocation() const;

	/** Local: the full-screen view for the owner of PC. */
	virtual UHCDeviceViewWidget* CreateView(APlayerController* PC) PURE_VIRTUAL(AHCMonitorDevice::CreateView, return nullptr;);

	/** Server: the player closed the view (or the server ends it). */
	void ServerRemoveViewer(AHCCharacter* Who, bool bTellClient);

	// IHCInteractable
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override { return FText::GetEmpty(); }
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual UPrimitiveComponent* GetHighlightComponent() const override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static AHCStationBuilder* FindBuilder(const UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	/** Server, twice a second: drops viewers that may not look any more. */
	virtual void ServerTick();
	bool IsViewer(const AHCCharacter* Who) const;

	/** Collision and interaction target; the visible parts are built by the subclasses. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Block;

	UPROPERTY(Replicated)
	uint8 ViewerCount = 0;

	TArray<TWeakObjectPtr<AHCCharacter>> Viewers;
	FTimerHandle ServerTimer;
	/** Distance from the actor origin to where the player stands, along +X. */
	float StandDistance = 120.f;
};

/**
 * The desk with the camera monitors in the security room. Its own four screens show the cameras too - a few
 * frames a second, only on machines whose player is close by.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCSecurityConsole : public AHCMonitorDevice
{
	GENERATED_BODY()

public:
	AHCSecurityConsole();

	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual UHCDeviceViewWidget* CreateView(APlayerController* PC) override;

protected:
	virtual void BeginPlay() override;
	void UpdateScreens();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UStaticMeshComponent>> Screens;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ScreenMaterials;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> ScreenGlow;

	FTimerHandle ScreenTimer;
	int32 NextCapture = 0;
	bool bShowsPictures = false;
};

/**
 * The plan table in the admin room: a glowing model of the ship over a round table. Looking at it shows how many
 * people are in each room - never who. Bodies count like people (Among Us); a room without power shows nothing.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCAdminTable : public AHCMonitorDevice
{
	GENERATED_BODY()

public:
	AHCAdminTable();

	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual UHCDeviceViewWidget* CreateView(APlayerController* PC) override;

	/** People (and bodies) per space of the layout; corridors are not counted. Empty until somebody looks. */
	const TArray<uint8>& GetRoomCounts() const { return RoomCounts; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void ServerTick() override;
	void BuildHologram();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> TableTop;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Rim;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> HoloLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> HoloParts;

	UPROPERTY(Replicated)
	TArray<uint8> RoomCounts;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoloRoom;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoloCorridor;
	FTimerHandle HoloTimer;
	void UpdateHologram();
};
