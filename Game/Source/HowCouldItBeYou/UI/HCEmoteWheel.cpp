#include "UI/HCEmoteWheel.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/HCCharacter.h"
#include "Core/HCKeyBindings.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "UI/HCHUD.h"
#include "UI/HCLook.h"

namespace HCWheelLook
{
	/** Radius of the ring of slots and of the dead middle, in Slate units. */
	constexpr double Ring = 220.0;
	constexpr double Middle = 70.0;
	constexpr double SlotRadius = 64.0;
}

void UHCEmoteWheelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	const TWeakObjectPtr<UHCEmoteWheelWidget> Weak(this);
	UHCPaintBox* View = HCLook::Control(WidgetTree, FVector2D::ZeroVector,
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCEmoteWheelWidget* Widget = Weak.Get())
			{
				Widget->PaintWheel(D, S);
			}
		},
		[](const FVector2D&, const FVector2D&) {});
	View->OnReleased = [Weak](bool)
	{
		if (UHCEmoteWheelWidget* Widget = Weak.Get())
		{
			Widget->Choose();
		}
	};
	View->WantsHand = []() { return false; };
	WidgetTree->RootWidget = View;
}

void UHCEmoteWheelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	OpenedAt = FPlatformTime::Seconds();
	bChosen = false;
	Hot = -1;
	// The mouse starts in the middle of the wheel
	if (APlayerController* PC = GetOwningPlayer())
	{
		int32 Width = 0, Height = 0;
		PC->GetViewportSize(Width, Height);
		PC->SetMouseLocation(Width / 2, Height / 2);
	}
}

int32 UHCEmoteWheelWidget::SlotAt(const FVector2D& Local, const FVector2D& Size) const
{
	const FVector2D FromMiddle = Local - Size * 0.5;
	if (FromMiddle.Size() < HCWheelLook::Middle)
	{
		return -1;
	}
	// Slot 0 at the top, clockwise
	const double Angle = FMath::Atan2(FromMiddle.X, -FromMiddle.Y);
	const int32 Count = static_cast<int32>(EHCEmote::Count);
	return (FMath::RoundToInt32(Angle / (2.0 * UE_DOUBLE_PI) * Count) + Count) % Count;
}

void UHCEmoteWheelWidget::PaintWheel(const FHCDraw& D, const FHCPaintState& S) const
{
	using namespace HCWheelLook;
	const FVector2D Centre = D.Size * 0.5;
	const float Appear = HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - OpenedAt), 0.f, 0.15f);
	Hot = S.bHovered ? SlotAt(S.Mouse, D.Size) : -1;
	D.Box(FVector2D::ZeroVector, D.Size, HCLook::Alpha(HCLook::Ink, 0.35f * Appear));
	const int32 Count = static_cast<int32>(EHCEmote::Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * Index / Count;
		const FVector2D At = Centre + FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle)) * Ring * (0.85 + 0.15 * Appear);
		const bool bHot = Index == Hot;
		const FVector2D Half(SlotRadius, SlotRadius * 0.62);
		D.Box(At - Half, Half * 2.0, HCLook::Alpha(bHot ? HCLook::Bone : HCLook::Plate, (bHot ? 0.95f : 0.85f) * Appear));
		D.Frame(At - Half, Half * 2.0, 1.f, HCLook::Alpha(bHot ? HCLook::Bone : HCLook::BoneMuted, 0.7f * Appear));
		const FString Label = HCLook::Upper(HCGesture::Name(static_cast<EHCEmote>(Index)));
		D.TextCentered(Label, At.X, At.Y - 9.0, HCLook::Caps(13.f, 160), HCLook::Alpha(bHot ? HCLook::Ink : HCLook::Bone, Appear));
	}
	// The middle: what letting go does
	const FString Hint = Hot >= 0 ? HCLook::Upper(HCGesture::Name(static_cast<EHCEmote>(Hot))) : HCLook::Upper(NSLOCTEXT("HC", "EmoteNone", "Nichts"));
	D.TextCentered(Hint, Centre.X, Centre.Y - 10.0, HCLook::Caps(15.f, 200), HCLook::Alpha(Hot >= 0 ? HCLook::Bone : HCLook::BoneMuted, Appear));
	D.TextCentered(FText::Format(NSLOCTEXT("HC", "EmoteWheelHintFmt", "[{0}] loslassen oder klicken"), HCKeys::ShortName(EHCBinding::Emote)).ToString(),
		Centre.X, Centre.Y + Ring + 90.0, HCLook::Mono(12.f, 120), HCLook::Alpha(HCLook::BoneMuted, Appear));
}

void UHCEmoteWheelWidget::Choose()
{
	if (bChosen)
	{
		return;
	}
	bChosen = true;
	const int32 Picked = Hot;
	Close();
	if (Picked >= 0)
	{
		ChooseEmote(static_cast<EHCEmote>(Picked));
	}
}

void UHCEmoteWheelWidget::ChooseEmote(EHCEmote Emote)
{
	bChosen = true;
	APlayerController* PC = GetOwningPlayer();
	if (AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr)
	{
		Character->RequestEmote(Emote);
	}
	Close();
}

void UHCEmoteWheelWidget::Close()
{
	if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->CloseModal(this);
	}
}

FReply UHCEmoteWheelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F1)
	{
		bChosen = true;
		Close();
		return FReply::Handled();
	}
	// Opened with a short tap: the next press of the key takes what the mouse points at
	if (HCKeys::Is(EHCBinding::Emote, InKeyEvent.GetKey()) && !InKeyEvent.IsRepeat() && FPlatformTime::Seconds() - OpenedAt > 0.3)
	{
		Choose();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UHCEmoteWheelWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (HCKeys::Is(EHCBinding::Emote, InKeyEvent.GetKey()))
	{
		// Held and let go: the emote under the mouse. A short tap leaves the wheel open for a click.
		if (FPlatformTime::Seconds() - OpenedAt > 0.3)
		{
			Choose();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}
