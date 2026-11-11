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
#include "Components/VerticalBoxSlot.h"
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

	// A device screen in the middle of the view: steel frame, dark glass, the station's red as accent.
	// The room around it stays visible, so you still notice who walks up behind you.
	UBorder* Frame = HCUI::Panel(Tree, FLinearColor(0.21f, 0.23f, 0.26f, 0.98f), FMargin(3.f));
	UBorder* Glass = HCUI::Panel(Tree, FLinearColor(0.026f, 0.032f, 0.044f, 0.95f), FMargin(0.f));
	Frame->SetContent(Glass);
	UVerticalBox* Shell = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Glass->SetContent(Shell);

	UBorder* Header = HCUI::Panel(Tree, FLinearColor(0.07f, 0.08f, 0.10f, 1.f), FMargin(20.f, 12.f));
	UHorizontalBox* HeaderRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	HCUI::AddColumn(HeaderRow, HCUI::Swatch(Tree, HCUI::AccentRed, 10.f), 12.f);
	HCUI::AddColumn(HeaderRow, HCUI::Text(Tree, FText::FromString((Def ? Def->DisplayName : FString(TEXT("Aufgabe"))).ToUpper()), 18, HCUI::TextColor, true), 12.f, true);
	HCUI::AddColumn(HeaderRow, HCUI::Text(Tree, FText::FromString(Step ? Step->Room.ToString().ToUpper() : FString()), 12, HCUI::MutedColor), 0.f);
	Header->SetContent(HeaderRow);
	HCUI::AddRow(Shell, Header, 0.f);
	HCUI::AddRow(Shell, HCUI::Panel(Tree, HCUI::AccentRed, FMargin(0.f, 1.f)), 0.f);

	UBorder* Body = HCUI::Panel(Tree, FLinearColor(0.f, 0.f, 0.f, 0.f), FMargin(24.f, 18.f, 24.f, 16.f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Body->SetContent(Box);
	HCUI::AddRow(Shell, Body, 0.f);

	if (Step && !Step->Label.IsEmpty())
	{
		HCUI::AddRow(Box, HCUI::Text(Tree, FText::FromString(Step->Label), 17, FLinearColor(0.93f, 0.89f, 0.83f), true), 4.f);
	}
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
	case EHCTaskOverlayKind::HoldPad:
		Instruction = NSLOCTEXT("HC", "HoldPadHelp", "Halte das Feld gedrückt, bis der Vorgang abgeschlossen ist (Maus oder Leertaste).");
		break;
	case EHCTaskOverlayKind::Transfer:
		Instruction = NSLOCTEXT("HC", "TransferHelp", "Starte die Übertragung und bleib am Terminal, bis sie durch ist.");
		break;
	case EHCTaskOverlayKind::Numbers:
		Instruction = NSLOCTEXT("HC", "NumbersHelp", "Tippe die Felder in aufsteigender Reihenfolge an: 1, 2, 3 ...");
		break;
	case EHCTaskOverlayKind::Filter:
		Instruction = NSLOCTEXT("HC", "FilterHelp", "Führe die Arbeitsschritte in der angegebenen Reihenfolge aus.");
		break;
	default:
		Instruction = NSLOCTEXT("HC", "ButtonsHelp", "Scanne die Kisten in der angegebenen Reihenfolge.");
		break;
	}
	HCUI::AddRow(Box, HCUI::Text(Tree, Instruction, 13, HCUI::MutedColor), 16.f);
	ContentBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, ContentBox, 14.f);
	ProgressBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
	ProgressBar->SetPercent(0.f);
	ProgressBar->SetFillColorAndOpacity(GoodColor);
	USizeBox* BarSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	BarSize->SetHeightOverride(10.f);
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
	case EHCTaskOverlayKind::HoldPad:
		BuildHoldPad();
		break;
	case EHCTaskOverlayKind::Transfer:
		BuildTransfer();
		break;
	case EHCTaskOverlayKind::Numbers:
		BuildNumbers(Step ? FMath::Clamp(Step->ButtonCount, 3, 10) : 6);
		break;
	case EHCTaskOverlayKind::Filter:
		BuildFilter();
		break;
	default:
		BuildButtons(Step ? FMath::Clamp(Step->ButtonCount, 2, 4) : 3);
		break;
	}
	HCUI::Place(Root, Frame, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 580.f);
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
// Button menus: crates, numbers, filter. One shared rule: the right button starts a segment that takes its
// share of the step duration; a wrong one starts the whole thing over.

void UHCTaskClickProxy::HandleClicked()
{
	if (UHCTaskOverlayWidget* Widget = Owner.Get())
	{
		Widget->PressButton(Index);
	}
}

