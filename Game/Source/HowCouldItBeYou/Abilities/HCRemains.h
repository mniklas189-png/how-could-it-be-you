#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/HCInteractable.h"
#include "HCRemains.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * What is left of a killed player (docs/02 §7.9): a pool on the floor plus the ID tag with the real name.
 * No collision at all, so remains never block a way. Living players can report them from up to 250 cm.
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCRemains : public AActor, public IHCInteractable
{
	GENERATED_BODY()

public:
	AHCRemains();

	/** Server, before FinishSpawning. */
	void Configure(const FString& InVictimName, int32 InVictimIdentity);

	const FString& GetVictimName() const { return VictimName; }

	/** Server: when the victim died and what the killer looked like (the medic finds it out; never replicated). */
	void ConfigureCause(double InDeathTime, int32 InKillerIdentity, const FString& InKillerName, bool bInTrueForm)
	{
		DeathTime = InDeathTime;
		KillerIdentity = InKillerIdentity;
		KillerName = InKillerName;
		bKillerTrueForm = bInTrueForm;
	}
	double GetDeathTime() const { return DeathTime; }
	int32 GetKillerIdentity() const { return KillerIdentity; }
	const FString& GetKillerName() const { return KillerName; }
	bool WasKillerInTrueForm() const { return bKillerTrueForm; }
	bool IsReported() const { return bReported; }
	void ServerMarkReported();

	// IHCInteractable
	virtual bool CanInteract(const AHCCharacter* Who, FText& OutReason) const override;
	virtual FText GetInteractionText(const AHCCharacter* Who) const override;
	virtual void ExecuteInteraction(AHCCharacter* Who) override;
	virtual bool ChecksRangeItself() const override { return true; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Victim();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Pool;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Tag;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UTextRenderComponent> NameLabel;

	/** The body of the victim in its suit colour (needs the model packs); falls where the victim stood. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(ReplicatedUsing = OnRep_Victim)
	FString VictimName;

	UPROPERTY(ReplicatedUsing = OnRep_Victim)
	int32 VictimIdentity = 0;

	UPROPERTY(Replicated)
	bool bReported = false;

	double DeathTime = 0.0;
	int32 KillerIdentity = INDEX_NONE;
	FString KillerName;
	bool bKillerTrueForm = false;
};
