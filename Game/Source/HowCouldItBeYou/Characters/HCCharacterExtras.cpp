// AHCCharacter, the parts added with jobs, carry tasks, emotes and the wardrobe: outfit colours, the biologist's UV
// light, the carried container, emotes and the ability key. The rest of the class is in HCCharacter.cpp.
#include "Characters/HCCharacter.h"

#include "Camera/CameraComponent.h"
#include "Characters/HCOutfit.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/HCGameState.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Roles/HCJobManager.h"
#include "Roles/HCJobs.h"
#include "UI/HCHUD.h"
#include "World/HCGreyboxAssets.h"
#include "World/HCModelAssets.h"

namespace HCCarry
{
	/** The container in front of the belly, in actor space (cm): centre, half its width (the hands hold its sides). */
	const FVector Centre(34.f, 0.f, 10.f);
	constexpr float HalfWidth = 21.f;
	/** Scale of the crate model (1 m wide) for a container one carries. */
	constexpr float Scale = 0.42f;
	/** At the bottom of the own view, in camera space. */
	const FVector ViewAt(46.f, 0.f, -40.f);
}

namespace HCOutfit
{
	FText PartName(EPart Part)
	{
		switch (Part)
		{
		case EPart::Trousers: return NSLOCTEXT("HC", "OutfitTrousers", "Hose");
		case EPart::Hair: return NSLOCTEXT("HC", "OutfitHair", "Haare");
		case EPart::Boots: return NSLOCTEXT("HC", "OutfitBoots", "Stiefel");
		default: return NSLOCTEXT("HC", "OutfitBackpack", "Rucksack");
		}
	}

	FText ChoiceName(EPart Part, int32 Choice)
	{
		switch (Part)
		{
		case EPart::Trousers:
		{
			const FText Names[] = { NSLOCTEXT("HC", "OutfitJeans", "Jeans"), NSLOCTEXT("HC", "OutfitBlack", "Schwarz"), NSLOCTEXT("HC", "OutfitKhaki", "Khaki"),
				NSLOCTEXT("HC", "OutfitGrey", "Grau") };
			return Names[FMath::Clamp(Choice, 0, 3)];
		}
		case EPart::Hair:
		{
			const FText Names[] = { NSLOCTEXT("HC", "OutfitBrown", "Braun"), NSLOCTEXT("HC", "OutfitBlack", "Schwarz"), NSLOCTEXT("HC", "OutfitBlond", "Blond"),
				NSLOCTEXT("HC", "OutfitRed", "Rot"), NSLOCTEXT("HC", "OutfitGrey", "Grau") };
			return Names[FMath::Clamp(Choice, 0, 4)];
		}
		case EPart::Boots:
		{
			const FText Names[] = { NSLOCTEXT("HC", "OutfitWorn", "Abgetragen"), NSLOCTEXT("HC", "OutfitBlack", "Schwarz"), NSLOCTEXT("HC", "OutfitLeather", "Leder") };
			return Names[FMath::Clamp(Choice, 0, 2)];
		}
		default:
			return Choice == 0 ? NSLOCTEXT("HC", "OutfitWorn2", "Aufgesetzt") : NSLOCTEXT("HC", "OutfitNone", "Ohne");
		}
	}
}

void AHCCharacter::CreateExtraComponents()
{
	auto MakeProp = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		Component->SetVisibility(false);
		return Component;
	};
	CarriedBox = MakeProp(TEXT("CarriedBox"), GetCapsuleComponent());
	CarriedBox->SetOwnerNoSee(true);
	CarriedBoxView = MakeProp(TEXT("CarriedBoxView"), Camera);
	CarriedBoxView->SetOnlyOwnerSee(true);
	CarriedBoxView->SetCastShadow(false);

	EmoteTag = CreateDefaultSubobject<UTextRenderComponent>(TEXT("EmoteTag"));
	EmoteTag->SetupAttachment(GetCapsuleComponent());
	EmoteTag->SetRelativeLocation(FVector(0.f, 0.f, 127.f));
	EmoteTag->SetHorizontalAlignment(EHTA_Center);
	EmoteTag->SetVerticalAlignment(EVRTA_TextCenter);
	EmoteTag->SetWorldSize(12.f);
	EmoteTag->SetTextRenderColor(FColor(255, 196, 70));
	EmoteTag->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EmoteTag->SetOwnerNoSee(true);
	EmoteTag->SetVisibility(false);
}

// ---------------------------------------------------------------------------
// Outfit (wardrobe)

