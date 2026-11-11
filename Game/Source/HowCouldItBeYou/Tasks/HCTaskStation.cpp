#include "Tasks/HCTaskStation.h"

#include "Characters/HCCharacter.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCGameSettings.h"
#include "Core/HCLog.h"
#include "World/HCModelAssets.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskTypes.h"
#include "TimerManager.h"
#include "World/HCGreyboxAssets.h"

AHCTaskStation::AHCTaskStation()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetDormancy(DORM_DormantAll);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// Console 60 cm deep, 120 cm wide, 110 cm tall; the actor origin is its centre, +X is the front
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetStaticMesh(HCGreybox::Cube());
	Body->SetRelativeScale3D(FVector(0.6f, 1.2f, 1.1f));
	Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Body->SetCollisionResponseToChannel(HC_TRACE_INTERACTION, ECR_Block);

	Screen = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Screen"));
	Screen->SetupAttachment(Root);
	Screen->SetStaticMesh(HCGreybox::Cube());
	Screen->SetRelativeLocation(FVector(30.5f, 0.f, 30.f));
	Screen->SetRelativeScale3D(FVector(0.03f, 0.9f, 0.35f));
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Screen->SetCanEverAffectNavigation(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Root);
	Label->SetRelativeLocation(FVector(-27.f, 0.f, 100.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(20.f);
	Label->SetTextRenderColor(FColor(170, 180, 190));
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Root);
	Glow->SetRelativeLocation(FVector(70.f, 0.f, 40.f));
	Glow->SetIntensityUnits(ELightUnits::Candelas);
	Glow->SetIntensity(0.f);
	Glow->SetAttenuationRadius(180.f);
	Glow->SetCastShadows(false);
}

void AHCTaskStation::Configure(FName InTaskId, int32 InStepIndex)
{
	TaskId = InTaskId;
	StepIndex = InStepIndex;
}

const FHCTaskDefinition* AHCTaskStation::GetDefinition() const
{
	return UHCGameSettings::Get()->FindTask(TaskId);
}

const FHCTaskStepDef* AHCTaskStation::GetStepDef() const
{
	const FHCTaskDefinition* Def = GetDefinition();
	return Def && Def->Steps.IsValidIndex(StepIndex) ? &Def->Steps[StepIndex] : nullptr;
}

FVector AHCTaskStation::GetStandLocation() const
{
	// Actor origin is 55 cm above the floor; a standing capsule centre is 92 cm above it
	return GetActorLocation() + GetActorForwardVector() * 110.f + FVector(0.f, 0.f, 37.f);
}

void AHCTaskStation::BeginPlay()
{
	Super::BeginPlay();
	ScreenMaterial = HCGreybox::MakeColored(this, FLinearColor(0.05f, 0.06f, 0.08f));
	if (ScreenMaterial)
	{
		Screen->SetMaterial(0, ScreenMaterial);
	}
	if (UMaterialInstanceDynamic* BodyMaterial = HCGreybox::MakeColored(this, FLinearColor(0.2f, 0.21f, 0.24f)))
	{
		Body->SetMaterial(0, BodyMaterial);
	}
	// A real machine from the model packs instead of the grey block. Which one follows from the place, so every
	// computer picks the same without asking the host.
	const UHCModelSet* Models = HCModels::Get();
	int32 Variants = 0;
	while (Models && Models->Props.Contains(*FString::Printf(TEXT("Task%d"), Variants)))
	{
		++Variants;
	}
	if (Variants > 0)
	{
		const FVector Place = GetActorLocation();
		const int32 Variant = FMath::Abs(FMath::RoundToInt32(Place.X / 100.0) * 7 + FMath::RoundToInt32(Place.Y / 100.0) * 13) % Variants;
		const FBox Fitted = HCModels::Dress(this, *FString::Printf(TEXT("Task%d"), Variant), FBox(FVector(-30.0, -60.0, -55.0), FVector(30.0, 60.0, 80.0)));
		if (Fitted.IsValid)
		{
			Body->SetVisibility(false); // stays as collision
			// The screen becomes a status strip on the front of the machine
			Screen->SetRelativeLocation(FVector(Fitted.Max.X + 1.0, 0.0, Fitted.Max.Z - 14.0));
			Screen->SetRelativeScale3D(FVector(0.02, FMath::Max(0.2, Fitted.GetSize().Y * 0.006), 0.06));
		}
	}
	OnRep_Task();
	if (GetNetMode() != NM_DedicatedServer)
	{
		GetWorldTimerManager().SetTimer(IndicatorTimer, this, &AHCTaskStation::UpdateLocalIndicator, 0.5f, true, 0.5f);
	}
}

