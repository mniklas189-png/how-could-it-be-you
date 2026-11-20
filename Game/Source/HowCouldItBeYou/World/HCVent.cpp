#include "World/HCVent.h"

#include "Abilities/HCKillComponent.h"
#include "Audio/HCProceduralAudio.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Roles/HCJobManager.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UI/HCHUD.h"
#include "UI/HCMonitorWidgets.h"
#include "UI/HCVentWidget.h"
#include "World/HCGreyboxAssets.h"
#include "World/HCModelAssets.h"
#include "World/HCStationBuilder.h"

namespace HCVentRules
{
	/** How close (cm, on the floor plan) the Wandler has to stand to climb in. */
	constexpr float UseRange = 190.f;
	/** Closer than this (cm, on the floor plan) it stands on the hatch or right at its edge: offered wherever it looks. */
	constexpr float UnderfootRange = 115.f;
	/** Further away the hatch has to lie in front: cosine of the largest angle between the way it faces and the hatch. */
	constexpr float InFrontCos = 0.5f;
	/** Holding [E] this long lifts the grille. */
	constexpr float EnterHold = 0.5f;
	/** After climbing in: this long until the first crawl or climbing out again. */
	constexpr float SettleSeconds = 0.6f;
	/** After climbing out of a hatch: this long until the same hatch can be used again. */
	constexpr float ReentrySeconds = 1.5f;
	/** The grille: how far it jumps open (degrees) and how long that takes. */
	constexpr float FlapAngle = 62.f, FlapOpen = 0.25f, FlapHold = 0.4f, FlapShut = 0.55f;
	/** Eyes of an occupant above the floor, cm, and how far they look up. */
	constexpr float EyeHeight = 26.f, EyePitch = 6.f;
}

AHCVent::AHCVent()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	StandDistance = 0.f;
	// The block of the base class stands in for desks and tables. A hatch lies flat and is walked over: no collision
	// at all, and the Wandler finds it by standing next to it (FindUsable) instead of by the interaction ray.
	Block->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Block->SetHiddenInGame(true);
	Block->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.02f));

	auto Part = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		Component->SetStaticMesh(HCGreybox::Cube());
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		return Component;
	};
	Frame = Part(TEXT("Frame"), RootComponent);
	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(RootComponent);
	Grille = Part(TEXT("Grille"), Hinge);

	Eyes = CreateDefaultSubobject<UCameraComponent>(TEXT("Eyes"));
	Eyes->SetupAttachment(RootComponent);
	Eyes->SetRelativeLocation(FVector(0.f, 0.f, HCVentRules::EyeHeight));
	Eyes->SetRelativeRotation(FRotator(HCVentRules::EyePitch, 0.f, 0.f));
	Eyes->SetFieldOfView(100.f);
	Eyes->bConstrainAspectRatio = false;
}

void AHCVent::Configure(FName InId, int32 InGroup)
{
	VentId = InId;
	Group = InGroup;
}

