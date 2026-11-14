#include "Abilities/HCJumpscareDirector.h"

#include "Animation/AnimSequence.h"
#include "Async/Async.h"
#include "Audio/HCProceduralAudio.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCGhostPawn.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameSettings.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UI/HCHUD.h"
#include "UObject/SoftObjectPath.h"
#include "UnrealClient.h"
#include "World/HCGreyboxAssets.h"
#include "World/HCModelAssets.h"
#include "World/HCStationBuilder.h"

#include <atomic>

// Named namespace on purpose: the unity build puts several .cpp files into one, generic helper names would collide
namespace HCScare
{
	using HCProceduralAudio::ESound;

	constexpr float LungeStart = 0.12f;
	constexpr float LungeEnd = 0.38f;
	constexpr float FarDistance = 260.f;
	constexpr float NearDistance = 74.f;

	/** Lights out: the moment the monster slams into the camera. */
	constexpr float SlamTime = 1.30f;
	/** Lights out: the breaker drops, the station falls silent. */
	constexpr float PowerDies = 0.30f;
	/** Dragged down: the body hits the floor. */
	constexpr float Landing = 0.76f;

	/** Which scare ran last on this PC (never the same one twice in a row). */
	int32 LastStyle = 0;

	/** Time (s, picture time) and power of every strike of the monster. */
	TArrayView<const FVector2f> StrikesOf(int32 Style)
	{
		static const FVector2f Quake[] = { {0.38f, 1.f}, {0.78f, 0.7f}, {1.12f, 0.8f}, {1.45f, 0.85f}, {1.76f, 0.95f}, {2.05f, 1.f} };
		static const FVector2f LightsOut[] = { {SlamTime, 1.1f}, {1.64f, 0.75f}, {1.95f, 0.85f}, {2.25f, 1.f} };
		static const FVector2f DraggedDown[] = { {0.38f, 1.f}, {0.76f, 0.9f}, {1.06f, 0.75f}, {1.38f, 0.8f}, {1.68f, 0.85f}, {1.98f, 0.9f} };
		static const FVector2f SignalLost[] = { {0.38f, 1.f}, {0.95f, 0.75f}, {1.48f, 0.85f}, {1.8f, 0.9f} };
		switch (Style)
		{
		case HCScareStyle::LightsOut: return MakeArrayView(LightsOut);
		case HCScareStyle::DraggedDown: return MakeArrayView(DraggedDown);
		case HCScareStyle::SignalLost: return MakeArrayView(SignalLost);
		default: return MakeArrayView(Quake);
		}
	}

	/** The sounds of a scare besides scream, stinger and hits (real time, s). */
	struct FCue
	{
		float Time;
		ESound Sound;
		float Volume;
	};
	TArrayView<const FCue> CuesOf(int32 Style)
	{
		static const FCue LightsOut[] = { {0.f, ESound::PowerDown, 0.8f}, {0.55f, ESound::Heartbeat, 0.9f}, {0.8f, ESound::Breath, 0.5f}, {1.0f, ESound::Heartbeat, 1.f} };
		static const FCue DraggedDown[] = { {Landing, ESound::Thud, 1.f}, {0.8f, ESound::Ringing, 0.3f}, {1.15f, ESound::Heartbeat, 1.f}, {1.72f, ESound::Heartbeat, 0.9f},
			{2.35f, ESound::Heartbeat, 0.75f}, {3.05f, ESound::Heartbeat, 0.55f} };
		static const FCue SignalLost[] = { {0.f, ESound::Static, 0.8f}, {1.4f, ESound::Ringing, 0.35f} };
		switch (Style)
		{
		case HCScareStyle::LightsOut: return MakeArrayView(LightsOut);
		case HCScareStyle::DraggedDown: return MakeArrayView(DraggedDown);
		case HCScareStyle::SignalLost: return MakeArrayView(SignalLost);
		default: return TArrayView<const FCue>();
		}
	}

	/** A strike's punch: rises within ~30 ms, dies within ~0.4 s. */
	float Pulse(float Age)
	{
		return Age < 0.f ? 0.f : (1.f - FMath::Exp(-Age * 70.f)) * FMath::Exp(-Age * 8.f);
	}

	float SmoothNoise(float Time, float Seed, float Frequency)
	{
		return FMath::Clamp(FMath::PerlinNoise1D(Time * Frequency + Seed) * 1.6f, -1.f, 1.f);
	}

	/** Lights out: the station lights flicker and die (exposure in stops). Reduced: they just go out, no strobe. */
	float LightsOutExposure(float T, bool bFlicker)
	{
		if (!bFlicker)
		{
			return -10.f * FMath::SmoothStep(0.05f, PowerDies, T);
		}
		if (T < 0.06f) return 0.f;
		if (T < 0.11f) return -7.f;
		if (T < 0.17f) return 0.f;
		if (T < 0.20f) return -7.f;
		if (T < 0.23f) return 0.8f;
		if (T < PowerDies) return -5.f;
		return -10.f;
	}

	UTexture2D* MakeTexture(int32 Width, int32 Height, bool bNearest, const TArray<FColor>& Pixels)
	{
		if (Pixels.Num() != Width * Height)
		{
			return nullptr;
		}
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		Texture->SRGB = true;
		Texture->Filter = bNearest ? TF_Nearest : TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	float Hash2(int32 X, int32 Y, int32 Seed)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + static_cast<uint32>(Seed) * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return static_cast<float>(H ^ (H >> 16)) / 4294967295.f;
	}

	float ValueNoise(float X, float Y, int32 Seed)
	{
		const int32 X0 = FMath::FloorToInt32(X);
		const int32 Y0 = FMath::FloorToInt32(Y);
		float FX = X - X0;
		float FY = Y - Y0;
		FX = FX * FX * (3.f - 2.f * FX);
		FY = FY * FY * (3.f - 2.f * FY);
		const float A = FMath::Lerp(Hash2(X0, Y0, Seed), Hash2(X0 + 1, Y0, Seed), FX);
		const float B = FMath::Lerp(Hash2(X0, Y0 + 1, Seed), Hash2(X0 + 1, Y0 + 1, Seed), FX);
		return FMath::Lerp(A, B, FY);
	}

