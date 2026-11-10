#include "UI/HCTaskOverlayWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameSettings.h"
#include "Core/HCPlayerController.h"
#include "Misc/CommandLine.h"
#include "Rendering/DrawElements.h"
#include "Tasks/HCTaskStation.h"
#include "UI/HCUIStyle.h"

namespace
{
	const FLinearColor GoodColor(0.2f, 0.9f, 0.35f);
	const FLinearColor BadColor(1.f, 0.45f, 0.3f);
	constexpr float DialTolerance = 0.05f;
	constexpr float CurveTolerance = 0.07f;
	constexpr float TemperatureLow = 0.42f;
	constexpr float TemperatureHigh = 0.62f;

	float CurveFrequency(float Slider) { return FMath::Lerp(1.f, 4.f, Slider); }
	float CurveAmplitude(float Slider) { return FMath::Lerp(0.25f, 1.f, Slider); }
}

void UHCTaskOverlayWidget::Setup(AHCTaskStation* InStation)
{
	Station = InStation;
	const FHCTaskDefinition* Def = InStation ? InStation->GetDefinition() : nullptr;
	const FHCTaskStepDef* Step = InStation ? InStation->GetStepDef() : nullptr;
	Kind = Step ? Step->Overlay : EHCTaskOverlayKind::None;
	Duration = Step ? FMath::Max(1.f, Step->Duration) : 6.f;
#if !UE_BUILD_SHIPPING
	bAutoSolve = FParse::Param(FCommandLine::Get(), TEXT("HCAutoSolve"));
#endif
	SetIsFocusable(true);

	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.f));
	FLinearColor PanelColor = HCUI::PanelColor;
	PanelColor.A = 0.86f; // semi-transparent: you still notice what happens around you
	UBorder* Panel = HCUI::Panel(Tree, PanelColor, FMargin(26.f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Box);

	HCUI::AddRow(Box, HCUI::Text(Tree, FText::FromString(Def ? Def->DisplayName : FString(TEXT("Aufgabe"))), 22, HCUI::TextColor, true), 4.f);
	FText Instruction;
	switch (Kind)
	{
	case EHCTaskOverlayKind::Dials:
		Instruction = NSLOCTEXT("HC", "DialsHelp", "Stelle alle drei Ventile auf ihren Sollwert und halte sie dort, bis der Druck stabil ist.");
		break;
	case EHCTaskOverlayKind::Curve:
		Instruction = NSLOCTEXT("HC", "CurveHelp", "Bringe deine Kurve (hell) mit dem Signal (dunkel) zur Deckung.");
		break;
	case EHCTaskOverlayKind::Temperature:
		Instruction = NSLOCTEXT("HC", "TempHelp", "Halte die Temperatur im grünen Bereich. Kühlen: Knopf oder Leertaste gedrückt halten.");
		break;
	default:
		Instruction = NSLOCTEXT("HC", "ButtonsHelp", "Scanne die Kisten in der angegebenen Reihenfolge.");
		break;
	}
	HCUI::AddRow(Box, HCUI::Text(Tree, Instruction, 14, HCUI::MutedColor), 16.f);

	ContentBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, ContentBox, 14.f);

	ProgressBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
	ProgressBar->SetPercent(0.f);
	ProgressBar->SetFillColorAndOpacity(GoodColor);
	USizeBox* BarSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BarSize->SetHeightOverride(14.f);
	BarSize->AddChild(ProgressBar);
	HCUI::AddRow(Box, BarSize, 8.f);

	StatusText = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::TextColor);
	HCUI::AddRow(Box, StatusText, 14.f);

	UHorizontalBox* Footer = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	HCUI::AddColumn(Footer, HCUI::Text(Tree, NSLOCTEXT("HC", "OverlayCancelHint", "Esc oder W/A/S/D bricht ab"), 12, HCUI::MutedColor), 10.f, true);
	UButton* CancelButton = HCUI::Button(Tree, NSLOCTEXT("HC", "OverlayCancel", "ABBRECHEN"), HCUI::ButtonColor, nullptr, 12);
	CancelButton->OnClicked.AddDynamic(this, &UHCTaskOverlayWidget::HandleCancel);
	HCUI::AddColumn(Footer, CancelButton, 0.f);
	HCUI::AddRow(Box, Footer, 0.f);

	switch (Kind)
	{
	case EHCTaskOverlayKind::Dials:
		BuildDials();
		break;
	case EHCTaskOverlayKind::Curve:
		BuildCurve();
		break;
	case EHCTaskOverlayKind::Temperature:
		BuildTemperature();
		break;
	default:
		BuildButtons(Step ? FMath::Clamp(Step->ButtonCount, 2, 4) : 3);
		break;
	}

	HCUI::Place(Root, Panel, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 560.f);
}

