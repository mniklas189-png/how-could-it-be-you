#include "Characters/HCUneaseComponent.h"

#include "Audio/HCProceduralAudio.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Core/HCGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sabotage/HCPowerSector.h"
#include "World/HCStationBuilder.h"

float FHCUnease::Compute(const FHCUneaseInput& In)
{
	float Value = 0.1f;
	if (!In.bSectorPowered)
	{
		Value += In.bFlashlightOn ? 0.35f : 0.5f;
	}
	if (In.VisiblePlayers <= 0)
	{
		Value += 0.3f; // alone
	}
	else if (In.VisiblePlayers >= 2)
	{
		Value -= 0.05f;
	}
	return FMath::Clamp(Value, 0.f, 1.f);
}

UHCUneaseComponent::UHCUneaseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

float UHCUneaseComponent::ComputeTarget() const
{
	const AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	if (!Character || !GS || GS->GetMatchPhase() != EHCMatchPhase::Playing)
	{
		return 0.f;
	}
	FHCUneaseInput In;
	In.bFlashlightOn = Character->IsFlashlightOn();
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		const int32 SectorIndex = It->GetSectorIndexAt(Character->GetActorLocation());
		const AHCPowerSector* Sector = SectorIndex >= 0 ? AHCPowerSector::Find(GetWorld(), static_cast<uint8>(SectorIndex)) : nullptr;
		In.bSectorPowered = !Sector || Sector->IsPowered();
	}
	const FVector Eyes = Character->GetCamera()->GetComponentLocation();
	for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Character || !It->IsAlive() || FVector::DistSquared(It->GetActorLocation(), Eyes) > FMath::Square(1500.f))
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(HCUneaseSight), false, Character);
		Params.AddIgnoredActor(*It);
		if (!GetWorld()->LineTraceTestByChannel(Eyes, It->GetActorLocation() + FVector(0.f, 0.f, 40.f), ECC_Visibility, Params))
		{
			++In.VisiblePlayers;
		}
	}
	return FHCUnease::Compute(In);
}

void UHCUneaseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AHCCharacter* Character = Cast<AHCCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled())
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextSampleTime)
	{
		NextSampleTime = Now + 0.25f;
		Target = ComputeTarget();
	}
	// Rises within about two seconds, fades a little slower
	Unease = FMath::FInterpConstantTo(Unease, Target, DeltaTime, Target > Unease ? 0.5f : 0.3f);

	UCameraComponent* Camera = Character->GetCamera();
	Camera->PostProcessBlendWeight = 1.f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.5f + 0.55f * Unease;

	if (Unease > 0.55f && Now >= NextBeatTime)
	{
		NextBeatTime = Now + FMath::Lerp(1.3f, 0.7f, Unease);
		HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Thump, 0.08f + 0.22f * Unease);
	}
}