void AHCCharacter::ApplyOutfit()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!bRealModel || !Models || !Body || Body->GetSkeletalMeshAsset() != Models->CharacterMesh || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const uint8 Outfit = Appearance.Outfit;
	auto Material = [this, Body](FName Slot) -> UMaterialInstanceDynamic*
	{
		const int32 Index = Body->GetMaterialIndex(Slot);
		if (Index == INDEX_NONE)
		{
			return nullptr;
		}
		// One own copy per slot; after the monster form the body has fresh materials again
		UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(Index));
		if (Existing && OutfitMaterials.Contains(Existing))
		{
			return Existing;
		}
		UMaterialInstanceDynamic* Made = Body->CreateDynamicMaterialInstance(Index);
		OutfitMaterials.Add(Made);
		return Made;
	};
	if (UMaterialInstanceDynamic* Jeans = Material(TEXT("Jeans1")))
	{
		Jeans->SetVectorParameterValue(TEXT("Tint"), HCOutfit::TrouserTint(HCOutfit::Get(Outfit, HCOutfit::EPart::Trousers)));
	}
	if (UMaterialInstanceDynamic* Shoes = Material(TEXT("Shoes1")))
	{
		Shoes->SetVectorParameterValue(TEXT("Tint"), HCOutfit::BootTint(HCOutfit::Get(Outfit, HCOutfit::EPart::Boots)));
	}
	FLinearColor Root, Tip;
	HCOutfit::HairColors(HCOutfit::Get(Outfit, HCOutfit::EPart::Hair), Root, Tip);
	for (const FName Slot : { FName(TEXT("Hair")), FName(TEXT("Hair_Cap1")) })
	{
		if (UMaterialInstanceDynamic* Hair = Material(Slot))
		{
			Hair->SetVectorParameterValue(TEXT("RootColor"), Root);
			Hair->SetVectorParameterValue(TEXT("TipColor"), Tip);
		}
	}
	// The backpack: its part of the model is simply not drawn
	const int32 PackIndex = Body->GetMaterialIndex(TEXT("Backpack1"));
	const bool bPack = HCOutfit::Get(Outfit, HCOutfit::EPart::Backpack) == 0;
	if (FSkeletalMeshRenderData* RenderData = PackIndex != INDEX_NONE ? Body->GetSkeletalMeshRenderData() : nullptr)
	{
		for (int32 Lod = 0; Lod < RenderData->LODRenderData.Num(); ++Lod)
		{
			const TArray<FSkelMeshRenderSection>& Sections = RenderData->LODRenderData[Lod].RenderSections;
			for (int32 Section = 0; Section < Sections.Num(); ++Section)
			{
				if (Sections[Section].MaterialIndex == PackIndex)
				{
					Body->ShowMaterialSection(PackIndex, Section, bPack, Lod);
				}
			}
		}
	}
}

void AHCCharacter::SetMirrorView(bool bOn)
{
	bMirrorView = bOn;
	// Everything the owner normally does not see of the own figure; parts that are not shown stay hidden anyway
	UPrimitiveComponent* const Parts[] = { GetMesh(), BodyMesh.Get(), HeadMesh.Get(), VisorMesh.Get(), ChestPlate.Get(), BackPlate.Get(),
		ChestLabel.Get(), BackLabel.Get(), FlashlightHandModel.Get(), CarriedBox.Get() };
	for (UPrimitiveComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetOwnerNoSee(!bOn);
		}
	}
}

// ---------------------------------------------------------------------------
// Biologist's UV light

void AHCCharacter::ServerSetUVLight(bool bOn)
{
	if (!HasAuthority() || bUVLight == bOn)
	{
		return;
	}
	bUVLight = bOn && !bCarrying;
	OnRep_UVLight();
}

void AHCCharacter::OnRep_UVLight()
{
	// Black light: violet and weaker - slime glows under it (AHCSlimeTrail asks the local figure)
	Flashlight->SetLightColor(bUVLight ? FLinearColor(0.42f, 0.1f, 1.f) : FLinearColor(1.f, 0.95f, 0.85f));
	Flashlight->SetIntensityUnits(ELightUnits::Candelas);
}

// ---------------------------------------------------------------------------
// Carrying

void AHCCharacter::ServerSetCarrying(bool bOn)
{
	if (!HasAuthority() || bCarrying == bOn)
	{
		return;
	}
	bCarrying = bOn;
	if (bOn)
	{
		// Both hands on the container: the flashlight goes out
		bFlashlightOn = false;
		OnRep_FlashlightOn();
		bUVLight = false;
		OnRep_UVLight();
	}
	OnRep_Carrying();
}