	float Fbm(float X, float Y, int32 Seed)
	{
		return 0.5f * ValueNoise(X, Y, Seed) + 0.3f * ValueNoise(X * 2.1f, Y * 2.1f, Seed + 1) + 0.2f * ValueNoise(X * 4.3f, Y * 4.3f, Seed + 2);
	}

	constexpr int32 SplatSize = 512;
	constexpr int32 SplatCount = 4;

	/** Blood on the lens: a ragged pool, a spray of stretched droplets in one direction and a few runs downwards. */
	TArray<FColor> MakeSplatPixels(int32 Seed)
	{
		FRandomStream R(Seed);
		struct FDrop { FVector2f C; float Radius; float Stretch; FVector2f Axis; };
		TArray<FDrop> Drops;
		const FVector2f Core(0.5f, 0.42f);
		const int32 CoreBlobs = R.RandRange(3, 5);
		for (int32 i = 0; i < CoreBlobs; ++i)
		{
			Drops.Add({ Core + FVector2f(float(R.FRandRange(-0.04f, 0.04f)), float(R.FRandRange(-0.03f, 0.03f))), float(R.FRandRange(0.045f, 0.075f)), 1.f, FVector2f(1.f, 0.f) });
		}
		const float Spray = R.FRandRange(0.f, 2.f * PI);
		const int32 Lobes = R.RandRange(5, 9);
		for (int32 i = 0; i < Lobes; ++i)
		{
			const float A = Spray + R.FRandRange(-1.6f, 1.6f);
			const float D = R.FRandRange(0.06f, 0.13f);
			const FVector2f Axis(FMath::Cos(A), FMath::Sin(A));
			Drops.Add({ Core + Axis * D, float(R.FRandRange(0.018f, 0.04f)), float(R.FRandRange(1.f, 1.8f)), Axis });
		}
		const int32 Sprays = R.RandRange(40, 70);
		for (int32 i = 0; i < Sprays; ++i)
		{
			const bool bAimed = R.FRand() < 0.7f;
			const float A = bAimed ? Spray + R.FRandRange(-0.7f, 0.7f) : R.FRandRange(0.f, 2.f * PI);
			const float D = R.FRandRange(0.14f, bAimed ? 0.47f : 0.3f);
			const FVector2f Axis(FMath::Cos(A), FMath::Sin(A));
			Drops.Add({ Core + Axis * D, float(R.FRandRange(0.0025f, 0.011f)), float(R.FRandRange(1.f, 3.2f)), Axis });
		}
		struct FRun { float X, Top, Bottom, Width; };
		TArray<FRun> Runs;
		const int32 RunCount = R.RandRange(1, 3);
		for (int32 i = 0; i < RunCount; ++i)
		{
			Runs.Add({ float(0.5f + R.FRandRange(-0.06f, 0.06f)), 0.46f, float(0.6f + R.FRandRange(0.06f, 0.32f)), float(R.FRandRange(0.005f, 0.011f)) });
		}
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(SplatSize * SplatSize);
		for (int32 Y = 0; Y < SplatSize; ++Y)
		{
			for (int32 X = 0; X < SplatSize; ++X)
			{
				const FVector2f P((X + 0.5f) / SplatSize, (Y + 0.5f) / SplatSize);
				float Field = 0.f;
				for (const FDrop& Drop : Drops)
				{
					const FVector2f D = P - Drop.C;
					const float Along = (D.X * Drop.Axis.X + D.Y * Drop.Axis.Y) / Drop.Stretch;
					const float Across = D.X * -Drop.Axis.Y + D.Y * Drop.Axis.X;
					Field += FMath::Square(Drop.Radius) / FMath::Max(1e-6f, Along * Along + Across * Across);
				}
				for (const FRun& Run : Runs)
				{
					if (P.Y > Run.Top && P.Y < Run.Bottom)
					{
						const float Taper = 1.f - 0.45f * (P.Y - Run.Top) / (Run.Bottom - Run.Top);
						const float Wobble = 0.004f * FMath::Sin(P.Y * 60.f + Run.X * 40.f);
						Field += FMath::Square(Run.Width * Taper) / FMath::Max(1e-6f, FMath::Square(P.X - Run.X - Wobble));
					}
					Field += FMath::Square(Run.Width * 1.5f) / FMath::Max(1e-6f, (P - FVector2f(Run.X, Run.Bottom)).SizeSquared());
				}
				Field *= 0.72f + 0.56f * Fbm(P.X * 14.f, P.Y * 14.f, Seed);
				const float Cover = FMath::SmoothStep(0.92f, 1.08f, Field);
				const float Thick = FMath::Clamp((Field - 1.f) / 7.f, 0.f, 1.f);
				// The thin film at the rim is lighter and more see-through, the pool is almost black
				const uint8 Light = static_cast<uint8>(255.f * FMath::Clamp(0.85f - 0.5f * Thick + 0.12f * (Fbm(P.X * 40.f, P.Y * 40.f, Seed + 7) - 0.5f), 0.f, 1.f));
				Pixels[Y * SplatSize + X] = FColor(Light, Light, Light, static_cast<uint8>(255.f * Cover * (0.8f + 0.2f * Thick)));
			}
		}
		return Pixels;
	}

	/** The blood pictures take about half a second to make: made once per process, in the background. */
	struct FSplatCache
	{
		TArray<FColor> Pixels[SplatCount];
		std::atomic<int32> State{ 0 }; // 0 not started, 1 being made, 2 ready
	};
	FSplatCache& SplatCache()
	{
		static FSplatCache* Cache = new FSplatCache(); // never freed: the worker may still write into it while the game quits
		return *Cache;
	}

	TArray<FColor> MakeStaticPixels(int32 Seed)
	{
		FRandomStream R(Seed);
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(320 * 180);
		for (FColor& Pixel : Pixels)
		{
			const uint8 V = static_cast<uint8>(R.RandRange(0, 255));
			Pixel = FColor(V, V, V, 255);
		}
		return Pixels;
	}
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

	EyeLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EyeLight"));
	EyeLight->SetupAttachment(FaceRoot);
	EyeLight->SetRelativeLocation(FVector(38.f, 0.f, -14.f));
	EyeLight->SetIntensityUnits(ELightUnits::Candelas);
	EyeLight->SetIntensity(0.f);
	EyeLight->SetAttenuationRadius(75.f);
	EyeLight->SetLightColor(FLinearColor(0.85f, 0.95f, 0.6f));
	EyeLight->SetCastShadows(false);
	EyeLight->SetVisibility(false);