float UHCTaskOverlayWidget::RollTarget(float Avoid) const
{
	// A new target at least 0.2 away from the given value
	for (int32 Try = 0; Try < 16; ++Try)
	{
		const float Value = FMath::FRandRange(0.12f, 0.88f);
		if (FMath::Abs(Value - Avoid) >= 0.2f)
		{
			return Value;
		}
	}
	return Avoid > 0.5f ? 0.2f : 0.8f;
}

USlider* UHCTaskOverlayWidget::AddSliderRow(const FText& RowLabel, UTextBlock*& OutValueText)
{
	UWidgetTree* Tree = WidgetTree;
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* LabelSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	LabelSize->SetWidthOverride(90.f);
	LabelSize->AddChild(HCUI::Text(Tree, RowLabel, 14, HCUI::TextColor, true));
	HCUI::AddColumn(Row, LabelSize, 10.f);
	USlider* Slider = Tree->ConstructWidget<USlider>(USlider::StaticClass());
	Slider->SetValue(0.5f);
	Slider->SetStepSize(0.01f);
	HCUI::AddColumn(Row, Slider, 12.f, true);
	USizeBox* ValueSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ValueSize->SetWidthOverride(170.f);
	OutValueText = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	ValueSize->AddChild(OutValueText);
	HCUI::AddColumn(Row, ValueSize, 0.f);
	HCUI::AddRow(ContentBox, Row, 10.f);
	return Slider;
}

// ---------------------------------------------------------------------------
// Dials

void UHCTaskOverlayWidget::BuildDials()
{
	for (int32 i = 0; i < 3; ++i)
	{
		UTextBlock* ValueText = nullptr;
		USlider* Slider = AddSliderRow(FText::Format(NSLOCTEXT("HC", "ValveN", "Ventil {0}"), i + 1), ValueText);
		const float Start = FMath::FRandRange(0.1f, 0.9f);
		Slider->SetValue(Start);
		Sliders.Add(Slider);
		SliderTexts.Add(ValueText);
		Targets.Add(RollTarget(Start));
	}
}

bool UHCTaskOverlayWidget::TickDials()
{
	// The pressure shifts twice: one valve gets a new target
	if ((ShiftsDone == 0 && Progress >= 0.35f) || (ShiftsDone == 1 && Progress >= 0.7f))
	{
		const int32 Index = FMath::RandRange(0, Targets.Num() - 1);
		Targets[Index] = RollTarget(Targets[Index]);
		++ShiftsDone;
	}
	int32 InZone = 0;
	for (int32 i = 0; i < Sliders.Num(); ++i)
	{
		if (bAutoSolve)
		{
			Sliders[i]->SetValue(Targets[i]);
		}
		const float Value = Sliders[i]->GetValue();
		const bool bGood = FMath::Abs(Value - Targets[i]) <= DialTolerance;
		InZone += bGood ? 1 : 0;
		SliderTexts[i]->SetText(FText::Format(NSLOCTEXT("HC", "DialValue", "Soll {0} %  ·  Ist {1} %"), FMath::RoundToInt(Targets[i] * 100.f), FMath::RoundToInt(Value * 100.f)));
		SliderTexts[i]->SetColorAndOpacity(FSlateColor(bGood ? GoodColor : BadColor));
	}
	const bool bAll = InZone == Sliders.Num();
	StatusText->SetText(bAll ? NSLOCTEXT("HC", "DialsGood", "Druck stabilisiert sich ...") : NSLOCTEXT("HC", "DialsBad", "Ventile nachstellen"));
	return bAll;
}

// ---------------------------------------------------------------------------
// Signal curve

