#include "World/HCSecurityCamera.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sabotage/HCPowerSector.h"
#include "TimerManager.h"
#include "World/HCGreyboxAssets.h"
#include "World/HCMonitorDevices.h"

namespace HCCameraLook
{
	/** Size of one picture: small and a little soft, like a real CCTV feed. */
	constexpr int32 FeedWidth = 640;
	constexpr int32 FeedHeight = 360;
	/** Exposure of the picture (EV100): far brighter than the eye, so dark corridors still show who walks there. */
	constexpr float FeedEV100 = 1.1f;
}

AHCSecurityCamera::AHCSecurityCamera()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Head = CreateDefaultSubobject<USceneComponent>(TEXT("Head"));
	Head->SetupAttachment(Root);

	auto Part = [this](const TCHAR* Name, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& At, const FVector& Scale, const FRotator& Turn = FRotator::ZeroRotator)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		Component->SetStaticMesh(Mesh);
		Component->SetRelativeLocation(At);
		Component->SetRelativeRotation(Turn);
		Component->SetRelativeScale3D(Scale);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		return Component;
	};
	// A camera housing of about 30 x 13 x 13 cm on a rod from the ceiling; +X is where it looks
	Rod = Part(TEXT("Rod"), Root, HCGreybox::Cylinder(), FVector(-4.f, 0.f, 30.f), FVector(0.04f, 0.04f, 0.45f));
	Body = Part(TEXT("Body"), Head, HCGreybox::Cube(), FVector::ZeroVector, FVector(0.3f, 0.13f, 0.13f));
	Hood = Part(TEXT("Hood"), Head, HCGreybox::Cube(), FVector(3.f, 0.f, 7.5f), FVector(0.36f, 0.17f, 0.02f));
	Lens = Part(TEXT("Lens"), Head, HCGreybox::Cylinder(), FVector(16.f, 0.f, 0.f), FVector(0.085f, 0.085f, 0.05f), FRotator(90.f, 0.f, 0.f));
	Lamp = Part(TEXT("Lamp"), Head, HCGreybox::Sphere(), FVector(12.f, 4.f, 8.6f), FVector(0.028f));

	LampLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LampLight"));
	LampLight->SetupAttachment(Head);
	LampLight->SetRelativeLocation(FVector(15.f, 4.f, 9.f));
	LampLight->SetIntensityUnits(ELightUnits::Candelas);
	LampLight->SetIntensity(1.6f);
	LampLight->SetAttenuationRadius(160.f);
	LampLight->SetLightColor(FLinearColor(1.f, 0.05f, 0.03f));
	LampLight->SetCastShadows(false);
	LampLight->SetVisibility(false);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Head);
	Capture->SetRelativeLocation(FVector(20.f, 0.f, 0.f));
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->FOVAngle = 96.f;
	Capture->ShowFlags.SetMotionBlur(false);
	// The look of an old surveillance feed: almost no colour, a green cast, grain and dark corners. Lumen is left out
	// of the picture (far too expensive for four extra views; the station lights carry no bounce light anyway).
	FPostProcessSettings& P = Capture->PostProcessSettings;
	P.bOverride_DynamicGlobalIlluminationMethod = true;
	P.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	P.bOverride_ReflectionMethod = true;
	P.ReflectionMethod = EReflectionMethod::None;
	P.bOverride_ColorSaturation = true;
	P.ColorSaturation = FVector4(0.14f, 0.14f, 0.14f, 1.f);
	P.bOverride_ColorGain = true;
	P.ColorGain = FVector4(0.86f, 1.f, 0.9f, 1.f);
	P.bOverride_ColorContrast = true;
	P.ColorContrast = FVector4(1.18f, 1.18f, 1.18f, 1.f);
	P.bOverride_FilmGrainIntensity = true;
	P.FilmGrainIntensity = 0.55f;
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = 0.8f;
	P.bOverride_BloomIntensity = true;
	P.BloomIntensity = 0.9f;
	P.bOverride_AutoExposureMethod = true;
	P.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	P.bOverride_AutoExposureMinBrightness = true;
	P.bOverride_AutoExposureMaxBrightness = true;
	P.AutoExposureMinBrightness = HCCameraLook::FeedEV100;
	P.AutoExposureMaxBrightness = HCCameraLook::FeedEV100;
	P.bOverride_AutoExposureBias = true;
	P.AutoExposureBias = 0.f;
	Capture->PostProcessBlendWeight = 1.f;
}

