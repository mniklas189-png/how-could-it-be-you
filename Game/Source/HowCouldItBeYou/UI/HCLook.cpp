#include "UI/HCLook.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

// ---------------------------------------------------------------------------
// Painter

FHCDraw::FHCDraw(const FGeometry& InGeometry, FSlateWindowElementList& InElements, int32 InLayer, float InOpacity)
	: Size(InGeometry.GetLocalSize())
	, Layer(InLayer)
	, Opacity(InOpacity)
	, Geometry(InGeometry)
	, Elements(InElements)
{
}

FPaintGeometry FHCDraw::ToPaint(const FVector2D& Pos, const FVector2D& BoxSize) const
{
	return Geometry.ToPaintGeometry(FVector2f(BoxSize), FSlateLayoutTransform(1.f, FVector2f(Pos)));
}

FLinearColor FHCDraw::Faded(const FLinearColor& Color) const
{
	return FLinearColor(Color.R, Color.G, Color.B, Color.A * Opacity);
}

FVector2D FHCDraw::Measure(const FString& Text, const FSlateFontInfo& Font)
{
	if (Text.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		return FVector2D(0.0, Font.Size * 1.3);
	}
	return FVector2D(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font));
}

FString FHCDraw::Fit(const FString& Text, const FSlateFontInfo& Font, double MaxWidth)
{
	if (Measure(Text, Font).X <= MaxWidth)
	{
		return Text;
	}
	FString Cut = Text;
	while (Cut.Len() > 1)
	{
		Cut.LeftChopInline(1);
		const FString Candidate = Cut.TrimEnd() + TEXT("…");
		if (Measure(Candidate, Font).X <= MaxWidth)
		{
			return Candidate;
		}
	}
	return Cut;
}

void FHCDraw::Image(const FVector2D& Pos, const FVector2D& BoxSize, const FSlateBrush* Brush, const FLinearColor& Tint) const
{
	if (!Brush || BoxSize.X <= 0.0 || BoxSize.Y <= 0.0)
	{
		return;
	}
	FSlateDrawElement::MakeBox(Elements, ++Layer, ToPaint(Pos, BoxSize), Brush, ESlateDrawEffect::None, Faded(Tint));
}

void FHCDraw::Box(const FVector2D& Pos, const FVector2D& BoxSize, const FLinearColor& Color) const
{
	const FLinearColor Tint = Faded(Color);
	if (Tint.A <= 0.001f || BoxSize.X <= 0.0 || BoxSize.Y <= 0.0)
	{
		return;
	}
	// Below 1080p a unit is less than a pixel: a hairline is at least one pixel, or it falls between two and is gone
	const double Pixel = PixelSize();
	FSlateDrawElement::MakeBox(Elements, ++Layer, ToPaint(Pos, FVector2D(FMath::Max(BoxSize.X, Pixel), FMath::Max(BoxSize.Y, Pixel))),
		FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Tint);
}

double FHCDraw::PixelSize() const
{
	return 1.0 / FMath::Max(0.01f, Geometry.Scale);
}

void FHCDraw::Frame(const FVector2D& Pos, const FVector2D& BoxSize, float Thickness, const FLinearColor& Color) const
{
	// At least one pixel thick, and the right and bottom lines stay inside the box
	const double T = FMath::Max(static_cast<double>(Thickness), PixelSize());
	Box(Pos, FVector2D(BoxSize.X, T), Color);
	Box(Pos + FVector2D(0.0, BoxSize.Y - T), FVector2D(BoxSize.X, T), Color);
	Box(Pos + FVector2D(0.0, T), FVector2D(T, BoxSize.Y - 2.0 * T), Color);
	Box(Pos + FVector2D(BoxSize.X - T, T), FVector2D(T, BoxSize.Y - 2.0 * T), Color);
}