void AHCCharacter::OnRep_Carrying()
{
	const UHCModelSet* Set = HCModels::Get();
	float OwnYaw = 0.f;
	UStaticMesh* Crate = Set ? Set->FindProp(TEXT("Crate"), &OwnYaw) : nullptr;
	for (UStaticMeshComponent* Box : { CarriedBox.Get(), CarriedBoxView.Get() })
	{
		if (!Box->GetStaticMesh())
		{
			if (Crate)
			{
				// The crate stands on its own origin; its front (+X after OwnYaw) looks away from the carrier
				Box->SetStaticMesh(Crate);
				Box->SetRelativeScale3D(FVector(HCCarry::Scale));
				Box->SetRelativeRotation(FRotator(0.f, OwnYaw, 0.f));
			}
			else
			{
				Box->SetStaticMesh(HCGreybox::Cube());
				Box->SetRelativeScale3D(FVector(0.34f, 0.42f, 0.34f));
				Box->SetMaterial(0, HCGreybox::MakeColored(this, FLinearColor(0.3f, 0.32f, 0.33f)));
			}
		}
	}
	const float Lift = Crate ? 17.f : 0.f;
	CarriedBox->SetRelativeLocation(HCCarry::Centre - FVector(0.f, 0.f, Lift));
	CarriedBoxView->SetRelativeLocation(HCCarry::ViewAt);
	// In the true form of the Wandler there are no hands to hold it
	CarriedBox->SetVisibility(bCarrying && bAlive && !bMonsterForm);
	CarriedBoxView->SetVisibility(bCarrying && bAlive && !bMonsterForm && IsLocallyControlled());
}