	Monster = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Monster"));
	Monster->SetupAttachment(FaceRoot);
	Monster->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Monster->SetCanEverAffectNavigation(false);
	Monster->SetCastShadow(false);
	Monster->SetVisibility(false);
}

AHCJumpscareDirector* AHCJumpscareDirector::Start(AHCPlayerController* InPC, const FVector& InAttackerLocation, bool bInPreview, int32 ForcedStyle)
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
		Director->Begin(InPC, InAttackerLocation, bInPreview, ForcedStyle);
	}
	return Director;
}

int32 AHCJumpscareDirector::PickStyle(int32 Previous, float Random01)
{
	const bool bAvoid = Previous >= 1 && Previous <= HCScareStyle::Count;
	const int32 Choices = bAvoid ? HCScareStyle::Count - 1 : HCScareStyle::Count;
	int32 Pick = FMath::Clamp(FMath::FloorToInt32(Random01 * Choices), 0, Choices - 1) + 1;
	if (bAvoid && Pick >= Previous)
	{
		++Pick;
	}
	return Pick;
}

float AHCJumpscareDirector::DurationOf(int32 InStyle)
{
	switch (InStyle)
	{
	case HCScareStyle::LightsOut: return 2.65f;
	case HCScareStyle::DraggedDown: return 2.75f;
	case HCScareStyle::SignalLost: return 2.32f;
	default: return 2.3f;
	}
}

void AHCJumpscareDirector::PrepareAssets()
{
	HCScare::FSplatCache& Cache = HCScare::SplatCache();
	int32 Expected = 0;
	if (!Cache.State.compare_exchange_strong(Expected, 1))
	{
		return;
	}
	Async(EAsyncExecution::ThreadPool, []()
	{
		HCScare::FSplatCache& Ready = HCScare::SplatCache();
		for (int32 i = 0; i < HCScare::SplatCount; ++i)
		{
			Ready.Pixels[i] = HCScare::MakeSplatPixels(900 + i * 37);
		}
		Ready.State = 2;
	});
}

void AHCJumpscareDirector::Begin(AHCPlayerController* InPC, const FVector& InAttackerLocation, bool bInPreview, int32 ForcedStyle)
{
	PC = InPC;
	AttackerLocation = InAttackerLocation;
	bPreview = bInPreview;
	TurnTime = UHCGameSettings::Get()->JumpscareTurnTime;
	if (const UHCGameInstance* GI = Cast<UHCGameInstance>(InPC->GetGameInstance()))
	{
		Intensity = GI->GetJumpscareIntensity();
	}
	int32 Wanted = ForcedStyle;
	if (Wanted == 0)
	{
		FParse::Value(FCommandLine::Get(), TEXT("HCJumpscareStyle="), Wanted);
	}
	Style = Wanted >= 1 && Wanted <= HCScareStyle::Count ? Wanted : PickStyle(HCScare::LastStyle, FMath::FRand());
	HCScare::LastStyle = Style;
	Duration = DurationOf(Style);
	Random.Initialize(FMath::Rand());
	NoiseTime = Random.FRandRange(0.f, 100.f); // a different shake every time

	if (Style == HCScareStyle::DraggedDown)
	{
		HCScare::FSplatCache& Cache = HCScare::SplatCache();
		if (Cache.State == 2)
		{
			for (const TArray<FColor>& Pixels : Cache.Pixels)
			{
				if (UTexture2D* Texture = HCScare::MakeTexture(HCScare::SplatSize, HCScare::SplatSize, false, Pixels))
				{
					SplatTextures.Add(Texture);
				}
			}
		}
		else
		{
			PrepareAssets(); // not ready yet (a scare in the first second of a round): this one stays without blood
		}
	}
	if (Style == HCScareStyle::SignalLost)
	{
		for (int32 i = 0; i < 6; ++i)
		{
			if (UTexture2D* Texture = HCScare::MakeTexture(320, 180, true, HCScare::MakeStaticPixels(31 + i)))
			{
				NoiseTextures.Add(Texture);
			}
		}
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

	const UHCModelSet* Models = HCModels::Get();
	if (Models && Models->MonsterMesh)
	{
		// The true form of the Wandler: the model attacks, its head stays where the face belongs
		Monster->SetSkeletalMesh(Models->MonsterMesh);
		Monster->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // the model looks along +Y, the face root along +X
		Monster->SetRelativeScale3D(FVector(1.15f));
		Monster->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Monster->SetVisibility(true);
		if (Models->MonsterAttack)
		{
			Monster->PlayAnimation(Models->MonsterAttack, false);
			if (Style == HCScareStyle::SignalLost)
			{
				Monster->SetPlayRate(0.f); // the broken signal drives the pose itself (freezes and jumps)
			}
		}
		bRealMonster = true;
		FaceLight->SetIntensity(9.f); // the model is much darker than the placeholder face
		Head->SetVisibility(false);
		Maw->SetVisibility(false);
		for (const TArray<TObjectPtr<UStaticMeshComponent>>* Group : { &Eyes, &UpperTeeth, &LowerTeeth })
		{
			for (UStaticMeshComponent* Part : *Group)
			{
				Part->SetVisibility(false);
			}
		}
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
		SavedCameraRelative = Cam->GetRelativeLocation();
		SavedPostProcess = Cam->PostProcessSettings;
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
	for (TActorIterator<AHCCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != InPC->GetPawn())
		{
			It->SetNameTagHiddenLocally(true);
			HiddenNameTags.Add(*It);
		}
	}
	CurrentRotation = StartRotation;
	TargetRotation = (AttackerLocation - LastCameraLocation).Rotation();
	TargetRotation.Roll = 0.f;
	TargetRotation.Pitch = FMath::Clamp(TargetRotation.Pitch, -35.f, 35.f);

	if (Intensity == EHCJumpscareIntensity::Minimal)
	{
		// Minimal: no close-up, just a gentle fade with a dull sound
		Duration = 2.3f;
		SetActorHiddenInGame(true);
		FaceLight->SetVisibility(false);
		if (InPC->PlayerCameraManager)
		{
			InPC->PlayerCameraManager->StartCameraFade(0.f, 1.f, Duration, FLinearColor::Black, false, true);
		}
		HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Thump, 0.5f);
		bStingerPlayed = true;
		bFlashed = true;
	}
	if (Style == HCScareStyle::LightsOut || Style == HCScareStyle::SignalLost)
	{
		bFlashed = true; // lights out flashes with the slam; the broken signal has its own flashes
	}
	UE_LOG(LogHC, Log, TEXT("Jumpscare started (intensity %d, preview %d, style %d)"), static_cast<int32>(Intensity), bPreview ? 1 : 0, Style);
	PlaySounds();
	UpdateScare(0.f);
}