void FHCDraw::Gradient(const FVector2D& Pos, const FVector2D& BoxSize, const FLinearColor& From, const FLinearColor& To, bool bLeftToRight) const
{
	if (BoxSize.X <= 0.0 || BoxSize.Y <= 0.0)
	{
		return;
	}
	TArray<FSlateGradientStop> Stops;
	Stops.Add(FSlateGradientStop(FVector2f::ZeroVector, Faded(From)));
	Stops.Add(FSlateGradientStop(bLeftToRight ? FVector2f(static_cast<float>(BoxSize.X), 0.f) : FVector2f(0.f, static_cast<float>(BoxSize.Y)), Faded(To)));
	// Orient_Vertical = vertical stripes, the colour changes from left to right
	FSlateDrawElement::MakeGradient(Elements, ++Layer, ToPaint(Pos, BoxSize), Stops, bLeftToRight ? Orient_Vertical : Orient_Horizontal);
}

void FHCDraw::Gradient(const FVector2D& Pos, const FVector2D& BoxSize, const TArray<TPair<float, FLinearColor>>& Stops, bool bLeftToRight) const
{
	if (BoxSize.X <= 0.0 || BoxSize.Y <= 0.0 || Stops.Num() < 2)
	{
		return;
	}
	TArray<FSlateGradientStop> SlateStops;
	SlateStops.Reserve(Stops.Num());
	for (const TPair<float, FLinearColor>& Stop : Stops)
	{
		const FVector2f At = bLeftToRight ? FVector2f(static_cast<float>(BoxSize.X) * Stop.Key, 0.f) : FVector2f(0.f, static_cast<float>(BoxSize.Y) * Stop.Key);
		SlateStops.Add(FSlateGradientStop(At, Faded(Stop.Value)));
	}
	FSlateDrawElement::MakeGradient(Elements, ++Layer, ToPaint(Pos, BoxSize), SlateStops, bLeftToRight ? Orient_Vertical : Orient_Horizontal);
}

void FHCDraw::Lines(const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness) const
{
	const FLinearColor Tint = Faded(Color);
	if (Points.Num() < 2 || Tint.A <= 0.001f)
	{
		return;
	}
	// The thickness of Slate lines is in screen pixels
	FSlateDrawElement::MakeLines(Elements, ++Layer, ToPaint(FVector2D::ZeroVector, Size), Points, ESlateDrawEffect::None, Tint, true, FMath::Max(1.f, Thickness * Geometry.Scale));
}

FVector2D FHCDraw::Text(const FString& InText, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& Color) const
{
	const FVector2D TextSize = Measure(InText, Font);
	const FLinearColor Tint = Faded(Color);
	if (!InText.IsEmpty() && Tint.A > 0.001f)
	{
		FSlateDrawElement::MakeText(Elements, ++Layer, ToPaint(Pos, TextSize + FVector2D(4.0, 4.0)), InText, Font, ESlateDrawEffect::None, Tint);
	}
	return TextSize;
}

FVector2D FHCDraw::TextCentered(const FString& InText, double CenterX, double Y, const FSlateFontInfo& Font, const FLinearColor& Color) const
{
	const FVector2D TextSize = Measure(InText, Font);
	// Letter spacing adds room after the last letter too; half of it is taken back so the text sits in the middle
	const double Trailing = Font.LetterSpacing / 1000.0 * Font.Size * 0.5;
	Text(InText, FVector2D(CenterX - (TextSize.X - Trailing) * 0.5, Y), Font, Color);
	return TextSize;
}

FVector2D FHCDraw::TextRight(const FString& InText, double RightX, double Y, const FSlateFontInfo& Font, const FLinearColor& Color) const
{
	const FVector2D TextSize = Measure(InText, Font);
	const double Trailing = Font.LetterSpacing / 1000.0 * Font.Size;
	Text(InText, FVector2D(RightX - TextSize.X + Trailing, Y), Font, Color);
	return TextSize;
}

void FHCDraw::Brackets(const FVector2D& Pos, const FVector2D& BoxSize, float Length, float Thickness, const FLinearColor& Color) const
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

