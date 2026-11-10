#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "GameFramework/PlayerState.h"
#include "HCPlayerState.generated.h"

class UHCTaskComponent;

/**
 * Identity of a player; survives death and map changes (docs/02 §4).
 * RoleInfo goes ONLY to the owner; everybody else always sees the default "Crew" (docs/02 §2.5).
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AHCPlayerState();

	/** Personal task list, replicated only to the owner (docs/02 §9.1). */
	UHCTaskComponent* GetTaskComponent() const { return TaskComponent; }

	/** Own role. On other clients this is always Crew - never react to other players' roles on a client. */
	const FHCRoleInfo& GetRoleInfo() const { return RoleInfo; }

	int32 GetIdentity() const { return Identity; }
	bool IsAlive() const { return bIsAlive; }

	// --- Server ---
	void ServerSetRole(EHCRole NewRole, uint8 MatchSerial);
	void ServerSetIdentity(int32 InIdentity);
	void ServerSetAlive(bool bAlive);

	virtual void CopyProperties(APlayerState* PlayerState) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_RoleInfo();

	UPROPERTY(ReplicatedUsing = OnRep_RoleInfo)
	FHCRoleInfo RoleInfo;

	UPROPERTY(Replicated)
	int32 Identity = INDEX_NONE;

	UPROPERTY(Replicated)
	bool bIsAlive = true;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UHCTaskComponent> TaskComponent;
};