void AHCSecurityCamera::Configure(FName InId, const FText& InLabel, uint8 InSector)
{
	CameraId = InId;
	Label = InLabel;
	SectorIndex = InSector;
}

void AHCSecurityCamera::BeginPlay()
{
	Super::BeginPlay();
	// The spawn transform carries the pitch: the head takes it, the rod stays upright
	const FRotator Spawned = GetActorRotation();
	SetActorRotation(FRotator(0.f, Spawned.Yaw, 0.f));
	Head->SetRelativeRotation(FRotator(Spawned.Pitch, 0.f, 0.f));

	const FLinearColor Housing(0.035f, 0.037f, 0.04f);
	Body->SetMaterial(0, HCGreybox::MakeColored(this, Housing));
	Hood->SetMaterial(0, HCGreybox::MakeColored(this, Housing * 0.8f));
	Rod->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.05f, 0.05f, 0.055f)));
	Lens->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.004f, 0.004f, 0.006f)));
	LampMaterial = HCScreens::MakeGlow(this, FLinearColor(0.12f, 0.01f, 0.01f));
	if (LampMaterial)
	{
		Lamp->SetMaterial(0, LampMaterial);
	}
	GetWorldTimerManager().SetTimer(LampTimer, this, &AHCSecurityCamera::UpdateLamp, 0.1f, true, FMath::FRandRange(0.f, 0.1f));
}

bool AHCSecurityCamera::HasSignal() const
{
	const AHCPowerSector* Sector = AHCPowerSector::Find(GetWorld(), SectorIndex);
	return !Sector || Sector->IsPowered();
}

void AHCSecurityCamera::UpdateLamp()
{
	// Among Us: while anybody sits at the monitors, every camera blinks red; ghosts have no seat there
	bool bWatched = false;
	for (TActorIterator<AHCSecurityConsole> It(GetWorld()); It && !bWatched; ++It)
	{
		bWatched = It->GetViewerCount() > 0 && It->IsPowered();
	}
	const bool bOn = bWatched && HasSignal() && FMath::Fmod(GetWorld()->GetTimeSeconds(), 0.9) < 0.45;
	if (bOn == bLampOn)
	{
		return;
	}
	bLampOn = bOn;
	LampLight->SetVisibility(bOn);
	HCScreens::SetGlowColor(LampMaterial, bOn ? FLinearColor(9.f, 0.25f, 0.15f) : FLinearColor(0.12f, 0.01f, 0.01f));
}

UTextureRenderTarget2D* AHCSecurityCamera::GetFeed()
{
	if (!Feed)
	{
		Feed = NewObject<UTextureRenderTarget2D>(this);
		Feed->ClearColor = FLinearColor::Black;
		Feed->InitCustomFormat(HCCameraLook::FeedWidth, HCCameraLook::FeedHeight, PF_B8G8R8A8, false);
		Feed->UpdateResourceImmediate(true);
		Capture->TextureTarget = Feed;
	}
	return Feed;
}

void AHCSecurityCamera::CaptureNow()
{
	if (!GetFeed() || !HasSignal())
	{
		return;
	}
	Capture->CaptureScene();
	LastCapture = GetWorld()->GetTimeSeconds();
}

void AHCSecurityCamera::GetAll(const UWorld* World, TArray<AHCSecurityCamera*>& OutCameras)
{
	OutCameras.Reset();
	for (TActorIterator<AHCSecurityCamera> It(World); It; ++It)
	{
		OutCameras.Add(*It);
	}
	OutCameras.Sort([](const AHCSecurityCamera& A, const AHCSecurityCamera& B) { return A.GetCameraId().LexicalLess(B.GetCameraId()); });
}