void AHCVent::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// Faster than the desks: nobody may sit hidden in a shaft for half a second after a meeting began
		GetWorldTimerManager().SetTimer(ServerTimer, this, &AHCVent::ServerTick, 0.2f, true, 0.2f);
	}
	const UHCModelSet* Models = HCModels::Get();
	UStaticMesh* FrameModel = Models ? Models->FindProp(TEXT("VentFrame")) : nullptr;
	UStaticMesh* GrilleModel = Models ? Models->FindProp(TEXT("VentGrille")) : nullptr;
	if (FrameModel && GrilleModel)
	{
		// The game's own models (Tools\Models): the frame stands on the floor, the grille lies in it. The grille's
		// origin is its hinge and it reaches along its own Y; here it is turned to reach towards +X.
		const FBox FrameBox = FrameModel->GetBoundingBox();
		const FBox GrilleBox = GrilleModel->GetBoundingBox();
		const bool bTowardsPlusY = FMath::Abs(GrilleBox.Max.Y) > FMath::Abs(GrilleBox.Min.Y);
		Frame->SetStaticMesh(FrameModel);
		Frame->SetRelativeLocation(FVector::ZeroVector);
		Frame->SetRelativeScale3D(FVector::OneVector);
		Hinge->SetRelativeLocation(FVector(-GrilleBox.GetSize().Y * 0.5, 0.0, FrameBox.Max.Z - GrilleBox.Max.Z));
		Grille->SetStaticMesh(GrilleModel);
		Grille->SetRelativeScale3D(FVector::OneVector);
		Grille->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator(0.f, bTowardsPlusY ? -90.f : 90.f, 0.f));
	}
	else
	{
		// Greybox: a dark frame flat on the floor and a lighter plate as the grille
		Frame->SetRelativeLocation(FVector(0.f, 0.f, 1.5f));
		Frame->SetRelativeScale3D(FVector(1.f, 1.f, 0.03f));
		Frame->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.012f, 0.013f, 0.015f)));
		Hinge->SetRelativeLocation(FVector(-44.f, 0.f, 4.25f));
		Grille->SetRelativeLocation(FVector(44.f, 0.f, 0.f));
		Grille->SetRelativeScale3D(FVector(0.88f, 0.88f, 0.025f));
		Grille->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.07f, 0.075f, 0.08f)));
	}
#if !UE_BUILD_SHIPPING
	// -HCVentOpen: every grille stands open, for pictures of the hinge side (camera tours)
	if (FParse::Param(FCommandLine::Get(), TEXT("HCVentOpen")))
	{
		Hinge->SetRelativeRotation(FRotator(HCVentRules::FlapAngle, 0.f, 0.f));
	}
#endif
}

FName AHCVent::GetRoomId() const
{
	const AHCStationBuilder* Builder = FindBuilder(GetWorld());
	return Builder ? Builder->GetSpaceNameAt(GetActorLocation()) : NAME_None;
}

void AHCVent::GetLinkedVents(TArray<AHCVent*>& OutLinked) const
{
	OutLinked.Reset();
	for (TActorIterator<AHCVent> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->Group == Group)
		{
			OutLinked.Add(*It);
		}
	}
	const FVector Here = GetActorLocation();
	OutLinked.Sort([&Here](const AHCVent& A, const AHCVent& B) { return FVector::DistSquared(A.GetActorLocation(), Here) < FVector::DistSquared(B.GetActorLocation(), Here); });
}

float AHCVent::CrawlSeconds(const AHCVent* From, const AHCVent* To, bool bTechnician)
{
	// About 14 m/s plus the time to get going: across the whole station in four seconds, where walking takes thirty
	const float Distance = From && To ? FVector::Dist(From->GetActorLocation(), To->GetActorLocation()) : 0.f;
	return FMath::Clamp(0.9f + Distance / 1400.f, 1.2f, 4.f) * (bTechnician ? TechnicianSlowdown : 1.f);
}

bool AHCVent::KnowsShafts(const AHCCharacter* Who)
{
	const AHCPlayerState* PS = Who ? Who->GetPlayerState<AHCPlayerState>() : nullptr;
	const AHCGameState* GS = Who && Who->GetWorld() ? Who->GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!PS || !PS->IsAlive())
	{
		return false;
	}
	// The own role is the only role a client knows: for everybody else a hatch is a part of the floor
	return PS->GetRoleInfo().Role == EHCRole::Monster
		|| (PS->GetRoleInfo().Role == EHCRole::Crew && PS->GetRoleInfo().Job == EHCJob::Technician && GS && GS->AreJobsEnabled());
}

bool AHCVent::MayUseShafts(const AHCCharacter* Who)
{
	// Nobody crawls with a container in both hands
	return Who && !Who->IsCarrying() && KnowsShafts(Who);
}

