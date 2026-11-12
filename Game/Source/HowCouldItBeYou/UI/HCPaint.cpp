#include "UI/HCPaint.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr double VirtualHeight = 1080.0;
}

FHCPainter::FHCPainter(const FGeometry& InGeometry, FSlateWindowElementList& InElements, int32 InLayer)
	: Layer(InLayer)
	, Geometry(InGeometry)
	, Elements(InElements)
{
	const FVector2D Local = FVector2D(InGeometry.GetLocalSize());
	Scale = static_cast<float>(FMath::Max(Local.Y, 1.0) / VirtualHeight);
	Size = FVector2D(Local.X / Scale, VirtualHeight);
	Offset = FVector2D::ZeroVector;
}

FSlateFontInfo FHCPainter::Font(FName Typeface, float FontSize, int32 LetterSpacing)
{
	FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle(Typeface, FMath::Max(1, FMath::RoundToInt(FontSize)));
	Info.LetterSpacing = LetterSpacing;
	return Info;
}

FVector2D FHCPainter::Measure(const FString& Text, const FSlateFontInfo& Font)
{
	if (Text.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		return FVector2D(0.0, Font.Size * 1.3);
	}
	return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
}

TArray<FString> FHCPainter::Wrap(const FString& Text, const FSlateFontInfo& Font, float Width)
{
	TArray<FString> Lines;
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "), true);
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		if (!Line.IsEmpty() && Measure(Candidate, Font).X > Width)
		{
			Lines.Add(Line);
			Line = Word;
		}
		else
		{
			Line = Candidate;
		}
	}
	if (!Line.IsEmpty())
	{
		Lines.Add(Line);
	}
	return Lines;
}

FPaintGeometry FHCPainter::ToPaint(const FVector2D& Pos, const FVector2D& BoxSize) const
{
	return Geometry.ToPaintGeometry(FVector2f(BoxSize), FSlateLayoutTransform(Scale, FVector2f(Offset + Pos * Scale)));
}

void FHCPainter::Box(const FVector2D& Pos, const FVector2D& BoxSize, const FLinearColor& Color) const
{
	if (Color.A <= 0.001f || BoxSize.X <= 0.0 || BoxSize.Y <= 0.0)
	{
		return;
	}
	FSlateDrawElement::MakeBox(Elements, ++Layer, ToPaint(Pos, BoxSize), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
}

void FHCPainter::Gradient(const FVector2D& Pos, const FVector2D& BoxSize, const TArray<TPair<float, FLinearColor>>& Stops, bool bLeftToRight) const
{
	if (Stops.Num() < 2 || BoxSize.X <= 0.0 || BoxSize.Y <= 0.0)
	{
		return;
	}
	TArray<FSlateGradientStop> SlateStops;
	for (const TPair<float, FLinearColor>& Stop : Stops)
	{
		// Stops are in the local space of the box (virtual units)
		SlateStops.Add(FSlateGradientStop(bLeftToRight ? FVector2f(BoxSize.X * Stop.Key, 0.f) : FVector2f(0.f, BoxSize.Y * Stop.Key), Stop.Value));
	}
	// Orient_Vertical = vertical stripes, the colour changes from left to right
	FSlateDrawElement::MakeGradient(Elements, ++Layer, ToPaint(Pos, BoxSize), SlateStops, bLeftToRight ? Orient_Vertical : Orient_Horizontal);
}

void FHCPainter::Glow(const FVector2D& Center, const FVector2D& GlowSize, const FLinearColor& Color) const
{
	// Bands that get thinner towards the middle: stacked, they fade out to the top and bottom as well
	constexpr int32 Bands = 10;
	const FLinearColor Clear(Color.R, Color.G, Color.B, 0.f);
	const FLinearColor Step(Color.R, Color.G, Color.B, Color.A / Bands);
	for (int32 i = 0; i < Bands; ++i)
	{
		const double F = 1.0 - static_cast<double>(i) / Bands;
		const FVector2D BandSize(GlowSize.X * (0.55 + 0.45 * F), GlowSize.Y * F);
		Gradient(Center - BandSize * 0.5, BandSize, { { 0.f, Clear }, { 0.5f, Step }, { 1.f, Clear } }, true);
	}
}

void FHCPainter::Text(const FString& Text, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& Color, float ShadowAlpha) const
{
	if (Text.IsEmpty() || Color.A <= 0.001f)
	{
		return;
	}
	const FVector2D TextSize = Measure(Text, Font) + FVector2D(4.0, 4.0);
	if (ShadowAlpha > 0.f)
	{
		FSlateDrawElement::MakeText(Elements, ++Layer, ToPaint(Pos + FVector2D(2.0, 2.0), TextSize), Text, Font, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, ShadowAlpha * Color.A));
	}
	FSlateDrawElement::MakeText(Elements, ++Layer, ToPaint(Pos, TextSize), Text, Font, ESlateDrawEffect::None, Color);
}

