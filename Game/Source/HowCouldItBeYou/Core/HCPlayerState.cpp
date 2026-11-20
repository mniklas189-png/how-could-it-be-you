#include "Core/HCPlayerState.h"

#include "Core/HCLog.h"
#include "Net/UnrealNetwork.h"
#include "Tasks/HCTaskComponent.h"

AHCPlayerState::AHCPlayerState()
{
	TaskComponent = CreateDefaultSubobject<UHCTaskComponent>(TEXT("Tasks"));
}

void AHCPlayerState::ServerSetRole(EHCRole NewRole, uint8 MatchSerial, EHCJob Job, int32 PartnerId)
{
	check(HasAuthority());
	RoleInfo.Role = NewRole;
	RoleInfo.MatchSerial = MatchSerial;
	RoleInfo.Job = Job;
	RoleInfo.PartnerId = PartnerId;
	bCommander = NewRole == EHCRole::Crew && Job == EHCJob::Commander;
	JobState = FHCJobState();
	OnRep_RoleInfo();
}

void AHCPlayerState::ServerSetIdentity(int32 InIdentity)
{
	check(HasAuthority());
	Identity = InIdentity;
}

void AHCPlayerState::ServerSetAlive(bool bAlive)
{
	check(HasAuthority());
	bIsAlive = bAlive;
}

void AHCPlayerState::OnRep_RoleInfo()
{
	// The HUD shows the role screen once phase RoleReveal AND this serial have arrived (docs/02 §5.4).
}

void AHCPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	// Seamless travel: the identity chosen in the lobby stays; roles and alive state are reset per match.
	if (AHCPlayerState* Target = Cast<AHCPlayerState>(PlayerState))
	{
		Target->Identity = Identity;
		Target->Outfit = Outfit;
	}
}

void AHCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AHCPlayerState, RoleInfo, COND_OwnerOnly);
	DOREPLIFETIME(AHCPlayerState, Identity);
	DOREPLIFETIME(AHCPlayerState, Outfit);
	DOREPLIFETIME(AHCPlayerState, bCommander);
	DOREPLIFETIME_CONDITION(AHCPlayerState, JobState, COND_OwnerOnly);
	DOREPLIFETIME(AHCPlayerState, bIsAlive);
	DOREPLIFETIME(AHCPlayerState, bHasVoted);
	DOREPLIFETIME(AHCPlayerState, EmergencyMeetingsLeft);
	DOREPLIFETIME_CONDITION(AHCPlayerState, SabotageEarliestTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AHCPlayerState, PowerSabotageReadyTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AHCPlayerState, DoorSabotageReadyTime, COND_OwnerOnly);
}
