#include "UI/HCShapeshiftWidget.h"

#include "Core/HCKeyBindings.h"
#include "Core/HCTypes.h"
#include "Abilities/HCShapeshiftComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/HCCharacter.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UI/HCHUD.h"
#include "UI/HCLook.h"
#include "UI/HCUIStyle.h"

namespace HCShapeMenu
{
	constexpr float PlateWidth = 560.f;
	constexpr float PlateInset = 24.f;
	constexpr float RowHeight = 58.f;
	constexpr float RowGap = 8.f;
	/** Each row fades in a little after the one above it. */
	constexpr float RowStagger = 0.05f;

	/** The room behind the menu: dark, a blood glow from above, the station's corner marks. */
	void PaintBackdrop(const FHCDraw& D, double Opened)
	{
		FHCDraw F = D;
		F.Opacity *= HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - Opened), 0.f, 0.3f);
		const FVector2D S = F.Size;

		F.Box(FVector2D::ZeroVector, S, HCLook::Alpha(HCLook::Ink, 0.74f));
		F.Gradient(FVector2D::ZeroVector, FVector2D(S.X, S.Y * 0.42), HCLook::Alpha(HCLook::Blood, 0.12f), HCLook::Alpha(HCLook::Blood, 0.f), false);
		F.Gradient(FVector2D(0.0, S.Y * 0.68), FVector2D(S.X, S.Y * 0.32), HCLook::Alpha(HCLook::Ink, 0.f), HCLook::Alpha(HCLook::Ink, 0.7f), false);
		F.Brackets(FVector2D(36.0, 36.0), S - FVector2D(72.0, 72.0), 30.f, 2.f, HCLook::Alpha(HCLook::Bone, 0.3f));
		F.Box(FVector2D(66.0, 69.0), FVector2D(9.0, 9.0), HCLook::Blood);
		F.Text(HCLook::Upper(NSLOCTEXT("HC", "ShiftStation", "Wandler  ·  Gestalt")), FVector2D(86.0, 63.0), HCLook::Mono(12.f, 260), HCLook::BoneMuted);
		F.ScanLines(0.12f);
	}

	/** The plate behind the list: hairline frame, blood corner marks. */
	void PaintPlate(const FHCDraw& D)
	{
		HCLook::DrawPlate(D, HCLook::Alpha(HCLook::BoneMuted, 0.28f), 0.94f);
		D.Brackets(FVector2D::ZeroVector, D.Size, 14.f, 2.f, HCLook::Alpha(HCLook::Blood, 0.7f));
	}

	/** One living player: number key, suit symbol, name. Blood when the mouse is on it. */
	void PaintRow(const FHCDraw& D, const FHCPaintState& S, int32 Number, int32 Identity, const FString& Name, int32 Index, double Opened)
	{
		FHCDraw F = D;
		F.Opacity *= HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - Opened), 0.1f + RowStagger * Index, 0.3f);
		const FVector2D Size = F.Size;
		const bool bHot = S.bHovered || S.bPressed;
		const FLinearColor Suit = HCIdentity::Color(Identity);

		F.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Ink, 0.6f));
		if (bHot)
		{
			F.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Blood, S.bPressed ? 0.22f : 0.12f));
		}
		F.Frame(FVector2D::ZeroVector, Size, 1.f, bHot ? HCLook::Blood : HCLook::Alpha(HCLook::BoneMuted, 0.35f));
		F.Box(FVector2D::ZeroVector, FVector2D(4.0, Size.Y), HCLook::Alpha(Suit, bHot ? 1.f : 0.7f));

		// The number key
		const FSlateFontInfo NumberFont = HCLook::Mono(14.f, 100);
		const FString NumberText = FString::FromInt(Number);
		const FVector2D NumberSize = FHCDraw::Measure(NumberText, NumberFont);
		F.Text(NumberText, FVector2D(18.0, (Size.Y - NumberSize.Y) * 0.5), NumberFont, bHot ? HCLook::Blood : HCLook::BoneMuted);

		// The suit symbol on a dark tile
		const double TileX = 52.0;
		const FVector2D Tile(TileX, (Size.Y - 40.0) * 0.5);
		F.Box(Tile, FVector2D(40.0, 40.0), HCLook::Alpha(HCLook::Ink, 0.8f));
		F.Frame(Tile, FVector2D(40.0, 40.0), 1.f, HCLook::Alpha(Suit, 0.55f));
		F.Symbol(Identity, Tile + FVector2D(20.0, 20.0), 12.f, Suit, 2.f);

		// The name
		const FSlateFontInfo NameFont = HCLook::Body(18.f, true);
		const double TextX = TileX + 40.0 + 16.0;
		const FString Shown = FHCDraw::Fit(Name, NameFont, Size.X - TextX - 130.0);
		const FVector2D NameSize = FHCDraw::Measure(Shown, NameFont);
		F.Text(Shown, FVector2D(TextX, (Size.Y - NameSize.Y) * 0.5), NameFont, bHot ? HCLook::Bone : HCLook::Alpha(HCLook::Bone, 0.9f));

		// On the right, what a click does
		if (bHot)
		{
			const FSlateFontInfo ActionFont = HCLook::Caps(11.f, 220);
			const FString Action = HCLook::Upper(NSLOCTEXT("HC", "ShiftPick", "verwandeln"));
			const FVector2D ActionSize = FHCDraw::Measure(Action, ActionFont);
			F.TextRight(Action, Size.X - 18.0, (Size.Y - ActionSize.Y) * 0.5, ActionFont, HCLook::Blood);
		}
	}
}

void UHCShapeshiftWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	OpenedAt = FPlatformTime::Seconds();

	UWidgetTree* Tree = WidgetTree;
	const TWeakObjectPtr<UHCShapeshiftWidget> Weak(this);
	const double Opened = OpenedAt;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor::Transparent);

	// The backdrop covers the whole screen
	UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(HCLook::Paint(Tree, FVector2D::ZeroVector, [Opened](const FHCDraw& D, const FHCPaintState&)
	{
		HCShapeMenu::PaintBackdrop(D, Opened);
	}));
	BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
	BackdropSlot->SetOffsets(FMargin(0.f));

	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = HCLook::Label(Tree, NSLOCTEXT("HC", "ShiftTitle", "IN WEN VERWANDELN?"), 26.f, HCLook::Blood, 240);
	Title->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Title, 10.f);
	HCUI::AddRow(Box, HCLook::Rule(Tree, HCLook::Alpha(HCLook::Bone, 0.16f)), 14.f);

	PlayerList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, PlayerList, 14.f);

	UTextBlock* Hint = HCLook::Text(Tree, FText::Format(NSLOCTEXT("HC", "ShiftHintFmt", "Klick oder Zifferntaste wählt · {0} oder Esc bricht ab"), HCKeys::ShortName(EHCBinding::Shapeshift)), 13.f, HCLook::BoneMuted);
	Hint->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Hint, 14.f);

	UHCPaintBox* Cancel = HCLook::Button(Tree, NSLOCTEXT("HC", "ShiftCancel", "Abbrechen"), HCLook::BoneMuted, [Weak]()
	{
		if (UHCShapeshiftWidget* Widget = Weak.Get())
		{
			Widget->Close();
		}
	}, FVector2D(HCShapeMenu::PlateWidth - 2.f * HCShapeMenu::PlateInset, 44.f), 12.f);
	HCUI::AddRow(Box, Cancel, 0.f);

	// The plate and the list on it
	UOverlay* Panel = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	UOverlaySlot* BackSlot = Panel->AddChildToOverlay(HCLook::Paint(Tree, FVector2D::ZeroVector, [](const FHCDraw& D, const FHCPaintState&)
	{
		HCShapeMenu::PaintPlate(D);
	}));
	BackSlot->SetHorizontalAlignment(HAlign_Fill);
	BackSlot->SetVerticalAlignment(VAlign_Fill);
	UOverlaySlot* ContentSlot = Panel->AddChildToOverlay(Box);
	ContentSlot->SetHorizontalAlignment(HAlign_Fill);
	ContentSlot->SetVerticalAlignment(VAlign_Fill);
	ContentSlot->SetPadding(FMargin(HCShapeMenu::PlateInset, 22.f));

	HCUI::Place(Root, Panel, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), HCShapeMenu::PlateWidth);
}

