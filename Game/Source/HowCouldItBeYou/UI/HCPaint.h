#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Geometry.h"

class FSlateWindowElementList;

/**
 * Draws the hand-painted full-screen moments (story intro, role card) inside NativePaint.
 * Works in a virtual screen 1080 units high (the width follows the aspect ratio) that is fitted into the widget,
 * so the layout is the same at every resolution and text is still rendered sharp at its real size.
 * Every call draws on a new layer, so later calls are always on top.
 */
struct FHCPainter
{
	FHCPainter(const FGeometry& InGeometry, FSlateWindowElementList& InElements, int32 InLayer);

	/** Virtual size of the screen: 1080 high, width by aspect ratio. */
	FVector2D Size;
	/** Highest layer used so far (the return value of NativePaint). */
	mutable int32 Layer;

	/** Font of the default composite (Regular, Bold, Black, Light, Medium, Mono ...); size in virtual units, spacing in 1/1000 em. */
	static FSlateFontInfo Font(FName Typeface, float FontSize, int32 LetterSpacing = 0);
	static FVector2D Measure(const FString& Text, const FSlateFontInfo& Font);
	/** Greedy word wrap of a paragraph to a width in virtual units. Typing out the lines later never moves a word. */
	static TArray<FString> Wrap(const FString& Text, const FSlateFontInfo& Font, float Width);

	void Box(const FVector2D& Pos, const FVector2D& BoxSize, const FLinearColor& Color) const;
	/** Colour stops at fractions 0..1 of the box; bLeftToRight = the colour changes along X, otherwise along Y. */
	void Gradient(const FVector2D& Pos, const FVector2D& BoxSize, const TArray<TPair<float, FLinearColor>>& Stops, bool bLeftToRight) const;
	/** A soft glow: horizontal bands that fade towards the edges in both directions. */
	void Glow(const FVector2D& Center, const FVector2D& GlowSize, const FLinearColor& Color) const;
	void Text(const FString& Text, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& Color, float ShadowAlpha = 0.f) const;
	/** Text centred on CenterX; returns its size. */
	FVector2D TextCentered(const FString& Text, float CenterX, float Y, const FSlateFontInfo& Font, const FLinearColor& Color, float ShadowAlpha = 0.f) const;
	void Lines(const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness) const;
	void Frame(const FVector2D& Pos, const FVector2D& BoxSize, float Thickness, const FLinearColor& Color) const;
	/** L-shaped brackets on the four corners of a box. */
	void CornerBrackets(const FVector2D& Pos, const FVector2D& BoxSize, float Length, float Thickness, const FLinearColor& Color) const;
	/** The identity symbol (triangle, circle, square, cross, star, diamond, bar, wave) as an outline. */
	void Symbol(int32 Identity, const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness) const;
	/** Key cap like the interaction prompt: a solid plate with the key name on it. Returns its width. */
	float KeyCap(const FString& Key, const FVector2D& Pos, float Height, const FLinearColor& Color) const;
	/** Faint scan lines over the whole screen and a slow bright band wandering down. */
	void ScanLines(float Time, float Alpha) const;

private:
	FPaintGeometry ToPaint(const FVector2D& Pos, const FVector2D& BoxSize) const;

	const FGeometry& Geometry;
	FSlateWindowElementList& Elements;
	float Scale = 1.f;
	FVector2D Offset = FVector2D::ZeroVector;
};