FVector2D FHCPainter::TextCentered(const FString& Text, float CenterX, float Y, const FSlateFontInfo& Font, const FLinearColor& Color, float ShadowAlpha) const
{
	const FVector2D TextSize = Measure(Text, Font);
	// Letter spacing adds room after the last letter too; half of it is taken back so the text sits in the middle
	const double Trailing = Font.LetterSpacing / 1000.0 * Font.Size * 0.5;
	this->Text(Text, FVector2D(CenterX - (TextSize.X - Trailing) * 0.5, Y), Font, Color, ShadowAlpha);
	return TextSize;
}

void FHCPainter::Lines(const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness) const
{
	if (Points.Num() < 2 || Color.A <= 0.001f)
	{
		return;
	}
	// The thickness of Slate lines is in screen pixels
	const float Pixels = Thickness * Scale * Geometry.Scale;
	FSlateDrawElement::MakeLines(Elements, ++Layer, ToPaint(FVector2D::ZeroVector, Size), Points, ESlateDrawEffect::None, Color, true, Pixels);
}

void FHCPainter::Frame(const FVector2D& Pos, const FVector2D& BoxSize, float Thickness, const FLinearColor& Color) const
{
	Box(Pos, FVector2D(BoxSize.X, Thickness), Color);
	Box(Pos + FVector2D(0.0, BoxSize.Y - Thickness), FVector2D(BoxSize.X, Thickness), Color);
	Box(Pos + FVector2D(0.0, Thickness), FVector2D(Thickness, BoxSize.Y - 2.0 * Thickness), Color);
	Box(Pos + FVector2D(BoxSize.X - Thickness, Thickness), FVector2D(Thickness, BoxSize.Y - 2.0 * Thickness), Color);
}

void FHCPainter::CornerBrackets(const FVector2D& Pos, const FVector2D& BoxSize, float Length, float Thickness, const FLinearColor& Color) const
{
	const FVector2D Corners[4] = { Pos, Pos + FVector2D(BoxSize.X, 0.0), Pos + FVector2D(0.0, BoxSize.Y), Pos + BoxSize };
	for (int32 i = 0; i < 4; ++i)
	{
		const bool bRight = (i % 2) == 1;
		const bool bBottom = i >= 2;
		const FVector2D C = Corners[i];
		Box(FVector2D(bRight ? C.X - Length : C.X, bBottom ? C.Y - Thickness : C.Y), FVector2D(Length, Thickness), Color);
		Box(FVector2D(bRight ? C.X - Thickness : C.X, bBottom ? C.Y - Length : C.Y), FVector2D(Thickness, Length), Color);
	}
}