void AHCTaskStation::OnRep_Task()
{
	const FHCTaskDefinition* Def = GetDefinition();
	Label->SetText(Def ? FText::FromString(Def->DisplayName) : FText::FromName(TaskId));
}

UHCTaskComponent* AHCTaskStation::GetTasksOf(const AHCCharacter* Who)
{
	const AHCPlayerState* PS = Who ? Who->GetPlayerState<AHCPlayerState>() : nullptr;
	return PS ? PS->GetTaskComponent() : nullptr;
}

void AHCTaskStation::UpdateLocalIndicator()
{
	// 0 = nothing here for me, 1 = my step is here, 2 = my step is here but still waiting, 3 = I am done here
	int32 State = 0;
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	if (const UHCTaskComponent* Tasks = PS ? PS->GetTaskComponent() : nullptr)
	{
		for (const FHCTaskEntry& Entry : Tasks->GetTasks())
		{
			if (Entry.TaskId != TaskId)
			{
				continue;
			}
			if (Entry.bCompleted || Entry.CurrentStep > StepIndex)
			{
				State = 3;
			}
			else if (Entry.CurrentStep == StepIndex)
			{
				State = Tasks->GetWaitSeconds(Entry) > 0.f ? 2 : 1;
			}
		}
	}
	if (State == LastIndicatorState)
	{
		return;
	}
	LastIndicatorState = State;
	static const FLinearColor Colors[4] = {
		FLinearColor(0.05f, 0.06f, 0.08f), FLinearColor(1.f, 0.75f, 0.1f), FLinearColor(0.2f, 0.45f, 1.f), FLinearColor(0.15f, 0.9f, 0.3f) };
	if (ScreenMaterial)
	{
		ScreenMaterial->SetVectorParameterValue(TEXT("Color"), Colors[State]);
	}
	Glow->SetLightColor(Colors[State]);
	Glow->SetIntensity(State == 0 ? 0.f : State == 3 ? 0.06f : 0.2f);
}

bool AHCTaskStation::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	if (!Who || !Who->CanAct())
	{
		return false;
	}
	const UHCTaskComponent* Tasks = GetTasksOf(Who);
	float Wait = 0.f;
	if (!Tasks || Tasks->FindPendingIndexAt(this, &Wait) == INDEX_NONE)
	{
		return false; // not my task: no hint at all
	}
	if (Wait > 0.f)
	{
		OutReason = FText::Format(NSLOCTEXT("HC", "TaskWait", "Noch nicht bereit ({0} s)"), FMath::CeilToInt(Wait));
		return false;
	}
	return true;
}

FText AHCTaskStation::GetInteractionText(const AHCCharacter* Who) const
{
	const FHCTaskStepDef* Step = GetStepDef();
	if (!Step)
	{
		return FText::GetEmpty();
	}
	const FText StepLabel = FText::FromString(Step->Label);
	return Step->Overlay == EHCTaskOverlayKind::None ? FText::Format(NSLOCTEXT("HC", "TaskHold", "halten: {0}"), StepLabel) : StepLabel;
}

float AHCTaskStation::GetHoldDuration(const AHCCharacter* Who) const
{
	const FHCTaskStepDef* Step = GetStepDef();
	return Step && Step->Overlay == EHCTaskOverlayKind::None ? Step->Duration : 0.f;
}

void AHCTaskStation::ExecuteInteraction(AHCCharacter* Who)
{
	UHCTaskComponent* Tasks = GetTasksOf(Who);
	const FHCTaskStepDef* Step = GetStepDef();
	if (!Tasks || !Step)
	{
		return;
	}
	if (Step->Overlay == EHCTaskOverlayKind::None)
	{
		Tasks->ServerCompleteHoldStep(this);
	}
	else if (Tasks->ServerBeginTimedStep(this))
	{
		if (AHCPlayerController* PC = Cast<AHCPlayerController>(Who->GetController()))
		{
			PC->ClientOpenTaskOverlay(this);
		}
	}
}

UPrimitiveComponent* AHCTaskStation::GetHighlightComponent() const
{
	return Body;
}

void AHCTaskStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCTaskStation, TaskId);
	DOREPLIFETIME(AHCTaskStation, StepIndex);
}