AHCVent* AHCVent::FindUsable(const AHCCharacter* Who, bool bOnlyUnderfoot)
{
	using namespace HCVentRules;
	const UWorld* World = Who ? Who->GetWorld() : nullptr;
	// The own role is the only role a client knows: for the crew no hatch ever lights up
	if (!World || !MayUseShafts(Who) || Who->IsInVent())
	{
		return nullptr;
	}
	// The way the figure faces, flat: a hatch lies at the feet, so where the eyes point up or down does not matter
	const FVector2D Facing = FVector2D(Who->GetActorForwardVector()).GetSafeNormal();
	const FVector2D Feet(Who->GetActorLocation());
	AHCVent* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<AHCVent> It(World); It; ++It)
	{
		const FVector2D Offset = FVector2D(It->GetActorLocation()) - Feet;
		const double Distance = Offset.Size();
		const bool bUnderfoot = Distance <= UnderfootRange;
		if (Distance >= BestDistance || (bOnlyUnderfoot && !bUnderfoot))
		{
			continue;
		}
		if (!bUnderfoot && FVector2D::DotProduct(Offset / Distance, Facing) < InFrontCos)
		{
			continue;
		}
		FText Reason;
		if (It->CanInteract(Who, Reason))
		{
			BestDistance = Distance;
			Best = *It;
		}
	}
	return Best;
}

AHCVent* AHCVent::FindVentOf(const UWorld* World, const AHCCharacter* Who)
{
	for (TActorIterator<AHCVent> It(World); It; ++It)
	{
		if (It->FindOccupant(Who) != INDEX_NONE)
		{
			return *It;
		}
	}
	return nullptr;
}

double AHCVent::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

int32 AHCVent::FindOccupant(const AHCCharacter* Who) const
{
	return Occupants.IndexOfByPredicate([Who](const FOccupant& Entry) { return Entry.Who.Get() == Who; });
}

bool AHCVent::CanInteract(const AHCCharacter* Who, FText& OutReason) const
{
	// Never a reason: for the crew a hatch is just a part of the floor
	OutReason = FText::GetEmpty();
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const AHCPlayerState* PS = Who ? Who->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!Who || !PS || !GS || !MayUseShafts(Who) || GS->GetMatchPhase() != EHCMatchPhase::Playing)
	{
		return false;
	}
	if (Who->IsInVent())
	{
		// From inside: crawl here from another hatch of the same shaft (only the server knows who sits where)
		if (!HasAuthority())
		{
			return false;
		}
		const AHCVent* From = FindVentOf(GetWorld(), Who);
		const int32 Index = From ? From->FindOccupant(Who) : INDEX_NONE;
		return From && From != this && From->Group == Group && ServerNow() >= From->Occupants[Index].ReadyAt && !From->Occupants[Index].bLeaving;
	}
	if (!Who->CanAct() || FVector::DistSquared2D(Who->GetActorLocation(), GetActorLocation()) > FMath::Square(HCVentRules::UseRange))
	{
		return false;
	}
	if (HasAuthority())
	{
		if (const double* Last = LastExit.Find(Who); Last && ServerNow() - *Last < HCVentRules::ReentrySeconds)
		{
			return false;
		}
	}
	return true;
}

FText AHCVent::GetInteractionText(const AHCCharacter* Who) const
{
	return NSLOCTEXT("HC", "VentEnter", "In den Schacht steigen");
}

float AHCVent::GetHoldDuration(const AHCCharacter* Who) const
{
	return Who && Who->IsInVent() ? 0.f : HCVentRules::EnterHold;
}

UPrimitiveComponent* AHCVent::GetHighlightComponent() const
{
	return Grille;
}

void AHCVent::ExecuteInteraction(AHCCharacter* Who)
{
	if (!Who || !HasAuthority())
	{
		return;
	}
	if (Who->IsInVent())
	{
		AHCVent* From = FindVentOf(GetWorld(), Who);
		if (From && From != this && From->Group == Group)
		{
			From->ServerMoveTo(Who, this);
		}
		return;
	}
	ServerEnter(Who);
}

void AHCVent::PlaceOccupant(AHCCharacter* Who) const
{
	// The hidden figure waits right on the hatch: that is where it comes out again
	Who->TeleportTo(GetActorLocation() + FVector(0.f, 0.f, Who->GetSimpleCollisionHalfHeight() + 6.f), FRotator(0.f, GetActorRotation().Yaw, 0.f), false, true);
}

