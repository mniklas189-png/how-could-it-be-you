#include "Sabotage/HCPowerSector.h"

#include "Audio/HCProceduralAudio.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "World/HCStationBuilder.h"

AHCPowerSector::AHCPowerSector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
}

void AHCPowerSector::BeginPlay()
{
	Super::BeginPlay();
	// A client that sees the sector for the first time gets the right lights without the flicker
	ApplyLights(bPowered ? 1.f : 0.f, !bPowered);
	bAppliedOnce = true;
}

float AHCPowerSector::GetSecondsUntilAutoRestore() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return bPowered || !GS ? 0.f : static_cast<float>(FMath::Max(0.0, RestoreServerTime - GS->GetServerWorldTimeSeconds()));
}

void AHCPowerSector::ServerSetPowered(bool bInPowered, double AutoRestoreServerTime)
{
	RestoreServerTime = bInPowered ? 0.0 : AutoRestoreServerTime;
	if (bPowered != bInPowered)
	{
		bPowered = bInPowered;
		OnRep_Powered(); // listen server: the rep notify does not run on the server in C++
	}
}

void AHCPowerSector::OnRep_Powered()
{
	if (!bAppliedOnce)
	{
		return; // BeginPlay applies the first state
	}
	if (bPowered)
	{
		FlickerLeft = 0.f;
		SetActorTickEnabled(false);
		ApplyLights(1.f, false);
	}
	else
	{
		// Flicker for a second, then dark (docs/02 §11.2)
		FlickerLeft = 1.f;
		SetActorTickEnabled(true);
		HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Thump, 0.35f);
	}
}

void AHCPowerSector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FlickerLeft -= DeltaSeconds;
	if (FlickerLeft <= 0.f || bPowered)
	{
		SetActorTickEnabled(false);
		ApplyLights(bPowered ? 1.f : 0.f, !bPowered);
		return;
	}
	ApplyLights(FMath::FRand() < 0.5f ? 0.9f : 0.08f, false);
}

void AHCPowerSector::ApplyLights(float NormalScale, bool bEmergency) const
{
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		It->SetSectorLights(SectorIndex, NormalScale, bEmergency);
	}
}

FText AHCPowerSector::GetDisplayName(uint8 InSectorIndex)
{
	switch (static_cast<EHCSector>(InSectorIndex))
	{
	case EHCSector::North: return NSLOCTEXT("HC", "SectorNorth", "Nord");
	case EHCSector::East: return NSLOCTEXT("HC", "SectorEast", "Ost");
	case EHCSector::SouthWest: return NSLOCTEXT("HC", "SectorSouthWest", "Süd-West");
	default: return NSLOCTEXT("HC", "SectorCommand", "Kommando");
	}
}

AHCPowerSector* AHCPowerSector::Find(const UWorld* World, uint8 InSectorIndex)
{
	for (TActorIterator<AHCPowerSector> It(World); It; ++It)
	{
		if (It->GetSectorIndex() == InSectorIndex)
		{
			return *It;
		}
	}
	return nullptr;
}

void AHCPowerSector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCPowerSector, SectorIndex);
	DOREPLIFETIME(AHCPowerSector, bPowered);
	DOREPLIFETIME(AHCPowerSector, RestoreServerTime);
}
