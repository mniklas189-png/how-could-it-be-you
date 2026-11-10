#include "Abilities/HCJumpscareDirector.h"

#include "Audio/HCProceduralAudio.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCGhostPawn.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameSettings.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/HCHUD.h"
#include "UObject/SoftObjectPath.h"
#include "World/HCGreyboxAssets.h"

namespace
{
	constexpr float LungeStart = 0.12f;
	constexpr float LungeEnd = 0.38f;
	constexpr float FarDistance = 260.f;
	constexpr float NearDistance = 88.f;
}

AHCJumpscareDirector::AHCJumpscareDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;

	FaceRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FaceRoot"));
	RootComponent = FaceRoot;

	UStaticMesh* Cone = Cast<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cone.Cone")).TryLoad());
	auto MakePart = [this](const TCHAR* PartName, UStaticMesh* PartMesh, const FVector& Location, const FVector& Scale)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(PartName);
		Part->SetupAttachment(FaceRoot);
		Part->SetStaticMesh(PartMesh);
		Part->SetRelativeLocation(Location);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetCastShadow(false);
		return Part;
	};
	// +X points at the camera. A long, dark skull with pale eyes and a maw full of teeth.
	Head = MakePart(TEXT("Head"), HCGreybox::Sphere(), FVector::ZeroVector, FVector(0.75f, 0.72f, 1.05f));
	Maw = MakePart(TEXT("Maw"), HCGreybox::Sphere(), FVector(30.f, 0.f, -18.f), FVector(0.2f, 0.36f, 0.06f));
	Eyes.Add(MakePart(TEXT("EyeL"), HCGreybox::Sphere(), FVector(31.f, -12.f, 16.f), FVector(0.07f, 0.09f, 0.05f)));
	Eyes.Add(MakePart(TEXT("EyeR"), HCGreybox::Sphere(), FVector(31.f, 12.f, 16.f), FVector(0.07f, 0.09f, 0.05f)));
	for (int32 i = 0; i < 5; ++i)
	{
		const float Y = (i - 2) * 6.5f;
		UStaticMeshComponent* Upper = MakePart(*FString::Printf(TEXT("UpperTooth%d"), i), Cone, FVector(36.f, Y, -13.f), FVector(0.035f, 0.035f, 0.09f));
		Upper->SetRelativeRotation(FRotator(180.f, 0.f, 0.f)); // tip down
		UpperTeeth.Add(Upper);
		LowerTeeth.Add(MakePart(*FString::Printf(TEXT("LowerTooth%d"), i), Cone, FVector(36.f, Y + 3.f, -24.f), FVector(0.035f, 0.035f, 0.08f)));
	}

	FaceLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FaceLight"));
	FaceLight->SetupAttachment(FaceRoot);
	FaceLight->SetRelativeLocation(FVector(70.f, 0.f, 22.f));
	FaceLight->SetIntensityUnits(ELightUnits::Candelas);
	FaceLight->SetIntensity(1.6f);
	FaceLight->SetAttenuationRadius(220.f);
	FaceLight->SetLightColor(FLinearColor(0.75f, 0.9f, 0.8f));
	FaceLight->SetCastShadows(false);
}

AHCJumpscareDirector* AHCJumpscareDirector::Start(AHCPlayerController* InPC, const FVector& InAttackerLocation, bool bInPreview)
{
	if (!InPC || !InPC->IsLocalController())
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AHCJumpscareDirector* Director = InPC->GetWorld()->SpawnActor<AHCJumpscareDirector>(AHCJumpscareDirector::StaticClass(), FTransform::Identity, Params);
	if (Director)
	{
		Director->Begin(InPC, InAttackerLocation, bInPreview);
	}
	return Director;
}

