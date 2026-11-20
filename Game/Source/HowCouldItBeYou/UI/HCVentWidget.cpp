#include "UI/HCVentWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/HCCharacter.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCPlayerState.h"
#include "Core/HCText.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/HCInteractionComponent.h"
#include "UI/HCLook.h"
#include "World/HCVent.h"

namespace HCVentLook
{
	constexpr double Margin = 64.0;
	/** The slot the Wandler looks through, as fractions of the screen height: louvres above, the frame below. */
	constexpr double SlotTop = 0.30, SlotBottom = 0.80;
	/** Degrees per second with the keys, degrees per screen unit with the mouse. */
	constexpr float TurnSpeed = 120.f, DragSpeed = 0.14f;
	/** Dark for this long after climbing in (the cut of the camera). */
	constexpr double SettleDark = 0.45;
}

double UHCVentViewWidget::DarkUntil = 0.0;

AHCVent* UHCVentViewWidget::GetVent() const
{
	return Cast<AHCVent>(Device.Get());
}

bool UHCVentViewWidget::IsCrawling() const
{
	return FPlatformTime::Seconds() < DarkUntil;
}

int32 UHCVentViewWidget::GetLinkCount() const
{
	TArray<AHCVent*> Links;
	if (const AHCVent* Vent = GetVent())
	{
		Vent->GetLinkedVents(Links);
	}
	return Links.Num();
}

void UHCVentViewWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Holding the mouse button and dragging turns the head (the cursor is free while a view is open)
	if (UHCPaintBox* View = Cast<UHCPaintBox>(WidgetTree->RootWidget))
	{
		TWeakObjectPtr<UHCVentViewWidget> Weak(this);
		View->OnDragged = [Weak](const FVector2D& Local, const FVector2D& Size)
		{
			UHCVentViewWidget* Widget = Weak.Get();
			AHCVent* Vent = Widget ? Widget->GetVent() : nullptr;
			if (Vent && Widget->bDragging && !Widget->IsCrawling())
			{
				Vent->AddViewYaw(static_cast<float>(Local.X - Widget->LastDrag.X) * HCVentLook::DragSpeed);
			}
			if (Widget)
			{
				Widget->LastDrag = Local;
				Widget->bDragging = true;
			}
		};
		View->OnReleased = [Weak](bool)
		{
			if (UHCVentViewWidget* Widget = Weak.Get())
			{
				Widget->bDragging = false;
			}
		};
	}
}

void UHCVentViewWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// A new view after a crawl keeps the darkness of the way; a fresh one covers the cut into the shaft
	DarkUntil = FMath::Max(DarkUntil, FPlatformTime::Seconds() + HCVentLook::SettleDark);
}

void UHCVentViewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	AHCVent* Vent = GetVent();
	if (Vent && bTurnLeft != bTurnRight && !IsCrawling())
	{
		Vent->AddViewYaw((bTurnRight ? 1.f : -1.f) * HCVentLook::TurnSpeed * InDeltaTime);
	}
}

bool UHCVentViewWidget::RequestCrawl(int32 LinkIndex)
{
	AHCVent* Vent = GetVent();
	const APlayerController* PC = GetOwningPlayer();
	AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	TArray<AHCVent*> Links;
	if (Vent)
	{
		Vent->GetLinkedVents(Links);
	}
	if (!Character || !Links.IsValidIndex(LinkIndex) || IsCrawling())
	{
		return false;
	}
	// The server moves the hidden figure and sends the view of the other hatch; until then it stays dark
	Character->GetInteraction()->RequestInteract(Links[LinkIndex]);
	// The own job is known on this machine: the technician crawls slower
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	const bool bTechnician = PS && PS->GetRoleInfo().Role == EHCRole::Crew && PS->GetRoleInfo().Job == EHCJob::Technician;
	DarkUntil = FPlatformTime::Seconds() + AHCVent::CrawlSeconds(Vent, Links[LinkIndex], bTechnician);
	return true;
}