void FHCDraw::Symbol(int32 Identity, const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness) const
{
	// The same eight shapes as on the role card and the suits (docs/01 §13)
	const double R = Radius;
	auto Polygon = [&](int32 Corners, double StartDegrees, double Radius1, double Radius2)
	{
		TArray<FVector2D> Points;
		for (int32 i = 0; i <= Corners; ++i)
		{
			const double A = FMath::DegreesToRadians(StartDegrees + 360.0 * i / Corners);
			Points.Add(Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * ((i % 2 == 0) ? Radius1 : Radius2));
		}
		Lines(Points, Color, Thickness);
	};
	switch (((Identity % 8) + 8) % 8)
	{
	case 0: // triangle
		Polygon(3, -90.0, R, R);
		break;
	case 1: // circle
		Polygon(32, 0.0, R * 0.92, R * 0.92);
		break;
	case 2: // square
		Polygon(4, 45.0, R * 1.05, R * 1.05);
		break;
	case 3: // cross: a plus sign as an outline
	{
		const double A = R * 0.95, B = R * 0.32;
		Lines({ Center + FVector2D(-B, -A), Center + FVector2D(B, -A), Center + FVector2D(B, -B), Center + FVector2D(A, -B), Center + FVector2D(A, B),
			Center + FVector2D(B, B), Center + FVector2D(B, A), Center + FVector2D(-B, A), Center + FVector2D(-B, B), Center + FVector2D(-A, B),
			Center + FVector2D(-A, -B), Center + FVector2D(-B, -B), Center + FVector2D(-B, -A) }, Color, Thickness);
		break;
	}
	case 4: // star
		Polygon(10, -90.0, R, R * 0.42);
		break;
	case 5: // diamond
		Polygon(4, -90.0, R, R * 0.7);
		break;
	case 6: // bar
	{
		const double W = R, H = R * 0.36;
		Lines({ Center + FVector2D(-W, -H), Center + FVector2D(W, -H), Center + FVector2D(W, H), Center + FVector2D(-W, H), Center + FVector2D(-W, -H) }, Color, Thickness);
		break;
	}
	default: // wave
	{
		TArray<FVector2D> Points;
		for (int32 i = 0; i <= 32; ++i)
		{
			const double X = -R + 2.0 * R * i / 32.0;
			Points.Add(Center + FVector2D(X, FMath::Sin(X / R * PI * 2.0) * R * 0.38));
		}
		Lines(Points, Color, Thickness);
		break;
	}
	}
}

void FHCDraw::Check(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness) const
{
	const double R = Radius;
	Lines({ Center + FVector2D(-R, 0.0), Center + FVector2D(-R * 0.3, R * 0.7), Center + FVector2D(R, -R * 0.7) }, Color, Thickness);
}

