#include "UI/HCWardrobeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCOutfit.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/World.h"
#include "UI/HCHUD.h"
#include "UI/HCLook.h"
#include "UI/HCUIStyle.h"
#include "World/HCWardrobe.h"

namespace HCWardrobeLook
{
	constexpr double Width = 460.0;
	constexpr double Height = 624.0;
	constexpr double Margin = 30.0;
	/** The colour row. */
	constexpr double ColourTop = 128.0;
	constexpr double ColourSize = 40.0;
	/** The rows of the small things, one under the other. */
	constexpr double PartsTop = 204.0;
	constexpr double PartRow = 82.0;
	constexpr double PartSize = 36.0;
	constexpr double PartGap = 12.0;
	constexpr double ButtonTop = Height - 76.0;
	constexpr double ButtonHeight = 42.0;

	double ColourGap()
	{
		return (Width - 2.0 * Margin - HCIdentity::Count * ColourSize) / (HCIdentity::Count - 1);
	}

	FVector2D ColourAt(int32 Index)
	{
		return FVector2D(Margin + Index * (ColourSize + ColourGap()), ColourTop);
	}

	FVector2D PartAt(int32 Part, int32 Choice)
	{
		return FVector2D(Margin + Choice * (PartSize + PartGap), PartsTop + Part * PartRow + 26.0);
	}

	bool Inside(const FVector2D& Point, const FVector2D& Pos, double Size)
	{
		return Point.X >= Pos.X && Point.Y >= Pos.Y && Point.X < Pos.X + Size && Point.Y < Pos.Y + Size;
	}
}

void UHCWardrobeWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	// No dark veil: the figure in the picture is the point of the window
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.f));
	const TWeakObjectPtr<UHCWardrobeWidget> Weak(this);
	UHCPaintBox* Panel = HCLook::Control(Tree, FVector2D(HCWardrobeLook::Width, HCWardrobeLook::Height),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCWardrobeWidget* Widget = Weak.Get())
			{
				Widget->PaintPanel(D, S);
			}
		},
		[Weak](const FVector2D& Local, const FVector2D&)
		{
			UHCWardrobeWidget* Widget = Weak.Get();
			if (!Widget)
			{
				return;
			}
			const FHit Hit = Widget->HitAt(Local);
			if (Hit.Part == 4)
			{
				Widget->Close();
			}
			else if (Hit.Part >= -1)
			{
				Widget->Pick(Hit.Part, Hit.Choice);
			}
		});
	Panel->WantsHand = [Weak]()
	{
		return Weak.IsValid();
	};
	USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(HCWardrobeLook::Width);
	Size->SetHeightOverride(HCWardrobeLook::Height);
	Size->SetContent(Panel);
	HCUI::Place(Root, Size, FVector2D(0.96f, 0.5f), FVector2D(1.f, 0.5f), HCWardrobeLook::Width);
}

void UHCWardrobeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	OpenedAt = FPlatformTime::Seconds();
	LastPick = -100.0;
	if (const AHCPlayerState* PS = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<AHCPlayerState>() : nullptr)
	{
		Identity = PS->GetIdentity();
		Outfit = PS->GetOutfit();
	}
	StartMirror();
}

void UHCWardrobeWidget::NativeDestruct()
{
	StopMirror();
	Super::NativeDestruct();
}

void UHCWardrobeWidget::StartMirror()
{
	APlayerController* PC = GetOwningPlayer();
	AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	const AHCWardrobe* Box = Wardrobe.Get();
	if (!PC || !Character || !Box || MirrorCamera)
	{
		return;
	}
	// Turn round, away from the wardrobe, and look at oneself through a camera in front
	FVector Away = Character->GetActorLocation() - Box->GetActorLocation();
	Away.Z = 0.0;
	Away = Away.GetSafeNormal(UE_SMALL_NUMBER, Box->GetActorForwardVector());
	PC->SetControlRotation(FRotator(0.f, Away.Rotation().Yaw, 0.f));
	float FieldOfView = 60.f;
	const FTransform View = Box->GetMirrorCamera(Character, FieldOfView);
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	MirrorCamera = GetWorld()->SpawnActor<ACameraActor>(View.GetLocation(), View.Rotator(), Params);
	if (MirrorCamera)
	{
		MirrorCamera->GetCameraComponent()->SetFieldOfView(FieldOfView);
		MirrorCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
		PC->SetViewTargetWithBlend(MirrorCamera, 0.45f, VTBlend_EaseInOut, 2.f);
	}
	Character->SetMirrorView(true);
}

void UHCWardrobeWidget::StopMirror()
{
	APlayerController* PC = GetOwningPlayer();
	AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	if (Character)
	{
		Character->SetMirrorView(false);
	}
	if (PC && MirrorCamera && PC->GetViewTarget() == MirrorCamera)
	{
		PC->SetViewTarget(PC->GetPawn() ? static_cast<AActor*>(PC->GetPawn()) : static_cast<AActor*>(PC));
	}
	if (MirrorCamera)
	{
		MirrorCamera->Destroy();
		MirrorCamera = nullptr;
	}
}

void UHCWardrobeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// After a moment the server's word counts again (a colour somebody else took in the same instant stays theirs)
	const AHCPlayerState* PS = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<AHCPlayerState>() : nullptr;
	if (PS && FPlatformTime::Seconds() - LastPick > 1.0)
	{
		Identity = PS->GetIdentity();
		Outfit = PS->GetOutfit();
	}
}

bool UHCWardrobeWidget::IsTaken(int32 InIdentity) const
{
	const APlayerController* PC = GetOwningPlayer();
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const APlayerState* Own = PC ? PC->PlayerState.Get() : nullptr;
	if (!GS)
	{
		return false;
	}
	for (const APlayerState* Other : GS->PlayerArray)
	{
		const AHCPlayerState* HCOther = Cast<AHCPlayerState>(Other);
		if (HCOther && Other != Own && HCOther->GetIdentity() == InIdentity)
		{
			return true;
		}
	}
	return false;
}

void UHCWardrobeWidget::Pick(int32 Part, int32 Choice)
{
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!PC)
	{
		return;
	}
	if (Part == -1)
	{
		if (!HCIdentity::IsValid(Choice) || IsTaken(Choice))
		{
			return;
		}
		Identity = Choice;
	}
	else if (Part >= 0 && Part < static_cast<int32>(HCOutfit::EPart::Count))
	{
		const HCOutfit::EPart Which = static_cast<HCOutfit::EPart>(Part);
		if (Choice < 0 || Choice >= HCOutfit::Choices(Which))
		{
			return;
		}
		Outfit = HCOutfit::With(Outfit, Which, Choice);
	}
	else
	{
		return;
	}
	LastPick = FPlatformTime::Seconds();
	PC->ServerSetLook(Identity, Outfit);
	AHCPlayerController::SaveLook(this, Identity, Outfit);
}

void UHCWardrobeWidget::Close()
{
	if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->CloseModal(this);
	}
}

bool UHCWardrobeWidget::ShouldClose() const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	const AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	return !Wardrobe.IsValid() || !GS || GS->GetMatchPhase() != EHCMatchPhase::Lobby || !Character || !Character->IsAlive();
}

UHCWardrobeWidget::FHit UHCWardrobeWidget::HitAt(const FVector2D& Local) const
{
	using namespace HCWardrobeLook;
	FHit Hit;
	for (int32 Index = 0; Index < HCIdentity::Count; ++Index)
	{
		if (Inside(Local, ColourAt(Index), ColourSize))
		{
			Hit.Part = -1;
			Hit.Choice = Index;
			return Hit;
		}
	}
	for (int32 Part = 0; Part < static_cast<int32>(HCOutfit::EPart::Count); ++Part)
	{
		for (int32 Choice = 0; Choice < HCOutfit::Choices(static_cast<HCOutfit::EPart>(Part)); ++Choice)
		{
			if (Inside(Local, PartAt(Part, Choice), PartSize))
			{
				Hit.Part = Part;
				Hit.Choice = Choice;
				return Hit;
			}
		}
	}
	if (Local.X >= Margin && Local.X < Width - Margin && Local.Y >= ButtonTop && Local.Y < ButtonTop + ButtonHeight)
	{
		Hit.Part = 4;
	}
	return Hit;
}