void AHCVent::ServerEnter(AHCCharacter* Who)
{
	FOccupant Entry;
	Entry.Who = Who;
	Entry.EnteredAt = ServerNow();
	Entry.ReadyAt = Entry.EnteredAt + HCVentRules::SettleSeconds;
	Occupants.Add(Entry);
	Who->ServerSetInVent(true);
	PlaceOccupant(Who);
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(Who->GetController()))
	{
		PC->ClientOpenDeviceView(this);
	}
	MulticastFlap();
	UE_LOG(LogHC, Log, TEXT("HC_VENT: %s climbs into %s (%s)"), *Who->GetName(), *VentId.ToString(), *GetRoomId().ToString());
}

void AHCVent::ServerMoveTo(AHCCharacter* Who, AHCVent* Target)
{
	const int32 Index = FindOccupant(Who);
	if (Index == INDEX_NONE || !Target)
	{
		return;
	}
	FOccupant Entry = Occupants[Index];
	Occupants.RemoveAt(Index);
	const bool bTechnician = AHCJobManager::HasJob(Who, EHCJob::Technician);
	Entry.ReadyAt = ServerNow() + CrawlSeconds(this, Target, bTechnician);
	Entry.bLeaving = false;
	Target->Occupants.Add(Entry);
	Target->PlaceOccupant(Who);
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(Who->GetController()))
	{
		// The HUD swaps the view; its "close" for this hatch finds nobody here any more and does nothing
		PC->ClientOpenDeviceView(Target);
	}
	MulticastRattle(bTechnician);
	Target->MulticastRattle(bTechnician);
	UE_LOG(LogHC, Log, TEXT("HC_VENT: %s crawls from %s to %s (%.1f s)%s"), *Who->GetName(), *VentId.ToString(), *Target->VentId.ToString(),
		CrawlSeconds(this, Target, bTechnician), bTechnician ? TEXT(" as technician") : TEXT(""));
}

void AHCVent::ServerExit(int32 OccupantIndex, bool bTellClient)
{
	if (!Occupants.IsValidIndex(OccupantIndex))
	{
		return;
	}
	const FOccupant Entry = Occupants[OccupantIndex];
	Occupants.RemoveAt(OccupantIndex);
	AHCCharacter* Who = Entry.Who.Get();
	if (!Who)
	{
		return;
	}
	const double Now = ServerNow();
	// The kill cooldown stood still inside the shaft (Among Us): what was left when going in is left now
	if (UHCKillComponent* Kill = Who->GetKillComponent(); Kill && Kill->GetReadyTime() > Entry.EnteredAt)
	{
		Kill->ServerSetReadyTime(Kill->GetReadyTime() + (Now - Entry.EnteredAt));
	}
	Who->ServerSetInVent(false);
	LastExit.Add(Who, Now);
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(Who->GetController()); PC && bTellClient)
	{
		PC->ClientCloseDeviceView();
	}
	MulticastFlap();
	UE_LOG(LogHC, Log, TEXT("HC_VENT: %s climbs out of %s (%s) after %.1f s"), *Who->GetName(), *VentId.ToString(), *GetRoomId().ToString(), Now - Entry.EnteredAt);
}

void AHCVent::ServerRemoveViewer(AHCCharacter* Who, bool bTellClient)
{
	const int32 Index = HasAuthority() ? FindOccupant(Who) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return;
	}
	if (ServerNow() < Occupants[Index].ReadyAt)
	{
		// Still crawling: out as soon as it has arrived (ServerTick), so nobody skips the way
		Occupants[Index].bLeaving = true;
		Occupants[Index].bTellClient |= bTellClient;
		return;
	}
	ServerExit(Index, bTellClient);
}

