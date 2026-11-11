#include "Sabotage/HCSabotageManager.h"

#include "Characters/HCCharacter.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCMatchRules.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sabotage/HCDoor.h"
#include "Sabotage/HCPowerSector.h"
#include "TimerManager.h"
#include "World/HCStationBuilder.h"

AHCSabotageManager::AHCSabotageManager()
{
	bReplicates = false;
	PrimaryActorTick.bCanEverTick = false;
}

void AHCSabotageManager::Init(AHCGameMode* InGameMode)
{
	GameMode = InGameMode;
	GetWorldTimerManager().SetTimer(TickTimer, this, &AHCSabotageManager::TickSabotage, 0.25f, true);
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		bLayoutSafeForLocks = FHCStationGrid(It->GetLayout()).Validate().IsEmpty();
	}
}

double AHCSabotageManager::GetNow() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : 0.0;
}

FName AHCSabotageManager::SectorTargetId(uint8 SectorIndex)
{
	switch (static_cast<EHCSector>(SectorIndex))
	{
	case EHCSector::North: return TEXT("North");
	case EHCSector::East: return TEXT("East");
	case EHCSector::SouthWest: return TEXT("SouthWest");
	default: return TEXT("Command");
	}
}

bool AHCSabotageManager::ParseSector(const FString& Text, uint8& OutSectorIndex)
{
	const FString T = Text.ToLower();
	if (T == TEXT("north") || T == TEXT("nord") || T == TEXT("n") || T == TEXT("1"))
	{
		OutSectorIndex = static_cast<uint8>(EHCSector::North);
		return true;
	}
	if (T == TEXT("east") || T == TEXT("ost") || T == TEXT("o") || T == TEXT("e") || T == TEXT("2"))
	{
		OutSectorIndex = static_cast<uint8>(EHCSector::East);
		return true;
	}
	if (T == TEXT("southwest") || T == TEXT("sw") || T == TEXT("suedwest") || T == TEXT("süd-west") || T == TEXT("3"))
	{
		OutSectorIndex = static_cast<uint8>(EHCSector::SouthWest);
		return true;
	}
	return false;
}

AHCDoor* AHCSabotageManager::FindDoor(FName DoorId) const
{
	for (TActorIterator<AHCDoor> It(GetWorld()); It; ++It)
	{
		if (It->DoorId == DoorId)
		{
			return *It;
		}
	}
	return nullptr;
}

bool AHCSabotageManager::IsAnySectorDark() const
{
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		if (!It->IsPowered())
		{
			return true;
		}
	}
	return false;
}

void AHCSabotageManager::PublishTimes() const
{
	// Only the Wandler gets the times, for its own map (docs/02 §11.1)
	for (AHCPlayerState* PS : GameMode->GetHCPlayerStates())
	{
		const bool bMonster = PS->GetRoleInfo().Role == EHCRole::Monster;
		PS->ServerSetSabotageTimes(bMonster ? EarliestTime : 0.0, bMonster ? PowerReadyTime : 0.0, bMonster ? DoorReadyTime : 0.0);
	}
}

void AHCSabotageManager::ResetForRound()
{
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		It->ServerSetPowered(true, 0.0);
	}
	for (TActorIterator<AHCDoor> It(GetWorld()); It; ++It)
	{
		It->ServerUnlock();
	}
	PendingDoor.Reset();
	LockedDoor.Reset();
	EarliestTime = PowerReadyTime = DoorReadyTime = 0.0;
	PublishTimes();
}

void AHCSabotageManager::OnPlayResumed()
{
	const UHCMatchRules* Rules = GameMode->GetRules();
	EarliestTime = GetNow() + (Rules ? Rules->SabotageStartDelay : 30.f);
	PublishTimes();
}

void AHCSabotageManager::OnMeetingStart()
{
	if (AHCDoor* Door = PendingDoor.Get())
	{
		Door->ServerUnlock();
	}
	if (AHCDoor* Door = LockedDoor.Get())
	{
		Door->ServerUnlock();
	}
}