void FHCPainter::Symbol(int32 Identity, const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness) const
{
	const double R = Radius;
	auto Polygon = [&](int32 Corners, double StartDegrees, double Radius1, double Radius2)
	{
		// Radius2 != Radius1 makes a star (every second corner further in)
		TArray<FVector2D> Points;
		for (int32 i = 0; i <= Corners; ++i)
		{
			const double A = FMath::DegreesToRadians(StartDegrees + 360.0 * i / Corners);
			const double Rad = (i % 2 == 0) ? Radius1 : Radius2;
			Points.Add(Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rad);
		}
		Lines(Points, Color, Thickness);
	};
	switch (((Identity % 8) + 8) % 8)
	{
	case 0: // Dreieck
		Polygon(3, -90.0, R, R);
		break;
	case 1: // Kreis
		Polygon(48, 0.0, R * 0.92, R * 0.92);
		break;
	case 2: // Quadrat
		Polygon(4, 45.0, R * 1.05, R * 1.05);
		break;
	case 3: // Kreuz: a plus sign as an outline
	{
		const double A = R * 0.95, B = R * 0.32;
		Lines({ Center + FVector2D(-B, -A), Center + FVector2D(B, -A), Center + FVector2D(B, -B), Center + FVector2D(A, -B), Center + FVector2D(A, B),
			Center + FVector2D(B, B), Center + FVector2D(B, A), Center + FVector2D(-B, A), Center + FVector2D(-B, B), Center + FVector2D(-A, B),
			Center + FVector2D(-A, -B), Center + FVector2D(-B, -B), Center + FVector2D(-B, -A) }, Color, Thickness);
		break;
	}
	case 4: // Stern
		Polygon(10, -90.0, R, R * 0.42);
		break;
	case 5: // Raute
		Polygon(4, -90.0, R, R * 0.7);
		break;
	case 6: // Balken
	{
		const double W = R * 1.0, H = R * 0.36;
		Lines({ Center + FVector2D(-W, -H), Center + FVector2D(W, -H), Center + FVector2D(W, H), Center + FVector2D(-W, H), Center + FVector2D(-W, -H) }, Color, Thickness);
		break;
	}
	default: // Welle
	{
		TArray<FVector2D> Points;
		for (int32 i = 0; i <= 40; ++i)
		{
			const double X = -R + 2.0 * R * i / 40.0;
			Points.Add(Center + FVector2D(X, FMath::Sin(X / R * PI * 2.0) * R * 0.38));
		}
		Lines(Points, Color, Thickness);
		break;
	}
	}
}

float FHCPainter::KeyCap(const FString& Key, const FVector2D& Pos, float Height, const FLinearColor& Color) const
{
	const FSlateFontInfo CapFont = Font(TEXT("Bold"), Height * 0.5f);
	const FVector2D KeySize = Measure(Key, CapFont);
	const float Width = FMath::Max(Height, static_cast<float>(KeySize.X) + Height * 0.6f);
	// Solid plate with a darker lower lip, like a real key
	Box(Pos + FVector2D(0.0, 3.0), FVector2D(Width, Height), Color * FLinearColor(0.5f, 0.5f, 0.5f, 1.f));
	Box(Pos, FVector2D(Width, Height - 1.0), Color);
	Text(Key, Pos + FVector2D((Width - KeySize.X) * 0.5, (Height - 1.0 - KeySize.Y) * 0.5 + Height * 0.05), CapFont, FLinearColor(0.04f, 0.04f, 0.05f, Color.A));
	return Width;
}

void FHCPainter::ScanLines(float Time, float Alpha) const
{
	if (Alpha <= 0.f)
	{
		return;
	}
	// One layer for all lines: they never overlap, so their order does not matter
	const int32 LineLayer = ++Layer;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	for (double Y = 0.0; Y < Size.Y; Y += 4.0)
	{
		FSlateDrawElement::MakeBox(Elements, LineLayer, ToPaint(FVector2D(0.0, Y), FVector2D(Size.X, 1.0)), White, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, Alpha));
	}
	const double BandY = FMath::Fmod(static_cast<double>(Time) * 90.0, Size.Y + 400.0) - 200.0;
	Gradient(FVector2D(0.0, BandY), FVector2D(Size.X, 200.0), { { 0.f, FLinearColor(1.f, 1.f, 1.f, 0.f) }, { 0.5f, FLinearColor(1.f, 1.f, 1.f, Alpha * 0.1f) }, { 1.f, FLinearColor(1.f, 1.f, 1.f, 0.f) } }, false);
}