void UHCTaskOverlayWidget::BuildCurve()
{
	UWidgetTree* Tree = WidgetTree;
	CurveArea = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	CurveArea->SetHeightOverride(150.f);
	CurveArea->AddChild(HCUI::Panel(Tree, FLinearColor(0.f, 0.f, 0.f, 0.6f), FMargin(0.f)));
	HCUI::AddRow(ContentBox, CurveArea, 12.f);

	const FText Labels[2] = { NSLOCTEXT("HC", "CurveFreq", "Frequenz"), NSLOCTEXT("HC", "CurveAmp", "Stärke") };
	for (int32 i = 0; i < 2; ++i)
	{
		UTextBlock* ValueText = nullptr;
		USlider* Slider = AddSliderRow(Labels[i], ValueText);
		const float Start = FMath::FRandRange(0.1f, 0.9f);
		Slider->SetValue(Start);
		Sliders.Add(Slider);
		SliderTexts.Add(ValueText);
		Targets.Add(RollTarget(Start));
	}
}

bool UHCTaskOverlayWidget::TickCurve()
{
	// The signal drifts once, halfway through
	if (ShiftsDone == 0 && Progress >= 0.5f)
	{
		Targets[0] = RollTarget(Targets[0]);
		Targets[1] = RollTarget(Targets[1]);
		++ShiftsDone;
	}
	bool bAll = true;
	for (int32 i = 0; i < Sliders.Num(); ++i)
	{
		if (bAutoSolve)
		{
			Sliders[i]->SetValue(Targets[i]);
		}
		const bool bGood = FMath::Abs(Sliders[i]->GetValue() - Targets[i]) <= CurveTolerance;
		bAll &= bGood;
		SliderTexts[i]->SetText(bGood ? NSLOCTEXT("HC", "CurveGood", "passt") : NSLOCTEXT("HC", "CurveBad", "nachstellen"));
		SliderTexts[i]->SetColorAndOpacity(FSlateColor(bGood ? GoodColor : BadColor));
	}
	StatusText->SetText(bAll ? NSLOCTEXT("HC", "CurveLocked", "Signal erfasst ...") : NSLOCTEXT("HC", "CurveSearch", "Signal suchen"));
	return bAll;
}

// ---------------------------------------------------------------------------
// Temperature

void UHCTaskOverlayWidget::BuildTemperature()
{
	UWidgetTree* Tree = WidgetTree;
	Temperature = 0.85f;
	TemperatureText = HCUI::Text(Tree, FText::GetEmpty(), 16, HCUI::TextColor, true);
	HCUI::AddRow(ContentBox, TemperatureText, 6.f);

	TemperatureBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
	USizeBox* BarSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BarSize->SetHeightOverride(26.f);
	BarSize->AddChild(TemperatureBar);
	HCUI::AddRow(ContentBox, BarSize, 6.f);
	HCUI::AddRow(ContentBox, HCUI::Text(Tree, FText::Format(NSLOCTEXT("HC", "TempBand", "Grüner Bereich: {0} % bis {1} %"),
		FMath::RoundToInt(TemperatureLow * 100.f), FMath::RoundToInt(TemperatureHigh * 100.f)), 13, HCUI::MutedColor), 12.f);

	CoolButton = HCUI::Button(Tree, NSLOCTEXT("HC", "Cool", "KÜHLEN (gedrückt halten)"), FLinearColor(0.05f, 0.2f, 0.5f));
	HCUI::AddRow(ContentBox, CoolButton, 0.f);
}

bool UHCTaskOverlayWidget::TickTemperature(float DeltaTime)
{
	const bool bCooling = bCoolKeyDown || (CoolButton && CoolButton->IsPressed());
	Temperature = FMath::Clamp(Temperature + (bCooling ? -0.38f : 0.17f) * DeltaTime, 0.f, 1.f);
	if (bAutoSolve)
	{
		Temperature = (TemperatureLow + TemperatureHigh) * 0.5f;
	}
	const bool bGood = Temperature >= TemperatureLow && Temperature <= TemperatureHigh;
	TemperatureBar->SetPercent(Temperature);
	TemperatureBar->SetFillColorAndOpacity(bGood ? GoodColor : Temperature > TemperatureHigh ? BadColor : FLinearColor(0.3f, 0.6f, 1.f));
	TemperatureText->SetText(FText::Format(NSLOCTEXT("HC", "TempValue", "Temperatur {0} %"), FMath::RoundToInt(Temperature * 100.f)));
	StatusText->SetText(bGood ? NSLOCTEXT("HC", "TempGood", "Kühlung stabil ...")
		: Temperature > TemperatureHigh ? NSLOCTEXT("HC", "TempHot", "Zu warm – kühlen") : NSLOCTEXT("HC", "TempCold", "Zu kalt – loslassen"));
	return bGood;
}