void AHCJumpscareDirector::PlayScare(HCProceduralAudio::ESound Sound, float Volume, bool bLoud)
{
	TArray<UAudioComponent*> Voices;
	HCProceduralAudio::Play2D(this, Sound, Volume, &Voices);
	if (!bLoud)
	{
		return;
	}
	for (UAudioComponent* Voice : Voices)
	{
		if (Muffle > 0.f)
		{
			Voice->SetLowPassFilterEnabled(true);
			Voice->SetLowPassFilterFrequency(20000.f * FMath::Pow(700.f / 20000.f, Muffle));
		}
		LoudVoices.Add(Voice);
	}
}

void AHCJumpscareDirector::PlaySounds()
{
	using HCProceduralAudio::ESound;
	const bool bMinimal = Intensity == EHCJumpscareIntensity::Minimal;
	const float Loudness = Intensity == EHCJumpscareIntensity::Full ? 1.f : 0.5f;

	// The victim's own scream (both recordings at once, feedback-kill-scream-mix): with the scare, not with the
	// multicast of the attack, so that "lights out" can hold it back until the slam. Everybody else hears it from
	// AHCCharacter::MulticastPlayKillAttack.
	const float ScreamAt = Style == HCScareStyle::LightsOut && !bMinimal ? HCScare::SlamTime : 0.f;
	if (!bScreamPlayed && Elapsed >= ScreamAt)
	{
		bScreamPlayed = true;
		PlayScare(ESound::Scream, Intensity == EHCJumpscareIntensity::Full ? 2.f : bMinimal ? 0.6f : 1.2f, true);
	}
	if (bMinimal)
	{
		return;
	}
	const float StingerAt = Style == HCScareStyle::LightsOut ? HCScare::SlamTime : HCScare::LungeStart;
	if (!bStingerPlayed && Elapsed >= StingerAt)
	{
		bStingerPlayed = true;
		// Reduced: about 12 dB quieter
		PlayScare(ESound::Stinger, Intensity == EHCJumpscareIntensity::Full ? 1.f : 0.25f, true);
	}
	const TArrayView<const HCScare::FCue> Cues = HCScare::CuesOf(Style);
	while (NextCue < Cues.Num() && Elapsed >= Cues[NextCue].Time)
	{
		PlayScare(Cues[NextCue].Sound, Cues[NextCue].Volume * Loudness, false);
		++NextCue;
	}
}

void AHCJumpscareDirector::SetAmbience(float Level)
{
	bAmbienceChanged = Level < 1.f;
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		It->SetLocalAmbience(Level);
	}
}

void AHCJumpscareDirector::ShowNameTags()
{
	for (const TWeakObjectPtr<AHCCharacter>& Other : HiddenNameTags)
	{
		if (AHCCharacter* Still = Other.Get())
		{
			Still->SetNameTagHiddenLocally(false);
		}
	}
	HiddenNameTags.Reset();
}

void AHCJumpscareDirector::Strike(float Power)
{
	Trauma = FMath::Min(1.25f, Trauma + Power);
	StrikeAge = 0.f;
	StrikePower = Power;
	StrikeRollSign = Random.FRand() < 0.5f ? -1.f : 1.f;
	if (Style == HCScareStyle::DraggedDown && Elapsed > 0.7f && Power >= 0.8f && SplatTextures.Num() > 0)
	{
		// Blood on the lens, around the edges so the monster stays in view
		static const FVector2f Spots[] = { {0.2f, 0.3f}, {0.8f, 0.62f}, {0.16f, 0.72f}, {0.78f, 0.22f}, {0.5f, 0.85f} };
		FSplat Splat;
		Splat.Pos = Spots[Splats.Num() % UE_ARRAY_COUNT(Spots)] + FVector2f(float(Random.FRandRange(-0.05f, 0.05f)), float(Random.FRandRange(-0.05f, 0.05f)));
		Splat.Size = Random.FRandRange(0.5f, 0.75f);
		Splat.Angle = Random.FRandRange(-12.f, 12.f);
		Splat.Born = Elapsed;
		Splat.Texture = Splats.Num() % SplatTextures.Num();
		Splats.Add(Splat);
		Darkness = FMath::Min(1.f, Darkness + 0.2f);
	}
	// Lights out: the slam has the scream and the stinger already
	const bool bSlam = Style == HCScareStyle::LightsOut && NextStrike == 0;
	if (!bSlam)
	{
		PlayScare(HCProceduralAudio::ESound::Hit, 0.9f * Power * (Intensity == EHCJumpscareIntensity::Full ? 1.f : 0.5f), true);
	}
}

