#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "GameFramework/Info.h"
#include "HCJobManager.generated.h"

class AHCCharacter;
class AHCGameMode;
class AHCPlayerController;
class AHCPlayerState;
class AHCRemains;
class AHCSecurityConsole;

/** What the ability key asks for (AHCPlayerController::ServerUseAbility). */
enum class EHCAbilityAction : uint8
{
	/** The ability key: examine remains (medic), camera tablet (guard), UV light (biologist). */
	Use,
	/** Radio operator: an announcement to everybody (the text comes along). */
	RadioAnnounce,
	/** Radio operator: an emergency meeting from wherever the operator is. */
	RadioMeeting
};

/**
 * The jobs on the server (Roles/HCJobs.h): who may do what right now, the medic's examinations, the guard's tablet
 * time, the radio operator's charge. Exists only on the server and is not replicated; what the owner sees lives in
 * AHCPlayerState (RoleInfo.Job, JobState). The technician's shafts and fuse boxes and the commander's vote are checked
 * where they happen (AHCVent, AHCFuseBox, AHCMeetingManager).
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCJobManager : public AInfo
{
	GENERATED_BODY()

public:
	AHCJobManager();

	void Init(AHCGameMode* InGameMode);

	/** Round start and the end of every meeting: charges and tablet time are full again. */
	void OnPlayResumed();
	/** A meeting starts: examinations stop, tablets close. */
	void OnMeetingStart();
	/** Ability key of a player. */
	void UseAbility(AHCPlayerController* PC, EHCAbilityAction Action, const FString& Text);

	/** Does this player really have this job right now (a living crew member; a Wandler's cover never counts)? */
	static bool HasJob(const AHCPlayerState* PS, EHCJob Job);
	/** The same for a figure on the station (the server knows the roles). */
	static bool HasJob(const AHCCharacter* Who, EHCJob Job);

	/** Seconds of camera tablet the guard has between two meetings. */
	static constexpr float TabletSeconds = 15.f;
	/** How long the medic kneels at the remains. */
	static constexpr float ExamineSeconds = 3.f;
	/** How far the medic may be from the remains, cm. */
	static constexpr float ExamineRange = 260.f;

private:
	void TickJobs();
	void Notice(AHCPlayerController* PC, const FText& Text, float Seconds = 6.f) const;
	void StartExamination(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who);
	void FinishExamination(int32 Index);
	void OpenTablet(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who);
	void ToggleUV(AHCPlayerController* PC, AHCCharacter* Who);
	void Announce(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who, const FString& Text);
	void RemoteMeeting(AHCPlayerController* PC, AHCPlayerState* PS, AHCCharacter* Who);
	double GetNow() const;

	UPROPERTY(Transient)
	TObjectPtr<AHCGameMode> GameMode;

	struct FExamination
	{
		TWeakObjectPtr<AHCPlayerState> Who;
		TWeakObjectPtr<AHCCharacter> Character;
		TWeakObjectPtr<AHCRemains> Remains;
		FVector Start = FVector::ZeroVector;
		double End = 0.0;
	};
	TArray<FExamination> Examinations;

	struct FTablet
	{
		TWeakObjectPtr<AHCPlayerState> Who;
		TWeakObjectPtr<AHCCharacter> Character;
		TWeakObjectPtr<AHCSecurityConsole> Console;
	};
	TArray<FTablet> Tablets;

	FTimerHandle TickTimer;
	double LastTick = 0.0;
};