bool AHCCharacter::EditCarryPose(TArray<FTransform>& Pose)
{
	if (!bFlashlightHandBones || !bAlive || bMonsterForm || !Models || GetMesh()->GetSkeletalMeshAsset() != Models->CharacterMesh)
	{
		return false;
	}
	// Both hands at the sides of the container, the handles along the carrier's forward direction
	const FTransform ActorToBody = GetActorTransform().GetRelativeTransform(GetMesh()->GetComponentTransform());
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float Y = (Side == 0 ? 1.f : -1.f) * (HCCarry::HalfWidth + 2.5f);
		const FTransform Handle = FTransform(FRotator(0.f, 0.f, Side == 0 ? -90.f : 90.f), HCCarry::Centre + FVector(0.f, Y, 0.f)) * ActorToBody;
		const FVector Elbow = ActorToBody.TransformVectorNoScale(FVector(-0.2f, Side == 0 ? 1.f : -1.f, -1.f).GetSafeNormal());
		HCFlashlightGrip::Apply(FlashlightGrips[Side], Pose, &Handle, Elbow, 1.f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Emotes

void AHCCharacter::RequestEmote(EHCEmote Emote)
{
	if (!bAlive || bInVent || Emote >= EHCEmote::Count)
	{
		return;
	}
	if (bCarrying && HCGesture::UsesArms(Emote))
	{
		if (AHCHUD* HUD = GetController<APlayerController>() ? GetController<APlayerController>()->GetHUD<AHCHUD>() : nullptr)
		{
			HUD->ShowCenterMessage(NSLOCTEXT("HC", "CarryNoLight", "Beide Hände sind voll."), 1.5f);
		}
		return;
	}
	ServerPlayEmote(static_cast<uint8>(Emote));
}

void AHCCharacter::ServerPlayEmote_Implementation(uint8 Emote)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const EHCEmote Which = static_cast<EHCEmote>(Emote);
	if (Which >= EHCEmote::Count || !CanAct() || bInVent || (bCarrying && HCGesture::UsesArms(Which)) || Now - LastEmoteRequest < 1.5)
	{
		return;
	}
	LastEmoteRequest = Now;
	MulticastPlayEmote(Emote);
}

void AHCCharacter::MulticastPlayEmote_Implementation(uint8 Emote)
{
	const EHCEmote Which = static_cast<EHCEmote>(Emote);
	if (Which >= EHCEmote::Count)
	{
		return;
	}
	ActiveEmote = Which;
	EmoteStart = GetWorld()->GetTimeSeconds();
	EmoteTag->SetText(HCGesture::Shout(Which));
	EmoteTag->SetVisibility(bAlive && !IsHidden());
	UE_LOG(LogHC, Log, TEXT("HC_EMOTE: %s plays %s"), *GetName(), *HCGesture::Name(Which).ToString());
}

void AHCCharacter::EditGesturePose(TArray<FTransform>& Pose)
{
	if (ActiveEmote >= EHCEmote::Count || !bAlive || bMonsterForm || !Models || GetMesh()->GetSkeletalMeshAsset() != Models->CharacterMesh)
	{
		return;
	}
	if (!GestureRig.bValid)
	{
		GestureRig = HCGesture::Build(GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton());
	}
	const FTransform ActorToBody = GetActorTransform().GetRelativeTransform(GetMesh()->GetComponentTransform());
	HCGesture::Apply(GestureRig, Pose, ActiveEmote, GetWorld()->GetTimeSeconds() - EmoteStart, ActorToBody);
}

void AHCCharacter::UpdateExtras(float DeltaSeconds)
{
	if (ActiveEmote < EHCEmote::Count)
	{
		const float Time = GetWorld()->GetTimeSeconds() - EmoteStart;
		if (Time > HCGesture::Seconds || !bAlive || IsHidden())
		{
			ActiveEmote = EHCEmote::Count;
			EmoteTag->SetVisibility(false);
		}
		else if (const APlayerController* Viewer = GetNetMode() != NM_DedicatedServer ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			FVector ViewLocation;
			FRotator ViewRotation;
			Viewer->GetPlayerViewPoint(ViewLocation, ViewRotation);
			EmoteTag->SetWorldRotation(FRotator(0.f, (ViewLocation - EmoteTag->GetComponentLocation()).Rotation().Yaw, 0.f));
		}
	}
	// After a death, a change of form or of who controls the figure: the container where it belongs
	const bool bShowBox = bCarrying && bAlive && !bMonsterForm;
	if (CarriedBox->IsVisible() != bShowBox || CarriedBoxView->IsVisible() != (bShowBox && IsLocallyControlled()))
	{
		OnRep_Carrying();
	}
}

// ---------------------------------------------------------------------------
// Ability key and emote key

void AHCCharacter::Input_Ability()
{
	APlayerController* PC = GetController<APlayerController>();
	AHCPlayerController* HCPC = Cast<AHCPlayerController>(PC);
	const AHCPlayerState* PS = GetPlayerState<AHCPlayerState>();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr;
	if (!HCPC || !PS || !GS || !HUD || !bAlive)
	{
		return;
	}
	if (GS->GetMatchPhase() != EHCMatchPhase::Playing)
	{
		HUD->ShowCenterMessage(NSLOCTEXT("HC", "AbilityOnlyInRound", "Berufe gibt es nur in einer laufenden Runde."), 2.f);
		return;
	}
	if (!CanAct())
	{
		return;
	}
	const FHCRoleInfo& Info = PS->GetRoleInfo();
	if (Info.Role == EHCRole::Monster)
	{
		HUD->ShowCenterMessage(Info.Job != EHCJob::None
			? FText::Format(NSLOCTEXT("HC", "AbilityCoverFmt", "Deine Tarnung ({0}) kann nichts. Behaupte es einfach."), HCJobs::Name(Info.Job))
			: NSLOCTEXT("HC", "AbilityNone", "Du hast in dieser Runde keinen Beruf."), 2.5f);
		return;
	}
	switch (Info.Job)
	{
	case EHCJob::Medic:
	case EHCJob::Guard:
	case EHCJob::Biologist:
		HCPC->ServerUseAbility(static_cast<uint8>(EHCAbilityAction::Use), FString());
		break;
	case EHCJob::Radio:
		HUD->OpenRadio();
		break;
	case EHCJob::Technician:
		HUD->ShowCenterMessage(GS->AreVentsEnabled() ? NSLOCTEXT("HC", "AbilityTechnician", "Techniker: Stell dich an eine Luke im Boden und halte [E].")
			: NSLOCTEXT("HC", "AbilityTechnicianNoVents", "Techniker: In dieser Runde gibt es keine Schächte. Sicherungen reparierst du doppelt so schnell."), 2.5f);
		break;
	case EHCJob::Commander:
		HUD->ShowCenterMessage(NSLOCTEXT("HC", "AbilityCommander", "Kommandant: Deine Stimme zählt im Meeting doppelt."), 2.5f);
		break;
	default:
		HUD->ShowCenterMessage(NSLOCTEXT("HC", "AbilityNone", "Du hast in dieser Runde keinen Beruf."), 2.f);
		break;
	}
}

void AHCCharacter::Input_Emote()
{
	APlayerController* PC = GetController<APlayerController>();
	AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr;
	if (HUD && bAlive && CanAct() && !bInVent)
	{
		HUD->OpenEmoteWheel();
	}
}