void UHCTaskOverlayWidget::BuildButtonGrid(const TArray<FText>& Labels, int32 Columns, bool bShuffleOrder, bool bShufflePositions, int32 FontSize, float TileHeight)
{
	UWidgetTree* Tree = WidgetTree;
	const int32 Count = Labels.Num();
	ButtonLabels = Labels;
	TArray<int32> Positions;
	for (int32 i = 0; i < Count; ++i)
	{
		Order.Add(i);
		Positions.Add(i);
	}
	auto Shuffle = [](TArray<int32>& Values)
	{
		for (int32 i = Values.Num() - 1; i > 0; --i)
		{
			Values.Swap(i, FMath::RandRange(0, i));
		}
	};
	if (bShuffleOrder)
	{
		Shuffle(Order);
	}
	if (bShufflePositions)
	{
		Shuffle(Positions);
	}
	OrderButtons.SetNum(Count);
	UHorizontalBox* Row = nullptr;
	for (int32 Cell = 0; Cell < Count; ++Cell)
	{
		if (Cell % Columns == 0)
		{
			Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			HCUI::AddRow(ContentBox, Row, Cell + Columns < Count ? 8.f : 0.f);
		}
		const int32 Index = Positions[Cell];
		// Screen-blue tiles, like the consoles of the station
		UButton* Button = HCUI::Button(Tree, Labels[Index], FLinearColor(0.09f, 0.17f, 0.27f), nullptr, FontSize);
		UHCTaskClickProxy* Proxy = NewObject<UHCTaskClickProxy>(this);
		Proxy->Index = Index;
		Proxy->Owner = this;
		ClickProxies.Add(Proxy);
		Button->OnClicked.AddDynamic(Proxy, &UHCTaskClickProxy::HandleClicked);
		OrderButtons[Index] = Button;
		USizeBox* Tile = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Tile->SetHeightOverride(TileHeight);
		Tile->AddChild(Button);
		HCUI::AddColumn(Row, Tile, (Cell % Columns) + 1 < Columns ? 8.f : 0.f, true);
	}
}

void UHCTaskOverlayWidget::BuildButtons(int32 Count)
{
	TArray<FText> Labels;
	for (int32 i = 0; i < Count; ++i)
	{
		Labels.Add(FText::Format(NSLOCTEXT("HC", "CrateN", "KISTE {0}"), FText::FromString(FString::Chr(static_cast<TCHAR>(TEXT('A') + i)))));
	}
	RunningFormat = NSLOCTEXT("HC", "OrderScanning", "{0} wird gescannt ...");
	NextText = NSLOCTEXT("HC", "OrderNext", "Nächste Kiste wählen");
	WrongText = NSLOCTEXT("HC", "OrderWrong", "Falsche Kiste – von vorn");
	// The order line is built after the shuffle, so the grid comes first and the line is inserted above it
	BuildButtonGrid(Labels, Count, true, false, 16, 56.f);
	FString OrderLine;
	for (const int32 Index : Order)
	{
		OrderLine += (OrderLine.IsEmpty() ? TEXT("") : TEXT("  >  ")) + FString::Chr(static_cast<TCHAR>(TEXT('A') + Index));
	}
	UTextBlock* Line = HCUI::Text(WidgetTree, FText::Format(NSLOCTEXT("HC", "OrderLine", "Reihenfolge:  {0}"), FText::FromString(OrderLine)), 18, FLinearColor(0.95f, 0.9f, 0.6f), true);
	ContentBox->InsertChildAt(0, Line);
	if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Line->Slot))
	{
		LineSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	}
	StatusText->SetText(NSLOCTEXT("HC", "OrderStart", "Erste Kiste wählen"));
}

void UHCTaskOverlayWidget::BuildNumbers(int32 Count)
{
	TArray<FText> Labels;
	for (int32 i = 0; i < Count; ++i)
	{
		Labels.Add(FText::AsNumber(i + 1));
	}
	RunningFormat = NSLOCTEXT("HC", "NumbersRunning", "Feld {0} wird eingelesen ...");
	NextText = NSLOCTEXT("HC", "NumbersNext", "Nächstes Feld antippen");
	WrongText = NSLOCTEXT("HC", "NumbersWrong", "Falsches Feld – von vorn bei 1");
	// Ascending order, but the fields lie scattered
	BuildButtonGrid(Labels, Count > 5 ? (Count + 1) / 2 : Count, false, true, 24, 72.f);
	StatusText->SetText(NSLOCTEXT("HC", "NumbersStart", "Beginne mit Feld 1"));
}

void UHCTaskOverlayWidget::BuildFilter()
{
	const TArray<FText> Labels = {
		NSLOCTEXT("HC", "Filter1", "1  Abdeckung öffnen"), NSLOCTEXT("HC", "Filter2", "2  Alten Filter ziehen"),
		NSLOCTEXT("HC", "Filter3", "3  Neuen Filter einsetzen"), NSLOCTEXT("HC", "Filter4", "4  Abdeckung verriegeln") };
	RunningFormat = NSLOCTEXT("HC", "FilterRunning", "{0} ...");
	NextText = NSLOCTEXT("HC", "FilterNext", "Nächster Arbeitsschritt");
	WrongText = NSLOCTEXT("HC", "FilterWrong", "Falsche Reihenfolge – von vorn");
	BuildButtonGrid(Labels, 2, false, true, 14, 58.f);
	StatusText->SetText(NSLOCTEXT("HC", "FilterStart", "Beginne mit Schritt 1"));
}