// ---------------------------------------------------------------------------
// Buttons in order

void UHCTaskOverlayWidget::BuildButtons(int32 Count)
{
	UWidgetTree* Tree = WidgetTree;
	for (int32 i = 0; i < Count; ++i)
	{
		Order.Add(i);
	}
	for (int32 i = Order.Num() - 1; i > 0; --i)
	{
		Order.Swap(i, FMath::RandRange(0, i));
	}
	FString OrderLine;
	for (const int32 Index : Order)
	{
		OrderLine += (OrderLine.IsEmpty() ? TEXT("") : TEXT("  >  ")) + FString::Chr(static_cast<TCHAR>(TEXT('A') + Index));
	}
	HCUI::AddRow(ContentBox, HCUI::Text(Tree, FText::Format(NSLOCTEXT("HC", "OrderLine", "Reihenfolge:  {0}"), FText::FromString(OrderLine)), 18, FLinearColor(0.95f, 0.9f, 0.6f), true), 12.f);

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	for (int32 i = 0; i < Count; ++i)
	{
		UButton* Button = HCUI::Button(Tree, FText::Format(NSLOCTEXT("HC", "CrateN", "KISTE {0}"), FText::FromString(FString::Chr(static_cast<TCHAR>(TEXT('A') + i)))));
		switch (i)
		{
		case 0: Button->OnClicked.AddDynamic(this, &UHCTaskOverlayWidget::HandleButton0); break;
		case 1: Button->OnClicked.AddDynamic(this, &UHCTaskOverlayWidget::HandleButton1); break;
		case 2: Button->OnClicked.AddDynamic(this, &UHCTaskOverlayWidget::HandleButton2); break;
		default: Button->OnClicked.AddDynamic(this, &UHCTaskOverlayWidget::HandleButton3); break;
		}
		OrderButtons.Add(Button);
		HCUI::AddColumn(Row, Button, i + 1 < Count ? 10.f : 0.f, true);
	}
	HCUI::AddRow(ContentBox, Row, 0.f);
	StatusText->SetText(NSLOCTEXT("HC", "OrderStart", "Erste Kiste wählen"));
}

void UHCTaskOverlayWidget::PressButton(int32 Index)
{
	if (bFinished || SegmentProgress >= 0.f || !Order.IsValidIndex(OrderIndex))
	{
		return; // a scan is running
	}
	if (Order[OrderIndex] == Index)
	{
		SegmentProgress = 0.f;
		return;
	}
	// Wrong one: start over
	OrderIndex = 0;
	Progress = 0.f;
	for (UButton* Button : OrderButtons)
	{
		Button->SetIsEnabled(true);
	}
	StatusText->SetText(NSLOCTEXT("HC", "OrderWrong", "Falsche Kiste – von vorn"));
	StatusText->SetColorAndOpacity(FSlateColor(BadColor));
}

void UHCTaskOverlayWidget::TickButtons(float DeltaTime)
{
	const int32 Count = Order.Num();
	if (bAutoSolve && SegmentProgress < 0.f && Order.IsValidIndex(OrderIndex))
	{
		PressButton(Order[OrderIndex]);
	}
	if (SegmentProgress >= 0.f && Order.IsValidIndex(OrderIndex))
	{
		SegmentProgress += DeltaTime / (Duration / Count);
		StatusText->SetText(FText::Format(NSLOCTEXT("HC", "OrderScanning", "Kiste {0} wird gescannt ..."), FText::FromString(FString::Chr(static_cast<TCHAR>(TEXT('A') + Order[OrderIndex])))));
		StatusText->SetColorAndOpacity(FSlateColor(HCUI::TextColor));
		if (SegmentProgress >= 1.f)
		{
			OrderButtons[Order[OrderIndex]]->SetIsEnabled(false);
			++OrderIndex;
			SegmentProgress = -1.f;
			if (OrderIndex < Count)
			{
				StatusText->SetText(NSLOCTEXT("HC", "OrderNext", "Nächste Kiste wählen"));
			}
		}
	}
	Progress = (OrderIndex + FMath::Clamp(SegmentProgress, 0.f, 1.f)) / FMath::Max(1, Count);
}