FReply UHCVentViewWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Left || HCKeys::Is(EHCBinding::Left, Key))
	{
		bTurnLeft = true;
		return FReply::Handled();
	}
	if (Key == EKeys::Right || HCKeys::Is(EHCBinding::Right, Key))
	{
		bTurnRight = true;
		return FReply::Handled();
	}
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Digits); ++Index)
	{
		if (Key == Digits[Index])
		{
			RequestCrawl(Index);
			return FReply::Handled();
		}
	}
	if (IsCrawling() && !InKeyEvent.IsRepeat() && (Key == EKeys::Escape || Key == EKeys::F1 || HCKeys::Is(EHCBinding::Interact, Key)))
	{
		return FReply::Handled(); // nobody climbs out between two hatches
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UHCVentViewWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Left || HCKeys::Is(EHCBinding::Left, Key))
	{
		bTurnLeft = false;
	}
	if (Key == EKeys::Right || HCKeys::Is(EHCBinding::Right, Key))
	{
		bTurnRight = false;
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

void UHCVentViewWidget::OnViewPressed(const FVector2D& Local, const FVector2D& Size)
{
	bDragging = false;
	for (int32 Index = 0; Index < LinkBoxes.Num(); ++Index)
	{
		if (LinkBoxes[Index].IsInside(Local))
		{
			RequestCrawl(Index);
			return;
		}
	}
}

void UHCVentViewWidget::PaintView(const FHCDraw& D, const FHCPaintState& S) const
{
	using namespace HCVentLook;
	const AHCVent* Vent = GetVent();
	const double W = D.Size.X, H = D.Size.Y;
	const FLinearColor Dark(0.f, 0.f, 0.f, 1.f);
	const FLinearColor Clear(0.f, 0.f, 0.f, 0.f);
	LinkBoxes.Reset();

	const double DarkLeft = DarkUntil - FPlatformTime::Seconds();
	if (DarkLeft > 0.0 || !Vent)
	{
		// On the way: nothing to see, only the sheet metal under hands and knees
		D.Box(FVector2D::ZeroVector, D.Size, Dark);
		if (DarkLeft > SettleDark)
		{
			const int32 Dots = 1 + FMath::FloorToInt32(S.Time * 3.0) % 3;
			D.TextCentered(HCLook::Upper(NSLOCTEXT("HC", "VentCrawling", "Du kriechst durch den Schacht")) + FString::ChrN(Dots, TEXT('.')), W * 0.5, H * 0.5 - 12.0,
				HCLook::Caps(18.f, 260), HCLook::Alpha(HCLook::BoneMuted, 0.8f));
		}
		return;
	}

	// Above the slot: the underside of the lifted grille, four louvres with a line of light between them
	const double SlatH = H * 0.064, GapH = H * 0.011;
	double Y = 0.0;
	for (int32 Slat = 0; Slat < 4; ++Slat)
	{
		D.Box(FVector2D(0.0, Y), FVector2D(W, SlatH), Dark);
		D.Box(FVector2D(0.0, Y + SlatH), FVector2D(W, GapH), FLinearColor(0.f, 0.f, 0.f, 0.72f));
		Y += SlatH + GapH;
	}
	D.Gradient(FVector2D(0.0, Y), FVector2D(W, FMath::Max(1.0, H * SlotTop + H * 0.07 - Y)), Dark, Clear, false);
	// Below: the frame of the hatch
	D.Gradient(FVector2D(0.0, H * SlotBottom - H * 0.05), FVector2D(W, H * 0.05), Clear, Dark, false);
	D.Box(FVector2D(0.0, H * SlotBottom), FVector2D(W, H * (1.0 - SlotBottom)), Dark);
	// The corners of the eyes
	D.Gradient(FVector2D::ZeroVector, FVector2D(W * 0.16, H), Dark, Clear, true);
	D.Gradient(FVector2D(W * 0.84, 0.0), FVector2D(W * 0.16, H), Clear, Dark, true);

	// What the Wandler can do from here, on the dark frame
	const double TextY = H * SlotBottom + 26.0;
	const FString Room = HCLook::Upper(HCText::Room(Vent->GetRoomId()));
	const FVector2D TitleSize = D.Text(HCLook::Upper(NSLOCTEXT("HC", "VentTitle", "Lüftungsschacht")), FVector2D(Margin, TextY), HCLook::Caps(22.f, 240), HCLook::Bone);
	D.Text(Room, FVector2D(Margin + TitleSize.X + 22.0, TextY + 5.0), HCLook::Caps(15.f, 200), HCLook::Blood);

	TArray<AHCVent*> Links;
	Vent->GetLinkedVents(Links);
	double X = Margin;
	// The row sits below the line of the voice chat ("TALK hold [V]"), which stays where it always is
	const double RowY = TextY + 86.0;
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		const int32 Meters = FMath::RoundToInt32(FVector::Dist(Links[Index]->GetActorLocation(), Vent->GetActorLocation()) / 100.0);
		const FString Label = FText::Format(NSLOCTEXT("HC", "VentLinkFmt", "{0}  ·  {1} m"), HCText::Room(Links[Index]->GetRoomId()), FText::AsNumber(Meters)).ToUpper().ToString();
		const FSlateFontInfo Font = HCLook::Caps(14.f, 180);
		const FVector2D LabelSize = D.MeasureExact(Label, Font);
		const FVector2D BoxSize(34.0 + 14.0 + LabelSize.X + 18.0, 38.0);
		const FBox2D Box(FVector2D(X, RowY), FVector2D(X, RowY) + BoxSize);
		const bool bHover = S.bHovered && Box.IsInside(S.Mouse);
		D.Box(Box.Min, BoxSize, HCLook::Alpha(HCLook::Bone, bHover ? 0.16f : 0.06f));
		D.Frame(Box.Min, BoxSize, 1.f, HCLook::Alpha(HCLook::Bone, bHover ? 0.9f : 0.35f));
		// The number key on a solid plate, like the interact prompt
		D.Box(Box.Min, FVector2D(34.0, BoxSize.Y), HCLook::Bone);
		D.TextCentered(FString::FromInt(Index + 1), X + 17.0, RowY + 8.0, HCLook::Mono(16.f), HCLook::Ink);
		D.Text(Label, FVector2D(X + 34.0 + 14.0, RowY + 10.0), Font, HCLook::Bone);
		LinkBoxes.Add(Box);
		X += BoxSize.X + 16.0;
	}
	if (Links.IsEmpty())
	{
		D.Text(HCLook::Upper(NSLOCTEXT("HC", "VentNoLinks", "Dieser Schacht führt nirgendwohin")), FVector2D(Margin, RowY + 10.0), HCLook::Caps(14.f, 180), HCLook::BoneMuted);
	}
	const FString Hint = FText::Format(NSLOCTEXT("HC", "VentHintFmt", "[{0}] [{1}] oder Maus ziehen: umsehen     [{2}] aussteigen"),
		HCKeys::ShortName(EHCBinding::Left), HCKeys::ShortName(EHCBinding::Right), HCKeys::ShortName(EHCBinding::Interact)).ToString();
	D.Text(Hint, FVector2D(Margin, H - 38.0), HCLook::Mono(12.f, 160), HCLook::BoneMuted);
}