void UHCShapeshiftWidget::Close()
{
	const APlayerController* PC = GetOwningPlayer();
	if (AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->CloseShapeshiftMenu();
	}
}

bool UHCShapeshiftWidget::Pick(int32 TargetPlayerId)
{
	const APlayerController* PC = GetOwningPlayer();
	const AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	if (!Character || !ShownIds.Contains(TargetPlayerId))
	{
		return false;
	}
	Character->GetShapeshift()->ServerRequestShapeshift(TargetPlayerId);
	Close();
	return true;
}

FReply UHCShapeshiftWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (HCKeys::Is(EHCBinding::Shapeshift, Key) || Key == EKeys::Escape || Key == EKeys::F1)
	{
		Close();
		return FReply::Handled();
	}
	static const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Numbers); ++Index)
	{
		if (Key == Numbers[Index] && ShownIds.IsValidIndex(Index))
		{
			Pick(ShownIds[Index]);
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHCShapeshiftWidget::Rebuild()
{
	PlayerList->ClearChildren();
	ShownIds.Reset();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	const APlayerController* PC = GetOwningPlayer();
	const APlayerState* Own = PC ? PC->PlayerState.Get() : nullptr;

	// Only living players can be copied; the dead and the ejected are not offered (docs/01 §4.2)
	TArray<const AHCPlayerState*> Players;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS);
		if (HCPS && HCPS != Own && HCPS->IsAlive())
		{
			Players.Add(HCPS);
		}
	}
	Players.Sort([](const AHCPlayerState& A, const AHCPlayerState& B) { return A.GetIdentity() < B.GetIdentity(); });

	const TWeakObjectPtr<UHCShapeshiftWidget> Weak(this);
	const double Opened = OpenedAt;
	for (int32 Index = 0; Index < Players.Num(); ++Index)
	{
		const AHCPlayerState* PS = Players[Index];
		const int32 PlayerId = PS->GetPlayerId();
		const int32 Identity = PS->GetIdentity();
		const FString Name = PS->GetPlayerName();
		ShownIds.Add(PlayerId);

		UHCPaintBox* Row = HCLook::Control(WidgetTree, FVector2D(HCShapeMenu::PlateWidth - 2.f * HCShapeMenu::PlateInset, HCShapeMenu::RowHeight),
			[Index, Identity, Name, Opened](const FHCDraw& D, const FHCPaintState& S)
			{
				HCShapeMenu::PaintRow(D, S, Index + 1, Identity, Name, Index, Opened);
			},
			[](const FVector2D&, const FVector2D&) {});
		Row->WantsHand = []() { return true; };
		Row->OnReleased = [Weak, PlayerId](bool bInside)
		{
			UHCShapeshiftWidget* Widget = Weak.Get();
			if (Widget && bInside)
			{
				Widget->Pick(PlayerId);
			}
		};
		HCUI::AddRow(PlayerList, Row, HCShapeMenu::RowGap);
	}
	if (Players.IsEmpty())
	{
		HCUI::AddRow(PlayerList, HCLook::Text(WidgetTree, NSLOCTEXT("HC", "ShiftNobody", "Niemand da, den du kopieren könntest."), 15.f, HCLook::BoneMuted), 0.f);
	}
}

void UHCShapeshiftWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	uint32 Signature = 17;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
		{
			Signature = HashCombine(Signature, GetTypeHash(HCPS->GetPlayerId() * 2 + (HCPS->IsAlive() ? 1 : 0)));
			Signature = HashCombine(Signature, GetTypeHash(HCPS->GetPlayerName()));
			Signature = HashCombine(Signature, GetTypeHash(HCPS->GetIdentity()));
		}
	}
	if (Signature != ShownSignature)
	{
		ShownSignature = Signature;
		Rebuild();
	}
	// The menu is only for a Wandler who could shapeshift right now
	const APlayerController* PC = GetOwningPlayer();
	const AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	if (!Character || Character->IsKillLocked() || Character->GetShapeshift()->GetState() != EHCShapeshiftState::Ready)
	{
		Close();
	}
}
