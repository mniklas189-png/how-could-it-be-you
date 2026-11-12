#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"
#include "HCLook.generated.h"

class FSlateWindowElementList;
class SHCPaintBox;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;
class UWidgetTree;

/**
 * Draws one painted control in its own local space: (0,0) is its top left corner, units are Slate units
 * (1080 of them from top to bottom of the screen). Every call draws on a new layer, so later calls are on top.
 */
struct HOWCOULDITBEYOU_API FHCDraw
{
	FHCDraw(const FGeometry& InGeometry, FSlateWindowElementList& InElements, int32 InLayer, float InOpacity);

	/** Size of the control. */
	FVector2D Size;
	/** Highest layer used so far. */
	mutable int32 Layer;
	/** Multiplies every alpha (render opacity of the parents, fade-ins). */
	float Opacity = 1.f;

	static FVector2D Measure(const FString& Text, const FSlateFontInfo& Font);
	/** The text, cut with "…" if it is wider than MaxWidth. */
	static FString Fit(const FString& Text, const FSlateFontInfo& Font, double MaxWidth);

	void Box(const FVector2D& Pos, const FVector2D& BoxSize, const FLinearColor& Color) const;
	void Frame(const FVector2D& Pos, const FVector2D& BoxSize, float Thickness, const FLinearColor& Color) const;
	/** From one colour to the other, along X (bLeftToRight) or along Y. */
	void Gradient(const FVector2D& Pos, const FVector2D& BoxSize, const FLinearColor& From, const FLinearColor& To, bool bLeftToRight) const;
	void Lines(const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness) const;
	/** Returns the size of the text. */
	FVector2D Text(const FString& Text, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& Color) const;
	FVector2D TextCentered(const FString& Text, double CenterX, double Y, const FSlateFontInfo& Font, const FLinearColor& Color) const;
	FVector2D TextRight(const FString& Text, double RightX, double Y, const FSlateFontInfo& Font, const FLinearColor& Color) const;
	/** L-shaped brackets on the four corners of a box. */
	void Brackets(const FVector2D& Pos, const FVector2D& BoxSize, float Length, float Thickness, const FLinearColor& Color) const;
	/** The identity symbol (triangle, circle, square, cross, star, diamond, bar, wave) as an outline. */
	void Symbol(int32 Identity, const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness) const;
	/** A tick mark. */
	void Check(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness) const;
	/** Part of a circle, clockwise from the top; From and To are fractions of the full turn. */
	void Arc(const FVector2D& Center, float Radius, float From, float To, const FLinearColor& Color, float Thickness) const;
	/** Faint horizontal scan lines over the whole control. */
	void ScanLines(float Alpha) const;

private:
	FPaintGeometry ToPaint(const FVector2D& Pos, const FVector2D& BoxSize) const;
	FLinearColor Faded(const FLinearColor& Color) const;
	/** One screen pixel in the units of the control. */
	double PixelSize() const;

	const FGeometry& Geometry;
	FSlateWindowElementList& Elements;
};

/** What a painted control knows about the mouse while it draws itself. */
struct FHCPaintState
{
	bool bHovered = false;
	/** The left mouse button went down on the control and is still held. */
	bool bPressed = false;
	/** Local position of the mouse (only meaningful while hovered or pressed). */
	FVector2D Mouse = FVector2D::ZeroVector;
	/** Seconds, for animations. */
	double Time = 0.0;
};

/**
 * A control that is painted in code instead of being put together from engine widgets: player cards, buttons,
 * sliders, gauges. Without OnPressed it is only a picture and lets the mouse through.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCPaintBox : public UWidget
{
	GENERATED_BODY()

public:
	TFunction<void(const FHCDraw&, const FHCPaintState&)> OnPaintBox;
	/** Left mouse button down on the control: local position and size of the control. */
	TFunction<void(const FVector2D&, const FVector2D&)> OnPressed;
	/** Mouse moved while the button is held (the position may be outside the control). */
	TFunction<void(const FVector2D&, const FVector2D&)> OnDragged;
	/** Button released; true = still over the control (a click). */
	TFunction<void(bool)> OnReleased;
	/** Whether the hand cursor is shown right now (none = always for clickable controls). */
	TFunction<bool()> WantsHand;

	/** Size the control asks for; a filling slot stretches it. */
	FVector2D DesiredSize = FVector2D::ZeroVector;

	bool IsBoxPressed() const;
	bool IsBoxHovered() const;

	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	TSharedPtr<SHCPaintBox> MyBox;
};