void AHCVent::ServerTick()
{
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	const bool bRound = GS && GS->GetMatchPhase() == EHCMatchPhase::Playing;
	const double Now = ServerNow();
	for (int32 Index = Occupants.Num() - 1; Index >= 0; --Index)
	{
		const FOccupant& Entry = Occupants[Index];
		const AHCCharacter* Who = Entry.Who.Get();
		const AHCPlayerState* PS = Who ? Who->GetPlayerState<AHCPlayerState>() : nullptr;
		if (!Who)
		{
			Occupants.RemoveAt(Index);
		}
		else if (!bRound || !PS || !PS->IsAlive())
		{
			// A meeting, the end of the round: everybody out at once, wherever they are by now
			ServerExit(Index, true);
		}
		else if (Entry.bLeaving && Now >= Entry.ReadyAt)
		{
			ServerExit(Index, Entry.bTellClient);
		}
	}
}

UHCDeviceViewWidget* AHCVent::CreateView(APlayerController* PC)
{
	UHCVentViewWidget* View = CreateWidget<UHCVentViewWidget>(PC, UHCVentViewWidget::StaticClass());
	if (View)
	{
		View->Setup(this);
		// The player's eyes move into the shaft; Tick gives them back when the HUD shows another view or none
		ResetView();
		bLocalView = true;
		SetActorTickEnabled(true);
		PC->SetViewTargetWithBlend(this, 0.f);
	}
	return View;
}

void AHCVent::AddViewYaw(float Degrees)
{
	ViewYaw = FRotator::NormalizeAxis(ViewYaw + Degrees);
	Eyes->SetRelativeRotation(FRotator(HCVentRules::EyePitch, ViewYaw, 0.f));
}

void AHCVent::ResetView()
{
	ViewYaw = 0.f;
	Eyes->SetRelativeRotation(FRotator(HCVentRules::EyePitch, 0.f, 0.f));
}

void AHCVent::MulticastFlap_Implementation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	FlapStart = GetWorld()->GetTimeSeconds();
	SetActorTickEnabled(true);
	HCProceduralAudio::PlayAtLocation(this, HCProceduralAudio::ESound::VentClank, GetActorLocation() + FVector(0.f, 0.f, 10.f), 0.9f, 1600.f);
}

void AHCVent::MulticastRattle_Implementation(bool bLoud)
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		// The technician is no climber: louder, and heard further
		HCProceduralAudio::PlayAtLocation(this, HCProceduralAudio::ESound::VentCrawl, GetActorLocation() + FVector(0.f, 0.f, 10.f), bLoud ? 1.f : 0.8f, bLoud ? 1800.f : 1100.f);
	}
}

void AHCVent::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	using namespace HCVentRules;
	if (FlapStart >= 0.f)
	{
		// Thrown open, a moment in the air, then it falls shut faster and faster (the clank of the sound sits there)
		const float T = GetWorld()->GetTimeSeconds() - FlapStart;
		float Angle = 0.f;
		if (T < FlapOpen)
		{
			Angle = FlapAngle * FMath::Sin(0.5f * PI * T / FlapOpen);
		}
		else if (T < FlapHold)
		{
			Angle = FlapAngle;
		}
		else if (T < FlapShut)
		{
			Angle = FlapAngle * (1.f - FMath::Square((T - FlapHold) / (FlapShut - FlapHold)));
		}
		else
		{
			FlapStart = -1.f;
		}
		Hinge->SetRelativeRotation(FRotator(Angle, 0.f, 0.f));
	}
	if (bLocalView)
	{
		// The HUD decides what the player looks at: as soon as its view is not this hatch any more, the eyes go back
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr;
		const UHCDeviceViewWidget* Open = HUD ? HUD->GetDeviceView() : nullptr;
		if (!Open || Open->GetDevice() != this)
		{
			bLocalView = false;
			if (PC && PC->GetViewTarget() == this)
			{
				PC->SetViewTargetWithBlend(PC->GetPawn(), 0.f);
			}
		}
	}
	if (FlapStart < 0.f && !bLocalView)
	{
		SetActorTickEnabled(false);
	}
}

void AHCVent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHCVent, VentId);
	DOREPLIFETIME(AHCVent, Group);
}
