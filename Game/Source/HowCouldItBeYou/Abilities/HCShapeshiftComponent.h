#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/HCTypes.h"
#include "HCShapeshiftComponent.generated.h"

class AHCPlayerState;

/**
 * Shapeshifting (docs/02 §6.3). Every character has this component, crew included, so nobody can tell the
 * monster by the build of a figure. It only ever does something for the monster.
 *
 * Ready -> Transforming (1.5 s, standing still) -> Disguised (45 s) -> Reverting (1.5 s) -> cooldown (30 s).
 * State and times go only to the owner (HUD); everyone else only sees AHCCharacter's appearance change.
 */
UCLASS(ClassGroup = (HC))
class HOWCOULDITBEYOU_API UHCShapeshiftComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHCShapeshiftComponent();

	EHCShapeshiftState GetState() const { return State; }
	/** Transforming or reverting: the figure stands still and can do nothing. */
	bool IsChanging() const { return State == EHCShapeshiftState::Transforming || State == EHCShapeshiftState::Reverting; }
	float GetCooldownRemaining() const;
	/** Seconds left of the transformation or the disguise; 0 = no limit. */
	float GetStateRemaining() const;
	/** Owner only: whose shape the monster wears right now. */
	const FString& GetDisguiseName() const { return DisguiseName; }

	UFUNCTION(Server, Reliable)
	void ServerRequestShapeshift(int32 TargetPlayerId);

	UFUNCTION(Server, Reliable)
	void ServerRequestRevert();

	// --- Server ---
	void ServerSetReadyTime(double ServerTime) { ReadyServerTime = ServerTime; }
	/** Meeting start: every shapeshift ends at once and without any effect (docs/02 §6.6). */
	void ServerEndForMeeting();
	/** A player died or disconnected; matters only if that player is the current target. */
	void ServerNotifyPlayerGone(int32 PlayerId, bool bDisconnected);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	double GetServerNow() const;
	const AHCPlayerState* FindPlayer(int32 PlayerId) const;
	/** Sound, short effect and trail for everyone - only the place, never who (docs/02 §6.4). */
	void PlayEffect() const;
	void FinishTransform();
	void BeginRevert();
	void FinishRevert();
	void RestoreOwnLook();

	UPROPERTY(Replicated)
	EHCShapeshiftState State = EHCShapeshiftState::Ready;

	UPROPERTY(Replicated)
	double ReadyServerTime = 0.0;

	UPROPERTY(Replicated)
	double StateEndServerTime = 0.0;

	UPROPERTY(Replicated)
	FString DisguiseName;

	/** Server only. */
	int32 TargetId = INDEX_NONE;
};