void UHCTaskOverlayWidget::PressButton(int32 Index)
{
	if (bFinished || SegmentProgress >= 0.f || !Order.IsValidIndex(OrderIndex))
	{
		return; // a segment is running
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
	StatusText->SetText(WrongText);
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
		StatusText->SetText(FText::Format(RunningFormat, ButtonLabels[Order[OrderIndex]]));
		StatusText->SetColorAndOpacity(FSlateColor(HCUI::TextColor));
		if (SegmentProgress >= 1.f)
		{
			OrderButtons[Order[OrderIndex]]->SetIsEnabled(false);
			++OrderIndex;
			SegmentProgress = -1.f;
			if (OrderIndex < Count)
			{
				StatusText->SetText(NextText);
			}
		}
	}
	Progress = (OrderIndex + FMath::Clamp(SegmentProgress, 0.f, 1.f)) / FMath::Max(1, Count);
}

// ---------------------------------------------------------------------------
// Hold pad and transfer

void UHCTaskOverlayWidget::BuildHoldPad()
{
	UWidgetTree* Tree = WidgetTree;
	HoldButton = HCUI::Button(Tree, NSLOCTEXT("HC", "HoldPad", "HIER GEDRÜCKT HALTEN"), FLinearColor(0.09f, 0.17f, 0.27f), nullptr, 18);
	USizeBox* Pad = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Pad->SetHeightOverride(120.f);
	Pad->AddChild(HoldButton);
	HCUI::AddRow(ContentBox, Pad, 0.f);
}

void UHCTaskOverlayWidget::TickHoldPad(float DeltaTime)
{
	// Mouse on the field or the space bar; letting go only pauses, nothing is lost
	const bool bHolding = bAutoSolve || bCoolKeyDown || (HoldButton && HoldButton->IsPressed());
	if (bHolding)
	{
		Progress += DeltaTime / Duration;
	}
	StatusText->SetText(bHolding ? FText::Format(NSLOCTEXT("HC", "HoldRunning", "Vorgang läuft ... {0} %"), FMath::Clamp(FMath::RoundToInt(Progress * 100.f), 0, 100))
		: Progress > 0.f ? NSLOCTEXT("HC", "HoldPaused", "Unterbrochen – weiter gedrückt halten") : NSLOCTEXT("HC", "HoldStart", "Feld gedrückt halten"));
	StatusText->SetColorAndOpacity(FSlateColor(bHolding || Progress <= 0.f ? HCUI::TextColor : BadColor));
}

void UHCTaskOverlayWidget::BuildTransfer()
{
	UWidgetTree* Tree = WidgetTree;
	StartButton = HCUI::Button(Tree, NSLOCTEXT("HC", "TransferStart", "ÜBERTRAGUNG STARTEN"), FLinearColor(0.09f, 0.17f, 0.27f), nullptr, 16);
	StartButton->OnClicked.AddDynamic(this, &UHCTaskOverlayWidget::HandleStart);
	USizeBox* Tile = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Tile->SetHeightOverride(64.f);
	Tile->AddChild(StartButton);
	HCUI::AddRow(ContentBox, Tile, 0.f);
	StatusText->SetText(NSLOCTEXT("HC", "TransferIdle", "Bereit zur Übertragung"));
}

void UHCTaskOverlayWidget::HandleStart()
{
	bTransferRunning = true;
	if (StartButton)
	{
		StartButton->SetIsEnabled(false);
	}
}

void UHCTaskOverlayWidget::TickTransfer(float DeltaTime)
{
	if (bAutoSolve && !bTransferRunning)
	{
		HandleStart();
	}
	if (bTransferRunning)
	{
		Progress += DeltaTime / Duration;
		const int32 Packets = FMath::Max(1, FMath::RoundToInt(Duration));
		StatusText->SetText(FText::Format(NSLOCTEXT("HC", "TransferRunning", "Paket {0} von {1}  ·  {2} %"),
			FMath::Clamp(FMath::FloorToInt(Progress * Packets) + 1, 1, Packets), Packets, FMath::Clamp(FMath::RoundToInt(Progress * 100.f), 0, 100)));
	}
}

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
	switch (Kind)
	{
	case EHCTaskOverlayKind::Dials:
	case EHCTaskOverlayKind::Curve:
	case EHCTaskOverlayKind::Temperature:
	{
		const bool bInTheGreen = Kind == EHCTaskOverlayKind::Dials ? TickDials() : Kind == EHCTaskOverlayKind::Curve ? TickCurve() : TickTemperature(InDeltaTime);
		if (bInTheGreen)
		{
			Progress += InDeltaTime / Duration;
		}
		break;
	}
	case EHCTaskOverlayKind::HoldPad:
		TickHoldPad(InDeltaTime);
		break;
	case EHCTaskOverlayKind::Transfer:
		TickTransfer(InDeltaTime);
		break;
	default:
		TickButtons(InDeltaTime);
		break;
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