EHCSabotageReject AHCSabotageManager::RequestSabotage(const AHCPlayerState* Who, EHCSabotageType Type, FName TargetId, AHCPlayerController* Requester)
{
	FHCSabotageCheck Check;
	Check.bPhasePlaying = GameMode->GetPhase() == EHCMatchPhase::Playing;
	Check.bIsMonster = Who && Who->GetRoleInfo().Role == EHCRole::Monster;
	Check.bAlive = Who && Who->IsAlive();
	Check.Now = GetNow();
	Check.EarliestTime = EarliestTime;

	AHCPowerSector* Sector = nullptr;
	AHCDoor* Door = nullptr;
	if (Type == EHCSabotageType::PowerOutage)
	{
		uint8 SectorIndex = 0;
		if (ParseSector(TargetId.ToString(), SectorIndex))
		{
			Sector = AHCPowerSector::Find(GetWorld(), SectorIndex);
		}
		Check.bTargetValid = Sector != nullptr;
		Check.bCommandCenter = Sector && Sector->GetSectorIndex() == static_cast<uint8>(EHCSector::Command);
		Check.bTypeActive = IsAnySectorDark();
		Check.ReadyTime = PowerReadyTime;
	}
	else
	{
		Door = FindDoor(TargetId);
		Check.bTargetValid = Door && Door->bSabotageable;
		Check.bTypeActive = PendingDoor.IsValid() || LockedDoor.IsValid();
		Check.ReadyTime = DoorReadyTime;
		// One lock at a time, and the map check proved every room stays reachable even with all of them locked
		Check.bPathStaysOpen = bLayoutSafeForLocks;
	}
	const EHCSabotageReject Reject = FHCSabotageRules::Validate(Check);
	if (Reject != EHCSabotageReject::None)
	{
		UE_LOG(LogHCMatch, Log, TEXT("Sabotage request of %s rejected: %s"), Who ? *Who->GetPlayerName() : TEXT("?"), FHCSabotageRules::ToString(Reject));
		return Reject;
	}
	if (Sector)
	{
		StartOutage(Sector);
	}
	else
	{
		const UHCMatchRules* Rules = GameMode->GetRules();
		Door->ServerRequestLock(Rules ? Rules->DoorLockDuration : 10.f);
		PendingDoor = Door;
		DoorRequester = Requester;
	}
	return EHCSabotageReject::None;
}

bool AHCSabotageManager::ForceSabotage(EHCSabotageType Type, FName TargetId)
{
	if (Type == EHCSabotageType::PowerOutage)
	{
		uint8 SectorIndex = 0;
		AHCPowerSector* Sector = ParseSector(TargetId.ToString(), SectorIndex) ? AHCPowerSector::Find(GetWorld(), SectorIndex) : nullptr;
		if (Sector && Sector->IsPowered())
		{
			StartOutage(Sector);
		}
		return Sector != nullptr;
	}
	AHCDoor* Door = FindDoor(TargetId);
	if (Door && !PendingDoor.IsValid() && !LockedDoor.IsValid())
	{
		const UHCMatchRules* Rules = GameMode->GetRules();
		Door->ServerRequestLock(Rules ? Rules->DoorLockDuration : 10.f);
		PendingDoor = Door;
		DoorRequester.Reset();
	}
	return Door != nullptr;
}

void AHCSabotageManager::StartOutage(AHCPowerSector* Sector)
{
	const UHCMatchRules* Rules = GameMode->GetRules();
	Sector->ServerSetPowered(false, GetNow() + (Rules ? Rules->PowerOutageMaxDuration : 90.f));
	UE_LOG(LogHCMatch, Log, TEXT("Sabotage: power outage in sector %s"), *SectorTargetId(Sector->GetSectorIndex()).ToString());
	// Announced for everyone, without a word about who did it (docs/01 §7 rule 6)
	if (AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>())
	{
		GS->MulticastSabotageAlert(FText::Format(NSLOCTEXT("HC", "AlertOutage", "STROMAUSFALL – SEKTOR {0}"), AHCPowerSector::GetDisplayName(Sector->GetSectorIndex()).ToUpper()));
	}
}