void AHCJumpscareDirector::UpdateScare(float DeltaSeconds)
{
	UCameraComponent* Cam = Camera.Get();
	const bool bFull = Intensity == EHCJumpscareIntensity::Full;
	const float ShakeScale = bFull ? 1.f : 0.35f;

	if (Intensity == EHCJumpscareIntensity::Minimal)
	{
		// Only the turn towards the attacker, while the screen fades to black
		const float TurnAlpha = FMath::Clamp(Elapsed / FMath::Max(0.01f, TurnTime), 0.f, 1.f);
		CurrentRotation = FQuat::Slerp(StartRotation.Quaternion(), TargetRotation.Quaternion(), FMath::InterpEaseOut(0.f, 1.f, TurnAlpha, 2.f)).Rotator();
		if (Cam)
		{
			Cam->SetWorldRotation(CurrentRotation);
		}
		return;
	}

	// --- Signal lost: the picture freezes now and then and jumps ahead; single frames of a close-up flash in between
	float VT = Elapsed;
	bHolding = false;
	bSubliminal = false;
	if (Style == HCScareStyle::SignalLost)
	{
		float Offset = 0.f;
		for (const float (&Hold)[3] : HCScareTimeline::SignalHolds)
		{
			if (Elapsed < Hold[0])
			{
				break;
			}
			if (Elapsed < Hold[1])
			{
				VT = Hold[0] + Offset;
				bHolding = true;
				break;
			}
			Offset += Hold[2] - (Hold[1] - Hold[0]);
		}
		if (!bHolding)
		{
			VT = Elapsed + Offset;
		}
		// Each flash shows for its length, but at least for one frame even at a low frame rate. Not at "Reduced".
		if (NextFlash < UE_ARRAY_COUNT(HCScareTimeline::SignalFlashes) && Elapsed >= HCScareTimeline::SignalFlashes[NextFlash])
		{
			bSubliminal = bFull;
			if (Elapsed >= HCScareTimeline::SignalFlashes[NextFlash] + HCScareTimeline::SignalFlashLength)
			{
				++NextFlash;
			}
		}
		if (bRealMonster)
		{
			Monster->SetPosition(VT, false);
		}
		// The loud sounds freeze with the picture (the static of the hold plays over them)
		if (bHolding != bVoicesPaused)
		{
			bVoicesPaused = bHolding;
			for (const TWeakObjectPtr<UAudioComponent>& Voice : LoudVoices)
			{
				if (UAudioComponent* Still = Voice.Get())
				{
					Still->SetPaused(bHolding);
				}
			}
		}
	}

	if (!bHolding)
	{
		Trauma = FMath::Max(0.f, Trauma - DeltaSeconds * 1.3f);
		StrikeAge += DeltaSeconds;
		NoiseTime += DeltaSeconds;
	}
	const TArrayView<const FVector2f> Strikes = HCScare::StrikesOf(Style);
	while (NextStrike < Strikes.Num() && VT >= Strikes[NextStrike].X)
	{
		Strike(Strikes[NextStrike].Y);
		++NextStrike;
	}

	// The camera's natural place (it is attached to the capsule)
	FVector BaseLocation = LastCameraLocation;
	if (Cam && Cam->GetAttachParent())
	{
		BaseLocation = Cam->GetAttachParent()->GetComponentTransform().TransformPosition(SavedCameraRelative);
	}

	// Turn towards the attacker: a fast whip, or (lights out) a slow, forced turn
	const bool bSlowTurn = Style == HCScareStyle::LightsOut;
	const float TurnDuration = bSlowTurn ? 0.7f : TurnTime;
	const float TurnAlpha = FMath::Clamp(VT / FMath::Max(0.01f, TurnDuration), 0.f, 1.f);
	const float TurnEase = bSlowTurn ? FMath::InterpEaseInOut(0.f, 1.f, TurnAlpha, 2.f) : FMath::InterpEaseOut(0.f, 1.f, TurnAlpha, 2.f);
	CurrentRotation = FQuat::Slerp(StartRotation.Quaternion(), TargetRotation.Quaternion(), TurnEase).Rotator();
	FRotator BaseRotation = CurrentRotation;
	FVector CameraLocation = BaseLocation;

	float LungeAlpha = FMath::Clamp((VT - HCScare::LungeStart) / (HCScare::LungeEnd - HCScare::LungeStart), 0.f, 1.f);
	float Distance = FMath::Lerp(HCScare::FarDistance, HCScare::NearDistance, FMath::InterpEaseIn(0.f, 1.f, LungeAlpha, 2.5f));
	const float P = StrikePower * HCScare::Pulse(StrikeAge);
	float Exposure = 0.f;
	float Saturation = 1.f;
	float Contrast = 1.f;
	float Vignette = FMath::Lerp(0.5f, 1.1f, LungeAlpha);
	float Fringe = 4.f * LungeAlpha + 3.f * P;
	float Grain = 0.15f;
	float Blur = 0.f;
	float Tremble = 0.f;
	FLinearColor Tint = FLinearColor::LerpUsingHSV(FLinearColor::White, FLinearColor(1.f, 0.55f, 0.5f), LungeAlpha);
	bool bShowMonster = true;
	bool bFaceLight = true;
	FVector FaceLocation = FVector::ZeroVector;
	bool bFaceSet = false;

	switch (Style)
	{
	case HCScareStyle::LightsOut:
	{
		if (VT < HCScare::SlamTime)
		{
			// The lights die; the figure stands in the dark, lit from below; it blinks and is suddenly closer
			Exposure = HCScare::LightsOutExposure(VT, bFull);
			bShowMonster = VT >= 0.82f;
			bFaceLight = false;
			Saturation = 0.45f;
			Vignette = 1.f;
			Fringe = 0.f;
			Tint = FLinearColor::White;
			Tremble = 0.06f;
			const float EyesUp = FMath::Clamp((VT - 0.82f) / 0.3f, 0.f, 1.f);
			const bool bBlink = VT > 1.04f && VT < 1.12f;
			Distance = VT < 1.08f ? 118.f : 96.f;
			EyeLight->SetVisibility(bShowMonster && !bBlink);
			EyeLight->SetIntensity(1400.f * EyesUp * (0.85f + 0.15f * HCScare::SmoothNoise(NoiseTime, 3.f, 9.f)));
			if (bRealMonster)
			{
				Monster->SetPlayRate(0.f);
				Monster->SetPosition(1.9f, false); // standing, face to you
			}
			if (!bAmbienceChanged && VT >= HCScare::PowerDies)
			{
				SetAmbience(0.f); // the breaker drops: the hum and the lamps fall silent
			}
		}
		else
		{
			// The slam
			const float Since = VT - HCScare::SlamTime;
			Distance = FMath::Lerp(118.f, HCScare::NearDistance, FMath::Clamp(Since / 0.07f, 0.f, 1.f));
			Exposure = 2.6f * ShakeScale * FMath::Exp(-Since * 8.f);
			EyeLight->SetVisibility(false);
			if (bRealMonster && Monster->GetPlayRate() == 0.f)
			{
				Monster->SetPosition(0.3f, false);
				Monster->SetPlayRate(1.f);
			}
			LungeAlpha = 1.f;
			Vignette = 1.1f;
			Fringe = 4.f + 3.f * P;
			Tint = FLinearColor(1.f, 0.55f, 0.5f);
			Tremble = 0.32f;
			Blur = 0.2f + 0.6f * P;
			if (!bFlashed)
			{
				bFlashed = true;
				if (bFull && PC.IsValid() && PC->PlayerCameraManager)
				{
					PC->PlayerCameraManager->StartCameraFade(0.8f, 0.f, 0.2f, FLinearColor::White, false, false);
				}
			}
		}
		break;
	}
	case HCScareStyle::DraggedDown:
	{
		// The first strike throws you to the floor, rolled onto your side; it crouches over you
		const float Fall = FMath::Clamp((VT - 0.40f) / (HCScare::Landing - 0.40f), 0.f, 1.f);
		const float FallDrop = Fall * Fall; // gravity
		const float FallBlend = FMath::SmoothStep(0.f, 1.f, Fall);
		const float RollAlpha = FMath::InterpEaseOut(0.f, 1.f, FMath::Clamp((VT - 0.38f) / 0.5f, 0.f, 1.f), 2.f);
		const float Landed = VT - HCScare::Landing;
		const float Bounce = Landed > 0.f ? 7.f * FMath::Sin(Landed * 28.f) * FMath::Exp(-Landed * 9.f) : 0.f;
		const FVector Fallen = BaseLocation + FVector(0.f, 0.f, -136.f);
		CameraLocation = BaseLocation + FVector(0.f, 0.f, -136.f * FallDrop + Bounce);

		const FVector Flat = BaseRotation.Vector().GetSafeNormal2D();
		const FVector Standing = BaseLocation + BaseRotation.Vector() * Distance + FVector(0.f, 0.f, -4.f);
		const FVector Crouched = Fallen + Flat * 55.f + FVector(0.f, 0.f, 32.f);
		FaceLocation = FMath::Lerp(Standing, Crouched, FallBlend);
		FaceLocation += (CameraLocation - FaceLocation).GetSafeNormal() * 28.f * P;
		bFaceSet = true;

		const FRotator Look = (FaceLocation - CameraLocation).Rotation();
		BaseRotation = FQuat::Slerp(BaseRotation.Quaternion(), Look.Quaternion(), FallBlend).Rotator();
		BaseRotation.Roll = -74.f * RollAlpha;

		const float Out = FMath::Clamp((VT - 2.15f) / 0.5f, 0.f, 1.f);
		Exposure = -0.9f * Darkness - 10.f * FMath::Square(Out);
		Saturation = 1.f - 0.55f * Darkness;
		Vignette = 1.1f + 0.9f * Darkness;
		Tremble = (0.2f + 0.15f * Darkness) * (1.f - Out);
		Blur = VT < 0.85f ? 0.8f : 0.2f + 0.5f * P;

		// On the floor everything sounds dull and far away
		Muffle = FMath::Clamp((Elapsed - HCScare::Landing) / 0.3f, 0.f, 1.f);
		if (Muffle > 0.f)
		{
			const float Cutoff = 20000.f * FMath::Pow(700.f / 20000.f, Muffle);
			for (const TWeakObjectPtr<UAudioComponent>& Voice : LoudVoices)
			{
				if (UAudioComponent* Still = Voice.Get())
				{
					Still->SetLowPassFilterEnabled(true);
					Still->SetLowPassFilterFrequency(Cutoff);
				}
			}
			if (!bAmbienceChanged)
			{
				SetAmbience(0.35f);
			}
		}
		break;
	}
	case HCScareStyle::SignalLost:
	{
		Tremble = 0.25f;
		Fringe = 5.f;
		Grain = bHolding ? 1.f : 0.6f;
		Contrast = bHolding ? 1.25f : 1.f;
		if (bHolding)
		{
			Tint = FLinearColor(0.85f, 1.05f, 1.f);
		}
		break;
	}
	default: // Quake
	{
		Tremble = 0.18f + 0.4f * FMath::Clamp(VT / Duration, 0.f, 1.f);
		Blur = VT < TurnTime + 0.05f ? 1.f : 0.2f + 0.6f * P;
		break;
	}
	}

	// Shake: smooth noise driven by trauma, plus a punch for every strike
	const float S = (FMath::Square(FMath::Min(Trauma, 1.f)) + Tremble) * ShakeScale;
	const float Pk = P * ShakeScale;
	FRotator Shake;
	Shake.Pitch = HCScare::SmoothNoise(NoiseTime, 1.3f, 17.f) * 3.5f * S + HCScare::SmoothNoise(NoiseTime, 4.4f, 43.f) * 0.8f * S + 5.f * Pk;
	Shake.Yaw = HCScare::SmoothNoise(NoiseTime, 7.1f, 17.f) * 3.5f * S + HCScare::SmoothNoise(NoiseTime, 9.9f, 43.f) * 0.8f * S + 2.f * Pk * StrikeRollSign;
	Shake.Roll = HCScare::SmoothNoise(NoiseTime, 13.7f, 13.f) * 7.f * S + StrikeRollSign * 6.f * Pk;
	const FVector ShakeOffset = BaseRotation.RotateVector(FVector(-12.f * Pk, HCScare::SmoothNoise(NoiseTime, 21.f, 15.f) * 4.f * S, HCScare::SmoothNoise(NoiseTime, 29.f, 15.f) * 4.f * S));

	FRotator ViewRotation = BaseRotation;
	ViewRotation.Pitch += Shake.Pitch;
	ViewRotation.Yaw += Shake.Yaw;
	ViewRotation.Roll += Shake.Roll;
	FVector ViewLocation = CameraLocation + ShakeOffset;
	float FOV = SavedFOV - 16.f * FMath::Square(LungeAlpha) * ShakeScale - 22.f * StrikePower * FMath::Exp(-StrikeAge * 13.f) * ShakeScale;
	// Every strike flashes red and bright for a moment
	Tint = Tint * FLinearColor::LerpUsingHSV(FLinearColor::White, FLinearColor(1.f, 0.22f, 0.18f), FMath::Clamp(0.85f * StrikePower * FMath::Exp(-StrikeAge * 7.f) * ShakeScale, 0.f, 1.f));
	Exposure += 1.3f * StrikePower * FMath::Exp(-StrikeAge * 10.f) * ShakeScale;

	if (Style == HCScareStyle::SignalLost)
	{
		if (bHolding)
		{
			ViewRotation = HeldRotation;
			ViewLocation = HeldLocation;
			FOV = HeldFOV;
		}
		else
		{
			HeldRotation = ViewRotation;
			HeldLocation = ViewLocation;
			HeldFOV = FOV;
		}
		if (bSubliminal)
		{
			// One frame: an extreme close-up of the face, blown out and blood red
			FOV = 24.f;
			Tint = FLinearColor(1.5f, 0.1f, 0.08f);
			Contrast = 1.9f;
			Exposure += 1.4f;
			Grain = 1.f;
		}
	}

	if (Cam)
	{
		LastCameraLocation = BaseLocation;
		Cam->SetWorldLocationAndRotation(ViewLocation, ViewRotation);
		Cam->SetFieldOfView(FMath::Clamp(FOV, 20.f, 120.f));
		FPostProcessSettings& PPS = Cam->PostProcessSettings;
		PPS.bOverride_SceneColorTint = true;
		PPS.SceneColorTint = Tint;
		PPS.bOverride_VignetteIntensity = true;
		PPS.VignetteIntensity = Vignette;
		PPS.bOverride_SceneFringeIntensity = true;
		PPS.SceneFringeIntensity = FMath::Min(5.f, Fringe * ShakeScale);
		PPS.bOverride_ChromaticAberrationStartOffset = true;
		PPS.ChromaticAberrationStartOffset = 0.f;
		PPS.bOverride_AutoExposureBias = true;
		PPS.AutoExposureBias = Exposure;
		PPS.bOverride_ColorSaturation = true;
		PPS.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.f);
		PPS.bOverride_ColorContrast = true;
		PPS.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.f);
		PPS.bOverride_FilmGrainIntensity = true;
		PPS.FilmGrainIntensity = Grain;
		// Motion blur is off in the project settings; this override still works and smears the whip and the hits
		PPS.bOverride_MotionBlurAmount = true;
		PPS.MotionBlurAmount = Blur * ShakeScale;
		PPS.bOverride_MotionBlurMax = true;
		PPS.MotionBlurMax = 6.f;
	}

	if (bHolding)
	{
		return; // the whole picture stands still, the monster too
	}

	// The face: placed along the view without most of the shake, so the shake moves it around the frame a little
	if (!bFaceSet)
	{
		Distance -= 14.f * P;
		const FRotator FaceDirection = FQuat::Slerp(BaseRotation.Quaternion(), ViewRotation.Quaternion(), 0.7f).Rotator();
		FaceLocation = BaseLocation + FaceDirection.Vector() * Distance + FVector(0.f, 0.f, -4.f);
		if (Style == HCScareStyle::LightsOut && VT < HCScare::SlamTime)
		{
			FaceLocation += FVector(0.f, 0.f, -6.f);
		}
	}
	SetActorHiddenInGame(!bShowMonster);
	FaceLight->SetVisibility(bFaceLight && bShowMonster);
	if (bFaceLight)
	{
		FaceLight->SetIntensity(bRealMonster ? (Style == HCScareStyle::LightsOut ? 14.f : 9.f) : 1.6f);
	}
	FRotator FaceRotation = (ViewLocation - FaceLocation).Rotation();
	if (Style == HCScareStyle::DraggedDown)
	{
		// The attack animation bows the head; leaning over you on top of that would show the back of the skull
		FaceRotation.Pitch *= 0.15f;
	}
	SetActorLocationAndRotation(FaceLocation, FaceRotation);
	if (bRealMonster)
	{
		// Whatever the attack animation does with the body: the head stays right where the face belongs
		const FVector HeadNow = Monster->GetBoneLocation(TEXT("head"));
		Monster->AddWorldOffset(FaceLocation + FVector(0.f, 0.f, 16.f) - HeadNow);
	}
	else
	{
		// The placeholder face opens its maw
		const float Open = FMath::Lerp(0.06f, 0.42f, FMath::Clamp((VT - 0.3f) / 0.5f, 0.f, 1.f));
		Maw->SetRelativeScale3D(FVector(0.2f, 0.36f, Open));
		Maw->SetRelativeLocation(FVector(30.f, 0.f, -18.f - Open * 22.f));
		for (UStaticMeshComponent* Tooth : LowerTeeth)
		{
			FVector Location = Tooth->GetRelativeLocation();
			Location.Z = -24.f - Open * 44.f;
			Tooth->SetRelativeLocation(Location);
		}
	}
}