/**
 * The game's own look for the screens inside a round (meeting, task menu): ink black, bone white, blood red,
 * condensed capitals with wide letter spacing and hairlines instead of grey boxes - the palette of the launcher,
 * the trailer and the interact prompt.
 */
namespace HCLook
{
	extern HOWCOULDITBEYOU_API const FLinearColor Ink;
	/** Dark plate of cards and panels. */
	extern HOWCOULDITBEYOU_API const FLinearColor Plate;
	extern HOWCOULDITBEYOU_API const FLinearColor Bone;
	extern HOWCOULDITBEYOU_API const FLinearColor BoneMuted;
	extern HOWCOULDITBEYOU_API const FLinearColor Blood;
	/** "In the green": instruments that read fine. */
	extern HOWCOULDITBEYOU_API const FLinearColor Signal;
	extern HOWCOULDITBEYOU_API const FLinearColor GhostBlue;

	HOWCOULDITBEYOU_API FLinearColor Alpha(const FLinearColor& Color, float A);
	/** 0..1 from Delay on, over Duration, eased out. */
	HOWCOULDITBEYOU_API float Appear(float Time, float Delay, float Duration);

	/** Condensed capitals; spacing in 1/1000 em. Use with upper-case text. */
	HOWCOULDITBEYOU_API FSlateFontInfo Caps(float Size, int32 Spacing = 160);
	HOWCOULDITBEYOU_API FSlateFontInfo Body(float Size, bool bBold = false);
	HOWCOULDITBEYOU_API FSlateFontInfo Mono(float Size, int32 Spacing = 0);
	/** Upper case that also knows ä, ö, ü and ß. */
	HOWCOULDITBEYOU_API FString Upper(const FText& Text);

	/** A picture: painted, lets the mouse through. */
	HOWCOULDITBEYOU_API UHCPaintBox* Paint(UWidgetTree* Tree, const FVector2D& Size, TFunction<void(const FHCDraw&, const FHCPaintState&)> PaintFn);
	/** A painted control that reacts to the mouse. */
	HOWCOULDITBEYOU_API UHCPaintBox* Control(UWidgetTree* Tree, const FVector2D& Size, TFunction<void(const FHCDraw&, const FHCPaintState&)> PaintFn,
		TFunction<void(const FVector2D&, const FVector2D&)> PressFn);

	/** Draws a flat button into the whole control: hairline frame in the accent colour, filled while hovered. */
	HOWCOULDITBEYOU_API void DrawButton(const FHCDraw& D, const FHCPaintState& S, const FString& Label, const FLinearColor& Accent, bool bEnabled = true, float FontSize = 12.f, bool bSolid = false);
	/** A flat button; clicks when the mouse is released over it. */
	HOWCOULDITBEYOU_API UHCPaintBox* Button(UWidgetTree* Tree, const FText& Label, const FLinearColor& Accent, TFunction<void()> OnClick, const FVector2D& Size, float FontSize = 12.f);
	/** Dark plate with a hairline frame, as the background of a panel. */
	HOWCOULDITBEYOU_API void DrawPlate(const FHCDraw& D, const FLinearColor& FrameColor, float PlateAlpha = 0.92f);
	/** A hairline across the full width. */
	HOWCOULDITBEYOU_API UHCPaintBox* Rule(UWidgetTree* Tree, const FLinearColor& Color, float Thickness = 1.f);

	/** Condensed capitals as a text block (the text is shown in upper case). */
	HOWCOULDITBEYOU_API UTextBlock* Label(UWidgetTree* Tree, const FText& Content, float Size, const FLinearColor& Color, int32 Spacing = 160);
	HOWCOULDITBEYOU_API UTextBlock* Text(UWidgetTree* Tree, const FText& Content, float Size, const FLinearColor& Color, bool bBold = false);
	/** Flat ink field with a hairline instead of the engine's rounded grey one. */
	HOWCOULDITBEYOU_API void StyleTextBox(UEditableTextBox* Box);
	/** Thin bone scroll bar without a track. */
	HOWCOULDITBEYOU_API void StyleScrollBox(UScrollBox* Scroll);
}
