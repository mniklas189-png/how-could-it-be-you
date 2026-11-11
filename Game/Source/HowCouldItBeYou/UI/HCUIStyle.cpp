#include "UI/HCUIStyle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace HCUI
{
	const FLinearColor Background(0.006f, 0.007f, 0.009f, 0.96f);
	const FLinearColor PanelColor(0.02f, 0.023f, 0.03f, 0.92f);
	const FLinearColor TextColor(0.82f, 0.85f, 0.9f, 1.f);
	const FLinearColor MutedColor(0.45f, 0.48f, 0.53f, 1.f);
	const FLinearColor AccentRed(0.6f, 0.05f, 0.05f, 1.f);
	const FLinearColor AccentGreen(0.05f, 0.35f, 0.15f, 1.f);
	const FLinearColor ButtonColor(0.06f, 0.07f, 0.09f, 1.f);

	UTextBlock* Text(UWidgetTree* Tree, const FText& Content, int32 Size, const FLinearColor& Color, bool bBold)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Content);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
		Block->SetAutoWrapText(true);
		return Block;
	}

	UButton* Button(UWidgetTree* Tree, const FText& Label, const FLinearColor& Color, UTextBlock** OutLabel, int32 FontSize)
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		Btn->SetBackgroundColor(Color);
		UTextBlock* LabelText = Text(Tree, Label, FontSize, FLinearColor::White, true);
		LabelText->SetJustification(ETextJustify::Center);
		LabelText->SetAutoWrapText(false);
		Btn->AddChild(LabelText);
		if (OutLabel)
		{
			*OutLabel = LabelText;
		}
		return Btn;
	}

	UBorder* Panel(UWidgetTree* Tree, const FLinearColor& Color, const FMargin& Padding)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Border->SetBrushColor(Color);
		Border->SetPadding(Padding);
		return Border;
	}

	UEditableTextBox* TextBox(UWidgetTree* Tree, const FText& Hint)
	{
		UEditableTextBox* Box = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		Box->SetHintText(Hint);
		// Dark field with light text. The engine default (light grey field, grey hint) was hard to read on the dark menu.
		FEditableTextBoxStyle Style = Box->WidgetStyle;
		const FLinearColor Field(0.05f, 0.058f, 0.074f, 1.f);
		Style.BackgroundImageNormal.TintColor = FSlateColor(Field);
		Style.BackgroundImageHovered.TintColor = FSlateColor(FLinearColor(0.07f, 0.08f, 0.10f, 1.f));
		Style.BackgroundImageFocused.TintColor = FSlateColor(FLinearColor(0.085f, 0.097f, 0.12f, 1.f));
		Style.BackgroundImageReadOnly.TintColor = FSlateColor(Field);
		Style.BackgroundColor = FSlateColor(FLinearColor::White);
		Style.ForegroundColor = FSlateColor(FLinearColor(0.95f, 0.96f, 0.98f, 1.f));
		Style.FocusedForegroundColor = FSlateColor(FLinearColor::White);
		Style.TextStyle.ColorAndOpacity = FSlateColor(FLinearColor(0.95f, 0.96f, 0.98f, 1.f));
		Style.TextStyle.Font.Size = 16;
		Style.Padding = FMargin(12.f, 9.f);
		Box->WidgetStyle = Style;
		return Box;
	}

	UWidget* Swatch(UWidgetTree* Tree, const FLinearColor& Color, float Size)
	{
		USizeBox* SizeBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		SizeBox->SetWidthOverride(Size);
		SizeBox->SetHeightOverride(Size);
		SizeBox->AddChild(Panel(Tree, Color, FMargin(0.f)));
		return SizeBox;
	}

	void AddRow(UVerticalBox* Box, UWidget* Child, float Bottom, EHorizontalAlignment Align)
	{
		UVerticalBoxSlot* BoxSlot = Box->AddChildToVerticalBox(Child);
		BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, Bottom));
		BoxSlot->SetHorizontalAlignment(Align);
	}

	void AddColumn(UHorizontalBox* Box, UWidget* Child, float Right, bool bFill)
	{
		// Short texts in a row ("Rot · Dreieck") must not wrap word by word
		if (UTextBlock* TextChild = Cast<UTextBlock>(Child))
		{
			TextChild->SetAutoWrapText(false);
		}
		UHorizontalBoxSlot* BoxSlot = Box->AddChildToHorizontalBox(Child);
		BoxSlot->SetPadding(FMargin(0.f, 0.f, Right, 0.f));
		BoxSlot->SetVerticalAlignment(VAlign_Center);
		if (bFill)
		{
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}

	UCanvasPanel* MakeRoot(UWidgetTree* Tree, const FLinearColor& BackgroundColor)
	{
		UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		Tree->RootWidget = Canvas;
		if (BackgroundColor.A > 0.f)
		{
			UBorder* Back = Panel(Tree, BackgroundColor, FMargin(0.f));
			UCanvasPanelSlot* BackSlot = Canvas->AddChildToCanvas(Back);
			BackSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			BackSlot->SetOffsets(FMargin(0.f));
		}
		return Canvas;
	}

	void Place(UCanvasPanel* Canvas, UWidget* Child, const FVector2D& Anchor, const FVector2D& Alignment, float Width)
	{
		UWidget* ToAdd = Child;
		if (Width > 0.f)
		{
			// Width via size box, height follows the content
			UWidgetTree* Tree = Cast<UWidgetTree>(Canvas->GetOuter());
			if (Tree)
			{
				USizeBox* SizeBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				SizeBox->SetWidthOverride(Width);
				SizeBox->AddChild(Child);
				ToAdd = SizeBox;
			}
		}
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(ToAdd);
		CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		CanvasSlot->SetAlignment(Alignment);
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
	}
}