void AHCJumpscareDirector::Begin(AHCPlayerController* InPC, const FVector& InAttackerLocation, bool bInPreview)
{
	PC = InPC;
	AttackerLocation = InAttackerLocation;
	bPreview = bInPreview;
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	Duration = Settings->JumpscareDuration;
	TurnTime = Settings->JumpscareTurnTime;
	if (const UHCGameInstance* GI = Cast<UHCGameInstance>(InPC->GetGameInstance()))
	{
		Intensity = GI->GetJumpscareIntensity();
	}

	// Colours
	auto Paint = [this](UStaticMeshComponent* Part, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* Material = HCGreybox::MakeColored(this, Color))
		{
			Part->SetMaterial(0, Material);
		}
	};
	Paint(Head, FLinearColor(0.012f, 0.014f, 0.012f));
	Paint(Maw, FLinearColor(0.22f, 0.f, 0.f));
	for (UStaticMeshComponent* Eye : Eyes)
	{
		Paint(Eye, FLinearColor(0.95f, 0.9f, 0.55f));
	}
	for (UStaticMeshComponent* Tooth : UpperTeeth)
	{
		Paint(Tooth, FLinearColor(0.8f, 0.78f, 0.7f));
	}
	for (UStaticMeshComponent* Tooth : LowerTeeth)
	{
		Paint(Tooth, FLinearColor(0.8f, 0.78f, 0.7f));
	}

	if (AHCHUD* HUD = InPC->GetHUD<AHCHUD>())
	{
		HUD->SetJumpscareActive(true);
	}
	// Take the camera and the input (docs/02 §7.4 step 1)
	InPC->SetIgnoreLookInput(true);
	InPC->SetIgnoreMoveInput(true);
	if (AHCCharacter* Victim = Cast<AHCCharacter>(InPC->GetPawn()))
	{
		Camera = Victim->GetCamera();
	}
	if (UCameraComponent* Cam = Camera.Get())
	{
		SavedFOV = Cam->FieldOfView;
		StartRotation = Cam->GetComponentRotation();
		LastCameraLocation = Cam->GetComponentLocation();
		Cam->bUsePawnControlRotation = false;
	}
	else
	{
		FVector ViewLocation;
		InPC->GetPlayerViewPoint(ViewLocation, StartRotation);
		LastCameraLocation = ViewLocation;
	}
	CurrentRotation = StartRotation;
	TargetRotation = (AttackerLocation - LastCameraLocation).Rotation();
	TargetRotation.Roll = 0.f;
	TargetRotation.Pitch = FMath::Clamp(TargetRotation.Pitch, -35.f, 35.f);

	const bool bShowFace = Intensity != EHCJumpscareIntensity::Minimal;
	SetActorHiddenInGame(!bShowFace);
	FaceLight->SetVisibility(bShowFace);
	if (!bShowFace && InPC->PlayerCameraManager)
	{
		// Minimal: no close-up, just a gentle fade with a dull sound
		InPC->PlayerCameraManager->StartCameraFade(0.f, 1.f, Duration, FLinearColor::Black, false, true);
		HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Thump, 0.5f);
		bStingerPlayed = true;
		bFlashed = true;
	}
	UE_LOG(LogHC, Log, TEXT("Jumpscare started (intensity %d, preview %d)"), static_cast<int32>(Intensity), bPreview ? 1 : 0);
	UpdateCameraAndFace(0.f);
}

void AHCJumpscareDirector::UpdateCameraAndFace(float DeltaSeconds)
{
	const float Shake = Intensity == EHCJumpscareIntensity::Full ? 1.f : Intensity == EHCJumpscareIntensity::Reduced ? 0.3f : 0.f;
	UCameraComponent* Cam = Camera.Get();

	// Camera is pulled towards the attacker within TurnTime, then shakes
	const float TurnAlpha = FMath::Clamp(Elapsed / FMath::Max(0.01f, TurnTime), 0.f, 1.f);
	CurrentRotation = FQuat::Slerp(StartRotation.Quaternion(), TargetRotation.Quaternion(), FMath::InterpEaseOut(0.f, 1.f, TurnAlpha, 2.f)).Rotator();
	FRotator ViewRotation = CurrentRotation;
	if (Elapsed > LungeEnd && !bBlack)
	{
		ViewRotation.Roll += FMath::FRandRange(-3.f, 3.f) * Shake;
		ViewRotation.Pitch += FMath::FRandRange(-1.2f, 1.2f) * Shake;
		ViewRotation.Yaw += FMath::FRandRange(-1.2f, 1.2f) * Shake;
	}
	const float LungeAlpha = FMath::Clamp((Elapsed - LungeStart) / (LungeEnd - LungeStart), 0.f, 1.f);
	if (Cam)
	{
		LastCameraLocation = Cam->GetComponentLocation();
		Cam->SetWorldRotation(ViewRotation);
		if (Shake > 0.f)
		{
			Cam->SetFieldOfView(FMath::Lerp(SavedFOV, SavedFOV - 16.f * Shake, FMath::Square(LungeAlpha)));
			// Red tint and tunnel vision
			FPostProcessSettings& PP = Cam->PostProcessSettings;
			PP.bOverride_SceneColorTint = true;
			PP.SceneColorTint = FLinearColor::LerpUsingHSV(FLinearColor::White, FLinearColor(1.f, 0.55f, 0.5f), LungeAlpha);
			PP.bOverride_VignetteIntensity = true;
			PP.VignetteIntensity = FMath::Lerp(0.4f, 1.1f, LungeAlpha);
			PP.bOverride_SceneFringeIntensity = true;
			PP.SceneFringeIntensity = 4.f * LungeAlpha * Shake;
		}
	}

	// The face shoots out of the dark towards the camera, the maw opens
	const float Distance = FMath::Lerp(FarDistance, NearDistance, FMath::InterpEaseIn(0.f, 1.f, LungeAlpha, 2.5f));
	const FVector Forward = ViewRotation.Vector();
	const FVector Jitter = Elapsed > LungeEnd ? FMath::VRand() * 1.6f * Shake : FVector::ZeroVector;
	const FVector FaceLocation = LastCameraLocation + Forward * Distance + FVector(0.f, 0.f, -4.f) + Jitter;
	SetActorLocationAndRotation(FaceLocation, (LastCameraLocation - FaceLocation).Rotation());

	const float MawAlpha = FMath::Clamp((Elapsed - 0.3f) / 0.5f, 0.f, 1.f);
	const float Open = FMath::Lerp(0.06f, 0.42f, MawAlpha);
	Maw->SetRelativeScale3D(FVector(0.2f, 0.36f, Open));
	Maw->SetRelativeLocation(FVector(30.f, 0.f, -18.f - Open * 22.f));
	for (int32 i = 0; i < LowerTeeth.Num(); ++i)
	{
		FVector Location = LowerTeeth[i]->GetRelativeLocation();
		Location.Z = -24.f - Open * 44.f;
		LowerTeeth[i]->SetRelativeLocation(Location);
	}
}

void AHCJumpscareDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bFinished)
	{
		return;
	}
	Elapsed += DeltaSeconds;
	AHCPlayerController* Controller = PC.Get();
	if (!Controller)
	{
		Destroy();
		return;
	}

	if (!bBlack)
	{
		UpdateCameraAndFace(DeltaSeconds);
#if !UE_BUILD_SHIPPING
		const float ShotTimes[3] = { 0.3f, 0.75f, 1.6f };
		if (DebugShots < 3 && Elapsed >= ShotTimes[DebugShots] && FParse::Param(FCommandLine::Get(), TEXT("HCPhaseShots")))
		{
			FString Dir;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir))
			{
				const FString Who = Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : FString(TEXT("player"));
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("jumpscare_%s_%d.png"), *Who, DebugShots)), true, false);
			}
			++DebugShots;
		}
#endif
		if (!bStingerPlayed && Elapsed >= LungeStart)
		{
			bStingerPlayed = true;
			// Reduced: about 12 dB quieter
			HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Stinger, Intensity == EHCJumpscareIntensity::Full ? 1.f : 0.25f);
		}
		if (!bFlashed && Elapsed >= LungeEnd)
		{
			bFlashed = true;
			// Short white flash, only at full intensity
			if (Intensity == EHCJumpscareIntensity::Full && Controller->PlayerCameraManager)
			{
				Controller->PlayerCameraManager->StartCameraFade(0.7f, 0.f, 0.22f, FLinearColor::White, false, false);
			}
		}
		if (Elapsed >= Duration)
		{
			CutToBlack();
		}
		return;
	}

	// Black: wait for the ghost (or just for the end of a preview); never longer than the client failsafe
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const float Failsafe = FMath::Min(Duration + 1.5f, Settings->JumpscareClientFailsafeSeconds);
	const bool bGhostReady = Cast<AHCGhostPawn>(Controller->GetPawn()) != nullptr;
	if ((bPreview && Elapsed >= Duration + 0.6f) || (!bPreview && bGhostReady && Elapsed >= Duration + 0.4f) || Elapsed >= Failsafe)
	{
		if (!bPreview && !bGhostReady)
		{
			UE_LOG(LogHC, Warning, TEXT("HC_FAILSAFE: jumpscare ended by the client failsafe after %.1f s"), Elapsed);
		}
		Finish();
	}
}

void AHCJumpscareDirector::CutToBlack()
{
	bBlack = true;
	SetActorHiddenInGame(true);
	FaceLight->SetVisibility(false);
	AHCPlayerController* Controller = PC.Get();
	if (Controller && Controller->PlayerCameraManager)
	{
		Controller->PlayerCameraManager->StartCameraFade(1.f, 1.f, 0.05f, FLinearColor::Black, false, true);
	}
	if (Controller && !bPreview)
	{
		Controller->ServerJumpscareFinished();
	}
}

void AHCJumpscareDirector::Finish()
{
	bFinished = true;
	AHCPlayerController* Controller = PC.Get();
	// Give everything back (matters for previews and when the failsafe ends a scare the server never finished)
	if (UCameraComponent* Cam = Camera.Get())
	{
		Cam->bUsePawnControlRotation = true;
		Cam->SetFieldOfView(SavedFOV);
		FPostProcessSettings& PP = Cam->PostProcessSettings;
		PP.bOverride_SceneColorTint = false;
		PP.bOverride_VignetteIntensity = false;
		PP.bOverride_SceneFringeIntensity = false;
	}
	if (Controller)
	{
		Controller->ResetIgnoreLookInput();
		Controller->ResetIgnoreMoveInput();
		FRotator View = CurrentRotation;
		View.Roll = 0.f;
		Controller->SetControlRotation(View);
		if (Controller->PlayerCameraManager)
		{
			Controller->PlayerCameraManager->StartCameraFade(1.f, 0.f, 0.9f, FLinearColor::Black, false, false);
		}
		if (AHCHUD* HUD = Controller->GetHUD<AHCHUD>())
		{
			HUD->SetJumpscareActive(false);
		}
		if (!bPreview)
		{
			if (AHCHUD* HUD = Controller->GetHUD<AHCHUD>())
			{
				HUD->ShowCenterMessage(NSLOCTEXT("HC", "YouDied", "Du bist tot. Du bist jetzt ein Geist."), 5.f);
			}
		}
	}
	Destroy();
}