void AHCJumpscareDirector::DrawOverlay(UCanvas* Canvas)
{
	if (!Canvas || bFinished || bBlack || Intensity == EHCJumpscareIntensity::Minimal)
	{
		return;
	}
	const bool bFull = Intensity == EHCJumpscareIntensity::Full;
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	auto Rect = [Canvas](float X, float Y, float RW, float RH, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(RW, RH), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	};

	if (Style == HCScareStyle::DraggedDown)
	{
		const float Fade = 1.f - 0.9f * FMath::Clamp((Elapsed - 2.15f) / 0.5f, 0.f, 1.f);
		for (const FSplat& Splat : Splats)
		{
			UTexture2D* Texture = SplatTextures.IsValidIndex(Splat.Texture) ? SplatTextures[Splat.Texture].Get() : nullptr;
			if (!Texture || !Texture->GetResource())
			{
				continue;
			}
			const float Age = Elapsed - Splat.Born;
			const float Grow = FMath::Lerp(0.8f, 1.f, FMath::Clamp(Age / 0.05f, 0.f, 1.f));
			const float Side = Splat.Size * H * Grow;
			const float Slide = FMath::Min(Age, 2.f) * 0.012f * H; // it runs down the lens
			FCanvasTileItem Tile(FVector2D(Splat.Pos.X * W - Side * 0.5f, Splat.Pos.Y * H - Side * 0.5f + Slide), Texture->GetResource(), FVector2D(Side, Side),
				FLinearColor(0.42f * Fade, 0.016f * Fade, 0.012f * Fade, 0.96f));
			Tile.BlendMode = SE_BLEND_Translucent;
			Tile.Rotation = FRotator(0.f, Splat.Angle, 0.f);
			Tile.PivotPoint = FVector2D(0.5f, 0.5f);
			Canvas->DrawItem(Tile);
		}
		// Each heartbeat darkens the edges a little more
		const float Beat = FMath::Pow(FMath::Max(0.f, FMath::Sin(Elapsed * 2.f * PI * 1.1f)), 6.f);
		Rect(0.f, 0.f, W, H, FLinearColor(0.08f, 0.f, 0.f, 0.12f * Darkness * Beat));
	}

	if (Style == HCScareStyle::SignalLost)
	{
		FRandomStream R(FMath::FloorToInt32(Elapsed * 240.f));
		const float End = FMath::Clamp((Elapsed - HCScareTimeline::SignalCollapseStart) / (HCScareTimeline::SignalCollapseEnd - HCScareTimeline::SignalCollapseStart), 0.f, 1.f);
		const float Strength = bFull ? 1.f : 0.6f;
		if (NoiseTextures.Num() > 0)
		{
			UTexture2D* Texture = NoiseTextures[R.RandRange(0, NoiseTextures.Num() - 1)];
			if (Texture && Texture->GetResource())
			{
				const float Alpha = bSubliminal ? 0.15f : bHolding ? 0.42f : 0.10f + 0.25f * End + (R.FRand() < 0.15f ? 0.2f : 0.f);
				FCanvasTileItem Tile(FVector2D(0.f, 0.f), Texture->GetResource(), FVector2D(W, H), FLinearColor(1.f, 1.f, 1.f, Alpha * Strength));
				Tile.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Tile);
			}
		}
		// Torn lines
		const int32 Bars = bHolding ? R.RandRange(6, 13) : (R.FRand() < 0.5f ? R.RandRange(0, 4) : 0);
		for (int32 i = 0; i < Bars; ++i)
		{
			const float Y = R.FRand() * H;
			const float BarH = R.FRandRange(2.f, 0.035f * H);
			const float X = R.FRand() < 0.6f ? 0.f : R.FRand() * W * 0.6f;
			const float BarW = X == 0.f ? W : R.FRandRange(0.2f, 0.6f) * W;
			const float Pick = R.FRand();
			const FLinearColor Color = Pick < 0.45f ? FLinearColor(0.f, 0.f, 0.f, 0.85f) : Pick < 0.7f ? FLinearColor(1.f, 1.f, 1.f, 0.45f * Strength)
				: Pick < 0.88f ? FLinearColor(1.f, 0.f, 0.f, 0.5f * Strength) : FLinearColor(0.f, 1.f, 1.f, 0.35f * Strength);
			Rect(X, Y, BarW, BarH, Color);
		}
		// Scan lines
		const float LineStep = FMath::Max(4.f, FMath::RoundToFloat(H / 270.f));
		for (float Y = 0.f; Y < H; Y += LineStep)
		{
			Rect(0.f, Y, W, FMath::Max(1.f, LineStep * 0.25f), FLinearColor(0.f, 0.f, 0.f, 0.18f));
		}
		// The end: the picture collapses to a bright line, then to a dot, like an old tube switching off
		if (End > 0.f)
		{
			const float Squash = FMath::InterpEaseIn(0.f, 1.f, FMath::Clamp(End / 0.5f, 0.f, 1.f), 3.f);
			const float BandH = FMath::Max(2.f, H * (1.f - Squash));
			const float Pinch = FMath::Clamp((End - 0.5f) / 0.4f, 0.f, 1.f);
			const float BandW = FMath::Max(3.f, W * (1.f - FMath::InterpEaseIn(0.f, 1.f, Pinch, 2.f)));
			const float Top = (H - BandH) * 0.5f;
			const float Left = (W - BandW) * 0.5f;
			Rect(0.f, 0.f, W, Top, FLinearColor::Black);
			Rect(0.f, Top + BandH, W, H - Top - BandH, FLinearColor::Black);
			Rect(0.f, Top, Left, BandH, FLinearColor::Black);
			Rect(Left + BandW, Top, W - Left - BandW, BandH, FLinearColor::Black);
			Rect(Left, Top, BandW, BandH, FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(Squash * 1.2f, 0.f, 1.f) * (End > 0.95f ? 0.f : Strength)));
		}
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
	// Scheduled sounds play on into the black (heartbeats after the fall)
	PlaySounds();

	if (!bBlack)
	{
		UpdateScare(DeltaSeconds);
#if !UE_BUILD_SHIPPING
		static const float ShotTimes[HCScareStyle::Count + 1][3] = { {0.3f, 0.75f, 1.6f}, {0.45f, 1.15f, 1.8f}, {0.95f, 1.2f, 1.45f}, {0.6f, 1.2f, 1.75f}, {0.45f, 1.1f, 2.12f} };
		const float* Shots = ShotTimes[Intensity == EHCJumpscareIntensity::Minimal ? 0 : Style];
		if (DebugShots < 3 && Elapsed >= Shots[DebugShots] && FParse::Param(FCommandLine::Get(), TEXT("HCPhaseShots")))
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
		if (!bFlashed && Elapsed >= HCScare::LungeEnd)
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
	EyeLight->SetVisibility(false);
	// A hard cut in the sound too: only the heartbeat and the ringing in the ears go on
	for (const TWeakObjectPtr<UAudioComponent>& Voice : LoudVoices)
	{
		if (UAudioComponent* Still = Voice.Get())
		{
			Still->SetPaused(false);
			Still->FadeOut(0.05f, 0.f);
		}
	}
	LoudVoices.Reset();
	SetAmbience(0.f);
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
		Cam->SetRelativeLocation(SavedCameraRelative);
		Cam->PostProcessSettings = SavedPostProcess;
	}
	SetAmbience(1.f); // comes back with the picture
	ShowNameTags();
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

void AHCJumpscareDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Destroyed some other way (the controller went away): never leave the station silent. When the whole world goes
	// (map change, quit) there is nothing to give back.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		if (bAmbienceChanged)
		{
			SetAmbience(1.f);
		}
		ShowNameTags();
	}
	Super::EndPlay(EndPlayReason);
}
