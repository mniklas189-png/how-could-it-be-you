#include "UI/HCRadioWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Roles/HCJobManager.h"
#include "UI/HCHUD.h"
#include "UI/HCLook.h"
#include "UI/HCUIStyle.h"

namespace HCRadioLook
{
	constexpr float Width = 520.f;
	constexpr float Height = 330.f;
}

void UHCRadioWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	const TWeakObjectPtr<UHCRadioWidget> Weak(this);

	UOverlay* Panel = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	UOverlaySlot* BackSlot = Panel->AddChildToOverlay(HCLook::Paint(Tree, FVector2D(HCRadioLook::Width, HCRadioLook::Height), [Weak](const FHCDraw& D, const FHCPaintState& S)
	{
		if (const UHCRadioWidget* Widget = Weak.Get())
		{
			Widget->PaintPanel(D, S);
		}
	}));
	BackSlot->SetHorizontalAlignment(HAlign_Fill);
	BackSlot->SetVerticalAlignment(VAlign_Fill);

	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UOverlaySlot* ColumnSlot = Panel->AddChildToOverlay(Column);
	ColumnSlot->SetPadding(FMargin(32.f, 104.f, 32.f, 28.f));
	MessageBox = HCUI::TextBox(Tree, NSLOCTEXT("HC", "RadioHint", "Was sollen alle hören? (Enter sendet)"));
	HCLook::StyleTextBox(MessageBox);
	MessageBox->SetClearKeyboardFocusOnCommit(false);
	MessageBox->OnTextCommitted.AddDynamic(this, &UHCRadioWidget::HandleCommitted);
	HCUI::AddRow(Column, MessageBox, 14.f);
	HCUI::AddRow(Column, HCLook::Button(Tree, NSLOCTEXT("HC", "RadioSend", "Durchsage senden"), HCLook::Bone, [Weak]()
	{
		if (UHCRadioWidget* Widget = Weak.Get())
		{
			Widget->SendAnnouncement(Widget->MessageBox->GetText().ToString());
		}
	}, FVector2D(0.f, 40.f), 13.f), 10.f);
	HCUI::AddRow(Column, HCLook::Button(Tree, NSLOCTEXT("HC", "RadioMeeting", "Meeting aus der Ferne"), HCLook::Blood, [Weak]()
	{
		if (UHCRadioWidget* Widget = Weak.Get())
		{
			Widget->CallMeeting();
		}
	}, FVector2D(0.f, 40.f), 13.f), 10.f);
	HCUI::AddRow(Column, HCLook::Button(Tree, NSLOCTEXT("HC", "RadioCancel", "Abbrechen"), HCLook::BoneMuted, [Weak]()
	{
		if (UHCRadioWidget* Widget = Weak.Get())
		{
			Widget->Close();
		}
	}, FVector2D(0.f, 32.f), 11.f), 0.f);

	USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(HCRadioLook::Width);
	Size->SetHeightOverride(HCRadioLook::Height);
	Size->SetContent(Panel);
	HCUI::Place(Root, Size, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), HCRadioLook::Width);
}

void UHCRadioWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MessageBox)
	{
		MessageBox->SetText(FText::GetEmpty());
		MessageBox->SetKeyboardFocus();
	}
}

bool UHCRadioWidget::HasCharge() const
{
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	return PS && PS->GetJobState().Charges > 0;
}

void UHCRadioWidget::PaintPanel(const FHCDraw& D, const FHCPaintState& S) const
{
	HCLook::DrawPlate(D, HCLook::Alpha(HCLook::Bone, 0.4f));
	D.Text(HCLook::Upper(NSLOCTEXT("HC", "RadioTitle", "Funkgerät")), FVector2D(32.0, 28.0), HCLook::Caps(22.f, 220), HCLook::Bone);
	const FText Line = HasCharge() ? NSLOCTEXT("HC", "RadioReady", "Einmal bis zum nächsten Meeting: eine Durchsage an alle oder ein Meeting von hier aus.")
		: NSLOCTEXT("HC", "RadioUsed", "Schon benutzt. Nach dem nächsten Meeting geht es wieder.");
	// Two lines at most, broken between words
	const FSlateFontInfo Font = HCLook::Body(13.f);
	TArray<FString> Words;
	Line.ToString().ParseIntoArrayWS(Words);
	FString Rows[2];
	int32 Row = 0;
	for (const FString& Word : Words)
	{
		const FString Longer = Rows[Row].IsEmpty() ? Word : Rows[Row] + TEXT(" ") + Word;
		if (Row == 0 && !Rows[Row].IsEmpty() && FHCDraw::Measure(Longer, Font).X > HCRadioLook::Width - 64.0)
		{
			Row = 1;
			Rows[Row] = Word;
		}
		else
		{
			Rows[Row] = Longer;
		}
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		D.Text(FHCDraw::Fit(Rows[Index], Font, HCRadioLook::Width - 64.0), FVector2D(32.0, 62.0 + Index * 18.0), Font, HasCharge() ? HCLook::BoneMuted : HCLook::Blood);
	}
}

void UHCRadioWidget::HandleCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		SendAnnouncement(Text.ToString());
	}
}

void UHCRadioWidget::SendAnnouncement(const FString& Text)
{
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	const FString Clean = Text.TrimStartAndEnd();
	if (!PC || Clean.IsEmpty() || !HasCharge())
	{
		return;
	}
	PC->ServerUseAbility(static_cast<uint8>(EHCAbilityAction::RadioAnnounce), Clean.Left(80));
	Close();
}

void UHCRadioWidget::CallMeeting()
{
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer()); PC && HasCharge())
	{
		PC->ServerUseAbility(static_cast<uint8>(EHCAbilityAction::RadioMeeting), FString());
	}
	Close();
}

void UHCRadioWidget::Close()
{
	if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->CloseModal(this);
	}
}

FReply UHCRadioWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F1)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