void FHCDraw::Arc(const FVector2D& Center, float Radius, float From, float To, const FLinearColor& Color, float Thickness) const
{
	if (To <= From)
	{
		return;
	}
	const int32 Steps = FMath::Max(2, FMath::CeilToInt((To - From) * 64.f));
	TArray<FVector2D> Points;
	Points.Reserve(Steps + 1);
	for (int32 i = 0; i <= Steps; ++i)
	{
		const double A = (FMath::Lerp(From, To, static_cast<float>(i) / Steps) - 0.25) * 2.0 * PI;
		Points.Add(Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
	}
	Lines(Points, Color, Thickness);
}

void FHCDraw::ScanLines(float Alpha) const
{
	const FLinearColor Tint = Faded(FLinearColor(0.f, 0.f, 0.f, Alpha));
	if (Tint.A <= 0.001f)
	{
		return;
	}
	// One layer for all lines: they never overlap, so their order does not matter.
	// One pixel line in four at every resolution (a unit is less than a pixel below 1080p).
	const int32 LineLayer = ++Layer;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const double LineHeight = FMath::Max(1.0, PixelSize());
	for (double Y = 0.0; Y < Size.Y; Y += LineHeight * 4.0)
	{
		FSlateDrawElement::MakeBox(Elements, LineLayer, ToPaint(FVector2D(0.0, Y), FVector2D(Size.X, LineHeight)), White, ESlateDrawEffect::None, Tint);
	}
}

// ---------------------------------------------------------------------------
// Painted control

class SHCPaintBox : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SHCPaintBox) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UHCPaintBox* InOwner)
	{
		Owner = InOwner;
	}

	bool IsPressed() const { return bPressed; }
	bool IsOver() const { return bOver; }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const UHCPaintBox* Box = Owner.Get();
		if (!Box || !Box->OnPaintBox)
		{
			return LayerId;
		}
		const FHCDraw Draw(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle.GetColorAndOpacityTint().A);
		FHCPaintState State;
		State.bHovered = bOver;
		State.bPressed = bPressed;
		State.Mouse = LocalMouse;
		State.Time = FPlatformTime::Seconds();
		Box->OnPaintBox(Draw, State);
		return Draw.Layer;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		const UHCPaintBox* Box = Owner.Get();
		return Box ? Box->DesiredSize : FVector2D::ZeroVector;
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		UHCPaintBox* Box = Owner.Get();
		if (!Box || !Box->OnPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		bPressed = true;
		LocalMouse = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		Box->OnPressed(LocalMouse, FVector2D(MyGeometry.GetLocalSize()));
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// The second click of a fast double click counts like any other
		return OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (!bPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		bPressed = false;
		UHCPaintBox* Box = Owner.Get();
		if (Box && Box->OnReleased)
		{
			Box->OnReleased(MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()));
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		LocalMouse = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		UHCPaintBox* Box = Owner.Get();
		if (bPressed && Box && Box->OnDragged)
		{
			Box->OnDragged(LocalMouse, FVector2D(MyGeometry.GetLocalSize()));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
		bOver = true;
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		SLeafWidget::OnMouseLeave(MouseEvent);
		bOver = false;
	}

	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override
	{
		if (bPressed)
		{
			bPressed = false;
			UHCPaintBox* Box = Owner.Get();
			if (Box && Box->OnReleased)
			{
				Box->OnReleased(false);
			}
		}
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		const UHCPaintBox* Box = Owner.Get();
		if (Box && Box->OnPressed && (!Box->WantsHand || Box->WantsHand()))
		{
			return FCursorReply::Cursor(EMouseCursor::Hand);
		}
		return FCursorReply::Unhandled();
	}

private:
	TWeakObjectPtr<UHCPaintBox> Owner;
	FVector2D LocalMouse = FVector2D::ZeroVector;
	bool bPressed = false;
	bool bOver = false;
};

TSharedRef<SWidget> UHCPaintBox::RebuildWidget()
{
	MyBox = SNew(SHCPaintBox, this);
	return MyBox.ToSharedRef();
}

void UHCPaintBox::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyBox.Reset();
}

bool UHCPaintBox::IsBoxPressed() const
{
	return MyBox.IsValid() && MyBox->IsPressed();
}

bool UHCPaintBox::IsBoxHovered() const
{
	return MyBox.IsValid() && MyBox->IsOver();
}

// ---------------------------------------------------------------------------
// Look

namespace HCLook
{
	const FLinearColor Ink(FColor(8, 9, 12));
	const FLinearColor Plate(FColor(15, 17, 22));
	const FLinearColor Bone(FColor(236, 228, 212));
	const FLinearColor BoneMuted(FColor(150, 156, 168));
	const FLinearColor Blood(FColor(226, 38, 50));
	const FLinearColor Signal(FColor(112, 222, 150));
	const FLinearColor GhostBlue(0.6f, 0.75f, 1.f, 1.f);

	namespace
	{
		/** Flat brush with a hairline, for the engine widgets that stay (text box). */
		FSlateBrush PlateBrush(const FLinearColor& Fill, const FLinearColor& Outline)
		{
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Brush.TintColor = FSlateColor(Fill);
			Brush.OutlineSettings = FSlateBrushOutlineSettings(1.f, FSlateColor(Outline), 1.f);
			return Brush;
		}
	}

