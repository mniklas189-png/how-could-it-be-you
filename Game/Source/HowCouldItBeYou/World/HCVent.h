#pragma once

#include "CoreMinimal.h"
#include "World/HCMonitorDevices.h"
#include "HCVent.generated.h"

class UCameraComponent;
class USceneComponent;

/**
 * A hatch of the ventilation shafts in the floor (Among Us "vent"). Only the Wandler can use it: holding [E] next to
 * it lifts the grille and the figure is gone - hidden for everybody, unable to walk, kill or be seen on a camera.
 * From inside it looks out at floor level, crawls to the other hatches of the same shaft and climbs out again.
 *
 * What the crew gets: the grille visibly jumps open and clangs shut whenever somebody goes in or out, and a crawl
 * scrapes behind the walls at both ends. Who sees the Wandler climb out has caught it.
 *
 * Built on the monitor devices (camera desk, plan table): going in is "looking at a device", so the player
 * controller, the HUD and the view widget work the same way. The kill cooldown stands still inside a shaft.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCVent : public AHCMonitorDevice
{
	GENERATED_BODY()

public:
	AHCVent();

	/** Server, before the spawn is finished. */
	void Configure(FName InId, int32 InGroup);

	FName GetVentId() const { return VentId; }
	int32 GetGroup() const { return Group; }
	/** The room the hatch lies in (layout id), None outside. */
	FName GetRoomId() const;
	/** The other hatches of the same shaft, nearest first. */
	void GetLinkedVents(TArray<AHCVent*>& OutLinked) const;

	/** Seconds a crawl between two hatches takes: much faster than walking, but not at once. The technician is slower. */
	static float CrawlSeconds(const AHCVent* From, const AHCVent* To, bool bTechnician = false);
	/** May this figure use the shafts at all (the Wandler, or a technician)? Client: only for the own figure. */
	static bool MayUseShafts(const AHCCharacter* Who);
	/** The technician crawls this much slower than the Wandler, and is heard further. */
	static constexpr float TechnicianSlowdown = 1.8f;
	/** Local: the nearest hatch this character could climb into right now (only ever one for the Wandler), or null. */
	static AHCVent* FindUsable(const AHCCharacter* Who);
	/** Server: the hatch a character is hidden in, or null. */
	static AHCVent* FindVentOf(const UWorld* World, const AHCCharacter* Who);

	/** Local, from inside: turn the head. */
	void AddViewYaw(float Degrees);
	void ResetView();

	// AHCMonitorDevice / IHCInteractable
	virtual bool IsPowered() const override { return true; }
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual float GetHoldDuration(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual bool ChecksRangeItself() const override { return true; }
	virtual UPrimitiveComponent* GetHighlightComponent() const override;
	virtual UHCDeviceViewWidget* CreateView(APlayerController* PC) override;
	virtual void ServerRemoveViewer(AHCCharacter* Who, bool bTellClient) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Everybody: the grille jumps open and falls shut (somebody went in or came out). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFlap();

	/** Everybody near: something crawls through the shaft below this hatch. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastRattle(bool bLoud);

protected:
	virtual void BeginPlay() override;
	virtual void ServerTick() override;

	struct FOccupant
	{
		TWeakObjectPtr<AHCCharacter> Who;
		/** Server time from which the occupant may crawl on or climb out (the crawl here takes that long). */
		double ReadyAt = 0.0;
		/** Server time it went into the shaft (any hatch): the kill cooldown is pushed back by the time spent inside. */
		double EnteredAt = 0.0;
		/** Wants out, but is still crawling. */
		bool bLeaving = false;
		bool bTellClient = false;
	};

	double ServerNow() const;
	int32 FindOccupant(const AHCCharacter* Who) const;
	void ServerEnter(AHCCharacter* Who);
	void ServerMoveTo(AHCCharacter* Who, AHCVent* Target);
	void ServerExit(int32 OccupantIndex, bool bTellClient);
	void PlaceOccupant(AHCCharacter* Who) const;

	UPROPERTY(Replicated)
	FName VentId;

	UPROPERTY(Replicated)
	int32 Group = 0;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Frame;

	/** The hinge of the grille: on the side away from where an occupant looks. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> Hinge;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Grille;

	/** The eyes of whoever hides inside: just above the floor. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UCameraComponent> Eyes;

	TArray<FOccupant> Occupants;
	/** Server: when a character last climbed out here (a short lock against going in and out without end). */
	TMap<TWeakObjectPtr<AHCCharacter>, double> LastExit;

	/** Local: world time the grille started to jump, < 0 = at rest. */
	float FlapStart = -1.f;
	float ViewYaw = 0.f;
	/** Local: this hatch is the view of the player on this machine. */
	bool bLocalView = false;
};
