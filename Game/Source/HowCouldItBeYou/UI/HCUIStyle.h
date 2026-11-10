#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Types/SlateEnums.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UHorizontalBox;
class UWidget;
class UWidgetTree;

/**
 * Builds prototype UMG widgets in code (docs/02 §16.1: C++ first, WBP_ designs later).
 * Colours follow the dark station look of the launcher.
 */
namespace HCUI
{
	extern HOWCOULDITBEYOU_API const FLinearColor Background;
	extern HOWCOULDITBEYOU_API const FLinearColor PanelColor;
	extern HOWCOULDITBEYOU_API const FLinearColor TextColor;
	extern HOWCOULDITBEYOU_API const FLinearColor MutedColor;
	extern HOWCOULDITBEYOU_API const FLinearColor AccentRed;
	extern HOWCOULDITBEYOU_API const FLinearColor AccentGreen;
	extern HOWCOULDITBEYOU_API const FLinearColor ButtonColor;

	HOWCOULDITBEYOU_API UTextBlock* Text(UWidgetTree* Tree, const FText& Content, int32 Size, const FLinearColor& Color = TextColor, bool bBold = false);
	HOWCOULDITBEYOU_API UButton* Button(UWidgetTree* Tree, const FText& Label, const FLinearColor& Color = ButtonColor, UTextBlock** OutLabel = nullptr, int32 FontSize = 16);
	HOWCOULDITBEYOU_API UBorder* Panel(UWidgetTree* Tree, const FLinearColor& Color, const FMargin& Padding);
	HOWCOULDITBEYOU_API UEditableTextBox* TextBox(UWidgetTree* Tree, const FText& Hint);
	HOWCOULDITBEYOU_API UWidget* Swatch(UWidgetTree* Tree, const FLinearColor& Color, float Size);

	/** Adds a child to a vertical box with bottom spacing. */
	HOWCOULDITBEYOU_API void AddRow(UVerticalBox* Box, UWidget* Child, float Bottom = 10.f, EHorizontalAlignment Align = HAlign_Fill);
	HOWCOULDITBEYOU_API void AddColumn(UHorizontalBox* Box, UWidget* Child, float Right = 10.f, bool bFill = false);

	/** Full-screen root: canvas + background border; returns the canvas. */
	HOWCOULDITBEYOU_API UCanvasPanel* MakeRoot(UWidgetTree* Tree, const FLinearColor& BackgroundColor);

	/** Places a widget on a canvas at a relative anchor point (0..1) with the given alignment. */
	HOWCOULDITBEYOU_API void Place(UCanvasPanel* Canvas, UWidget* Child, const FVector2D& Anchor, const FVector2D& Alignment, float Width = 0.f);
}