void UHCTaskOverlayWidget::HandleButton0() { PressButton(0); }
void UHCTaskOverlayWidget::HandleButton1() { PressButton(1); }
void UHCTaskOverlayWidget::HandleButton2() { PressButton(2); }
void UHCTaskOverlayWidget::HandleButton3() { PressButton(3); }

// ---------------------------------------------------------------------------

void UHCTaskOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bFinished || !ProgressBar)
	{
		return;
	}
	if (!Station.IsValid())
	{
		Cancel();
		return;
	}
	if (Kind == EHCTaskOverlayKind::Buttons || Kind == EHCTaskOverlayKind::None)
	{
		TickButtons(InDeltaTime);
	}
	else
	{
		const bool bInTheGreen = Kind == EHCTaskOverlayKind::Dials ? TickDials() : Kind == EHCTaskOverlayKind::Curve ? TickCurve() : TickTemperature(InDeltaTime);
		if (bInTheGreen)
		{
			Progress += InDeltaTime / Duration;
		}
	}
	ProgressBar->SetPercent(FMath::Clamp(Progress, 0.f, 1.f));
	if (Progress >= 1.f)
	{
		Complete();
	}
}

void UHCTaskOverlayWidget::Complete()
{
	bFinished = true;
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer()))
	{
		PC->ServerCompleteTaskStep(Station.Get());
	}
}

void UHCTaskOverlayWidget::Cancel()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer()))
	{
		PC->ServerCancelTaskStep();
	}
}

void UHCTaskOverlayWidget::HandleCancel()
{
	Cancel();
}

FReply UHCTaskOverlayWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	// Movement or Esc cancels every task at once (docs/01 §5)
	if (Key == EKeys::Escape || Key == EKeys::F1 || Key == EKeys::W || Key == EKeys::A || Key == EKeys::S || Key == EKeys::D)
	{
		Cancel();
		return FReply::Handled();
	}
	if (Key == EKeys::SpaceBar)
	{
		bCoolKeyDown = true;
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UHCTaskOverlayWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::SpaceBar)
	{
		bCoolKeyDown = false;
		return FReply::Handled();
	}
	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

int32 UHCTaskOverlayWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (Kind != EHCTaskOverlayKind::Curve || !CurveArea || Sliders.Num() < 2 || Targets.Num() < 2)
	{
		return MaxLayer;
	}
	// Curve box in the local space of this widget
	// Paint space = window space, the same space as AllottedGeometry here (the cached geometry is desktop space)
	const FGeometry& Area = CurveArea->GetPaintSpaceGeometry();
	const FVector2D AreaSize = FVector2D(Area.GetAbsoluteSize()) / FMath::Max(0.01f, AllottedGeometry.Scale);
	if (AreaSize.X < 10.0 || AreaSize.Y < 10.0)
	{
		return MaxLayer; // not laid out yet
	}
	const FVector2D TopLeft = AllottedGeometry.AbsoluteToLocal(FVector2D(Area.GetAbsolutePosition()));

	auto MakeCurve = [&](float FrequencySlider, float AmplitudeSlider)
	{
		TArray<FVector2f> Points;
		constexpr int32 Segments = 96;
		Points.Reserve(Segments + 1);
		const float Frequency = CurveFrequency(FrequencySlider);
		const float Amplitude = CurveAmplitude(AmplitudeSlider);
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float T = static_cast<float>(i) / Segments;
			const float X = static_cast<float>(TopLeft.X + 8.0 + T * (AreaSize.X - 16.0));
			const float Y = static_cast<float>(TopLeft.Y + AreaSize.Y * 0.5 - FMath::Sin(T * 2.f * PI * Frequency) * Amplitude * AreaSize.Y * 0.42);
			Points.Add(FVector2f(X, Y));
		}
		return Points;
	};
	FSlateDrawElement::MakeLines(OutDrawElements, ++MaxLayer, AllottedGeometry.ToPaintGeometry(), MakeCurve(Targets[0], Targets[1]),
		ESlateDrawEffect::None, FLinearColor(0.25f, 0.45f, 0.6f, 0.9f), true, 5.f);
	FSlateDrawElement::MakeLines(OutDrawElements, ++MaxLayer, AllottedGeometry.ToPaintGeometry(), MakeCurve(Sliders[0]->GetValue(), Sliders[1]->GetValue()),
		ESlateDrawEffect::None, FLinearColor(0.95f, 0.95f, 0.6f, 1.f), true, 2.f);
	return MaxLayer;
}