void UHCWardrobeWidget::PaintPanel(const FHCDraw& D, const FHCPaintState& S) const
{
	using namespace HCWardrobeLook;
	const float Appear = HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - OpenedAt), 0.f, 0.2f);
	HCLook::DrawPlate(D, HCLook::Alpha(HCLook::Bone, 0.4f * Appear), 0.9f * Appear);
	const FHit Hot = S.bHovered ? HitAt(S.Mouse) : FHit();

	D.Text(HCLook::Upper(NSLOCTEXT("HC", "WardrobeTitle", "Kleiderschrank")), FVector2D(Margin, 28.0), HCLook::Caps(22.f, 220), HCLook::Bone);
	D.Text(FHCDraw::Fit(NSLOCTEXT("HC", "WardrobeSub", "Jede Farbe trägt nur einer. Der Rest ist Geschmack.").ToString(), HCLook::Body(13.f), Width - 2.0 * Margin),
		FVector2D(Margin, 64.0), HCLook::Body(13.f), HCLook::BoneMuted);

	// Colour of the jacket: the eight identities with their symbols, taken ones crossed out
	const int32 ShownIdentity = Hot.Part == -1 ? Hot.Choice : Identity;
	const FText ColourName = HCIdentity::IsValid(ShownIdentity) ? HCIdentity::ColorName(ShownIdentity) : FText::GetEmpty();
	D.Text(HCLook::Upper(FText::Format(NSLOCTEXT("HC", "WardrobeColourFmt", "JACKE  ·  {0}"), ColourName)), FVector2D(Margin, ColourTop - 26.0), HCLook::Caps(12.f, 200), HCLook::Bone);
	for (int32 Index = 0; Index < HCIdentity::Count; ++Index)
	{
		const FVector2D At = ColourAt(Index);
		const FVector2D Size(ColourSize, ColourSize);
		const bool bTaken = IsTaken(Index);
		const bool bOwn = Index == Identity;
		D.Box(At, Size, HCLook::Alpha(HCIdentity::Color(Index), bTaken ? 0.3f : 1.f));
		D.Symbol(Index, At + Size * 0.5, 9.f, HCLook::Alpha(HCLook::Ink, bTaken ? 0.4f : 0.8f), 2.f);
		if (bTaken)
		{
			D.Lines({ At + FVector2D(4.0, 4.0), At + Size - FVector2D(4.0, 4.0) }, HCLook::Alpha(HCLook::Bone, 0.7f), 2.f);
		}
		if (bOwn)
		{
			D.Frame(At - FVector2D(4.0, 4.0), Size + FVector2D(8.0, 8.0), 2.f, HCLook::Bone);
		}
		else if (Hot.Part == -1 && Hot.Choice == Index && !bTaken)
		{
			D.Frame(At - FVector2D(2.0, 2.0), Size + FVector2D(4.0, 4.0), 1.f, HCLook::Alpha(HCLook::Bone, 0.7f));
		}
	}

	// The small things: one row each, with the name of the chosen (or the pointed at) choice
	for (int32 Part = 0; Part < static_cast<int32>(HCOutfit::EPart::Count); ++Part)
	{
		const HCOutfit::EPart Which = static_cast<HCOutfit::EPart>(Part);
		const int32 Chosen = HCOutfit::Get(Outfit, Which);
		const int32 Shown = Hot.Part == Part ? Hot.Choice : Chosen;
		const double Top = PartsTop + Part * PartRow;
		D.Text(HCLook::Upper(FText::Format(NSLOCTEXT("HC", "WardrobePartFmt", "{0}  ·  {1}"), HCOutfit::PartName(Which), HCOutfit::ChoiceName(Which, Shown))),
			FVector2D(Margin, Top), HCLook::Caps(12.f, 200), HCLook::Bone);
		for (int32 Choice = 0; Choice < HCOutfit::Choices(Which); ++Choice)
		{
			const FVector2D At = PartAt(Part, Choice);
			const FVector2D Size(PartSize, PartSize);
			D.Box(At, Size, HCOutfit::Swatch(Which, Choice));
			D.Frame(At, Size, 1.f, HCLook::Alpha(HCLook::BoneMuted, 0.5f));
			if (Which == HCOutfit::EPart::Backpack && Choice == 1)
			{
				// "Without": an empty field with a stroke through it
				D.Lines({ At + FVector2D(6.0, PartSize - 6.0), At + FVector2D(PartSize - 6.0, 6.0) }, HCLook::Alpha(HCLook::BoneMuted, 0.8f), 1.5f);
			}
			if (Choice == Chosen)
			{
				D.Frame(At - FVector2D(4.0, 4.0), Size + FVector2D(8.0, 8.0), 2.f, HCLook::Bone);
			}
			else if (Hot.Part == Part && Hot.Choice == Choice)
			{
				D.Frame(At - FVector2D(2.0, 2.0), Size + FVector2D(4.0, 4.0), 1.f, HCLook::Alpha(HCLook::Bone, 0.7f));
			}
		}
	}

	// Done (Esc does the same)
	const bool bHotButton = Hot.Part == 4;
	const FVector2D ButtonAt(Margin, ButtonTop);
	const FVector2D ButtonSize(Width - 2.0 * Margin, ButtonHeight);
	D.Box(ButtonAt, ButtonSize, HCLook::Alpha(bHotButton ? HCLook::Bone : HCLook::Ink, bHotButton ? (S.bPressed ? 0.8f : 1.f) : 0.55f));
	D.Frame(ButtonAt, ButtonSize, 1.f, HCLook::Alpha(HCLook::Bone, 0.85f));
	D.Box(ButtonAt + FVector2D(0.0, ButtonHeight - 2.0), FVector2D(ButtonSize.X, 2.0), HCLook::Alpha(HCLook::Bone, 0.85f));
	D.TextCentered(HCLook::Upper(NSLOCTEXT("HC", "WardrobeDone", "Fertig")), ButtonAt.X + ButtonSize.X * 0.5, ButtonAt.Y + 11.0, HCLook::Caps(14.f, 220),
		bHotButton ? HCLook::Ink : HCLook::Bone);
	D.TextCentered(NSLOCTEXT("HC", "WardrobeRemembered", "Wird für das nächste Spiel gemerkt").ToString(), Width * 0.5, ButtonTop + ButtonHeight + 10.0, HCLook::Mono(11.f, 80),
		HCLook::BoneMuted);
}

FReply UHCWardrobeWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F1 || InKeyEvent.GetKey() == EKeys::Enter)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