void AHCSabotageManager::RepairPower(uint8 SectorIndex, const AHCCharacter* Who)
{
	AHCPowerSector* Sector = AHCPowerSector::Find(GetWorld(), SectorIndex);
	if (!Sector || Sector->IsPowered())
	{
		return;
	}
	const UHCMatchRules* Rules = GameMode->GetRules();
	Sector->ServerSetPowered(true, 0.0);
	// The cooldown counts from the end of the outage (docs/01 §7)
	PowerReadyTime = GetNow() + (Rules ? Rules->PowerSabotageCooldown : 45.f);
	PublishTimes();
	const APlayerState* PS = Who ? Who->GetPlayerState() : nullptr;
	UE_LOG(LogHCMatch, Log, TEXT("Sabotage: power in sector %s is back (%s)"), *SectorTargetId(SectorIndex).ToString(),
		PS ? *FString::Printf(TEXT("repaired by %s"), *PS->GetPlayerName()) : TEXT("emergency power"));
	if (AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>())
	{
		GS->MulticastAnnouncement(FText::Format(Who ? NSLOCTEXT("HC", "PowerRepaired", "Strom in Sektor {0} wiederhergestellt.")
			: NSLOCTEXT("HC", "PowerAuto", "Notstrom in Sektor {0} angelaufen."), AHCPowerSector::GetDisplayName(SectorIndex)), 5.f);
	}
}

void AHCSabotageManager::TickSabotage()
{
	if (!GameMode)
	{
		return;
	}
	// No sector stays dark for ever: the emergency power comes back by itself (docs/02 §11.2)
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		if (!It->IsPowered() && It->GetSecondsUntilAutoRestore() <= 0.f)
		{
			RepairPower(It->GetSectorIndex(), nullptr);
		}
	}
	const UHCMatchRules* Rules = GameMode->GetRules();
	if (AHCDoor* Door = PendingDoor.Get())
	{
		if (Door->GetDoorState() == EHCDoorState::Locked)
		{
			LockedDoor = Door;
			PendingDoor.Reset();
			UE_LOG(LogHCMatch, Log, TEXT("Sabotage: door %s locked"), *Door->DoorId.ToString());
			if (AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>())
			{
				GS->MulticastSabotageAlert(FText::Format(NSLOCTEXT("HC", "AlertDoor", "TÜR VERRIEGELT – {0}"), Door->GetDisplayName().ToUpper()));
			}
		}
		else if (!Door->IsLockPending())
		{
			// Someone stood in the frame for 2 s: failed, and the cooldown is not used up (docs/01 §7 rule 3)
			PendingDoor.Reset();
			UE_LOG(LogHCMatch, Log, TEXT("Sabotage: door %s could not be locked, the frame was occupied"), *Door->DoorId.ToString());
			if (AHCPlayerController* PC = DoorRequester.Get())
			{
				PC->ClientShowMessage(NSLOCTEXT("HC", "DoorBlocked", "Jemand steht in der Tür – Sabotage fehlgeschlagen."));
			}
		}
	}
	if (AHCDoor* Door = LockedDoor.Get())
	{
		if (Door->GetDoorState() != EHCDoorState::Locked)
		{
			LockedDoor.Reset();
			DoorReadyTime = GetNow() + (Rules ? Rules->DoorSabotageCooldown : 30.f);
			PublishTimes();
			UE_LOG(LogHCMatch, Log, TEXT("Sabotage: door %s is open again"), *Door->DoorId.ToString());
		}
	}
}