	FLinearColor Alpha(const FLinearColor& Color, float A)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * FMath::Clamp(A, 0.f, 1.f));
	}

	float Appear(float Time, float Delay, float Duration)
	{
		const float X = FMath::Clamp((Time - Delay) / FMath::Max(0.001f, Duration), 0.f, 1.f);
		return 1.f - FMath::Square(1.f - X);
	}

	FSlateFontInfo Caps(float Size, int32 Spacing)
	{
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle("BoldCondensed", Size);
		Info.LetterSpacing = Spacing;
		return Info;
	}

	FSlateFontInfo Body(float Size, bool bBold)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	FSlateFontInfo Mono(float Size, int32 Spacing)
	{
		FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle("Mono", Size);
		Info.LetterSpacing = Spacing;
		return Info;
	}

	FString Upper(const FText& Text)
	{
		return Text.ToUpper().ToString();
	}

	UHCPaintBox* Paint(UWidgetTree* Tree, const FVector2D& Size, TFunction<void(const FHCDraw&, const FHCPaintState&)> PaintFn)
	{
		UHCPaintBox* Box = Tree->ConstructWidget<UHCPaintBox>(UHCPaintBox::StaticClass());
		Box->DesiredSize = Size;
		Box->OnPaintBox = MoveTemp(PaintFn);
		Box->SetVisibility(ESlateVisibility::HitTestInvisible);
		return Box;
	}

	UHCPaintBox* Control(UWidgetTree* Tree, const FVector2D& Size, TFunction<void(const FHCDraw&, const FHCPaintState&)> PaintFn,
		TFunction<void(const FVector2D&, const FVector2D&)> PressFn)
	{
		UHCPaintBox* Box = Tree->ConstructWidget<UHCPaintBox>(UHCPaintBox::StaticClass());
		Box->DesiredSize = Size;
		Box->OnPaintBox = MoveTemp(PaintFn);
		Box->OnPressed = MoveTemp(PressFn);
		Box->SetVisibility(ESlateVisibility::Visible);
		return Box;
	}

	void DrawButton(const FHCDraw& D, const FHCPaintState& S, const FString& Label, const FLinearColor& Accent, bool bEnabled, float FontSize, bool bSolid)
	{
		const FVector2D Size = D.Size;
		const bool bHot = bEnabled && (S.bHovered || S.bPressed);
		const FLinearColor Edge = bEnabled ? Accent : Alpha(BoneMuted, 0.55f);
		const bool bFill = bEnabled && (bSolid || bHot);
		if (bFill)
		{
			D.Box(FVector2D::ZeroVector, Size, Alpha(Accent, S.bPressed ? 0.78f : (bSolid && !S.bHovered) ? 0.86f : 1.f));
		}
		else
		{
			D.Box(FVector2D::ZeroVector, Size, Alpha(Ink, 0.55f));
			D.Frame(FVector2D::ZeroVector, Size, 1.f, Alpha(Edge, 0.85f));
			// Heavier lower lip, like the key cap of the interact prompt
			D.Box(FVector2D(0.0, Size.Y - 2.0), FVector2D(Size.X, 2.0), Alpha(Edge, 0.85f));
		}
		// Ink on a light fill, bone on a dark one
		const FLinearColor OnFill = Accent.GetLuminance() > 0.3f ? Ink : Bone;
		const FSlateFontInfo Font = Caps(FontSize, 220);
		const FVector2D TextSize = FHCDraw::Measure(Label, Font);
		D.TextCentered(Label, Size.X * 0.5, (Size.Y - TextSize.Y) * 0.5, Font, bFill ? OnFill : Edge);
	}

	UHCPaintBox* Button(UWidgetTree* Tree, const FText& Label, const FLinearColor& Accent, TFunction<void()> OnClick, const FVector2D& Size, float FontSize)
	{
		const FString Caption = Upper(Label);
		UHCPaintBox* Box = Control(Tree, Size,
			[Caption, Accent, FontSize](const FHCDraw& D, const FHCPaintState& S) { DrawButton(D, S, Caption, Accent, true, FontSize); },
			[](const FVector2D&, const FVector2D&) {});
		Box->OnReleased = [Click = MoveTemp(OnClick)](bool bInside)
		{
			if (bInside && Click)
			{
				Click();
			}
		};
		return Box;
	}

	void DrawPlate(const FHCDraw& D, const FLinearColor& FrameColor, float PlateAlpha)
	{
		D.Box(FVector2D::ZeroVector, D.Size, Alpha(Plate, PlateAlpha));
		D.Frame(FVector2D::ZeroVector, D.Size, 1.f, FrameColor);
	}

	UHCPaintBox* Rule(UWidgetTree* Tree, const FLinearColor& Color, float Thickness)
	{
		return Paint(Tree, FVector2D(0.0, Thickness), [Color](const FHCDraw& D, const FHCPaintState&) { D.Box(FVector2D::ZeroVector, D.Size, Color); });
	}

	UTextBlock* Label(UWidgetTree* Tree, const FText& Content, float Size, const FLinearColor& Color, int32 Spacing)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Content);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetFont(Caps(Size, Spacing));
		Block->SetTextTransformPolicy(ETextTransformPolicy::ToUpper);
		Block->SetAutoWrapText(false);
		return Block;
	}

	UTextBlock* Text(UWidgetTree* Tree, const FText& Content, float Size, const FLinearColor& Color, bool bBold)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(Content);
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetFont(Body(Size, bBold));
		Block->SetAutoWrapText(true);
		return Block;
	}

	void StyleTextBox(UEditableTextBox* Box)
	{
		FEditableTextBoxStyle Style = Box->GetWidgetStyle();
		Style.BackgroundImageNormal = PlateBrush(Alpha(Ink, 0.7f), Alpha(BoneMuted, 0.4f));
		Style.BackgroundImageHovered = PlateBrush(Alpha(Ink, 0.7f), Alpha(Bone, 0.55f));
		Style.BackgroundImageFocused = PlateBrush(Alpha(Ink, 0.9f), Alpha(Bone, 0.9f));
		Style.BackgroundImageReadOnly = Style.BackgroundImageNormal;
		Style.BackgroundColor = FSlateColor(FLinearColor::White);
		Style.ForegroundColor = FSlateColor(Bone);
		Style.FocusedForegroundColor = FSlateColor(Bone);
		Style.TextStyle.ColorAndOpacity = FSlateColor(Bone);
		Style.TextStyle.Font = Body(15.f);
		Style.Padding = FMargin(12.f, 9.f);
		Box->SetWidgetStyle(Style);
	}

	void StyleScrollBox(UScrollBox* Scroll)
	{
		const FSlateNoResource None;
		FScrollBarStyle Bar = Scroll->GetWidgetBarStyle();
		Bar.NormalThumbImage = FSlateColorBrush(Alpha(Bone, 0.28f));
		Bar.HoveredThumbImage = FSlateColorBrush(Alpha(Bone, 0.6f));
		Bar.DraggedThumbImage = FSlateColorBrush(Alpha(Bone, 0.8f));
		Bar.HorizontalBackgroundImage = None;
		Bar.VerticalBackgroundImage = None;
		Bar.VerticalTopSlotImage = None;
		Bar.HorizontalTopSlotImage = None;
		Bar.VerticalBottomSlotImage = None;
		Bar.HorizontalBottomSlotImage = None;
		Bar.Thickness = 4.f;
		Scroll->SetWidgetBarStyle(Bar);
		Scroll->SetScrollbarThickness(FVector2D(4.0, 4.0));
		Scroll->SetScrollbarPadding(FMargin(6.f, 0.f, 0.f, 0.f));
		// No shadows at the edges of the list
		FScrollBoxStyle BoxStyle = Scroll->GetWidgetStyle();
		BoxStyle.TopShadowBrush = None;
		BoxStyle.BottomShadowBrush = None;
		BoxStyle.LeftShadowBrush = None;
		BoxStyle.RightShadowBrush = None;
		Scroll->SetWidgetStyle(BoxStyle);
	}
}
