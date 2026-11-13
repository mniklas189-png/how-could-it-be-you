#include "UI/HCTaskOverlayWidget.h"

#include "Audio/HCProceduralAudio.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/HCGameSettings.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCPlayerController.h"
#include "Core/HCText.h"
#include "Misc/CommandLine.h"
#include "Tasks/HCTaskStation.h"
#include "UI/HCLook.h"
#include "UI/HCUIStyle.h"

namespace HCTaskLook
{
	constexpr float PanelWidth = 620.f;
	constexpr float DialTolerance = 0.04f;
	constexpr float CurveTolerance = 0.06f;
	constexpr float TemperatureLow = 0.45f;
	constexpr float TemperatureHigh = 0.6f;
	/** With the tip on, the zones of the sliders are this much wider. */
	constexpr float HintTolerance = 1.8f;
	/** The tip comes after 1.5 x the step duration plus this many seconds in the menu. */
	constexpr double HintGraceSeconds = 20.0;

	// Cables
	constexpr double WireRowHeight = 50.0;
	constexpr double WireCapWidth = 48.0;
	/** A click this far beside a cap still counts for it. */
	constexpr double WireReach = 44.0;
	// Memory
	constexpr int32 MemoryRounds = 3;
	constexpr int32 MemoryFirstLength = 3;
	constexpr int32 MemoryColumns = 3;
	constexpr double MemoryTileHeight = 70.0;
	constexpr double MemoryGap = 8.0;
	/** Seconds before the first field lights up, and per field. */
	constexpr double MemoryLead = 0.6;
	constexpr double MemoryStep = 0.62;

	static FLinearColor Amber() { return FLinearColor(0.95f, 0.68f, 0.18f); }
	/** Red, blue, yellow and pink as on every wiring panel, then green and white. */
	static FLinearColor WireColor(int32 Wire)
	{
		switch (Wire % 6)
		{
		case 0: return FLinearColor(0.86f, 0.1f, 0.1f);
		case 1: return FLinearColor(0.14f, 0.3f, 0.95f);
		case 2: return FLinearColor(0.98f, 0.84f, 0.08f);
		case 3: return FLinearColor(0.92f, 0.12f, 0.85f);
		case 4: return FLinearColor(0.2f, 0.82f, 0.36f);
		default: return FLinearColor(0.9f, 0.88f, 0.82f);
		}
	}
	/** The track of a slider inside its row: after the label, before the value. */
	constexpr double TrackLeft = 104.0;
	constexpr double TrackRight = 96.0;

	static float CurveFrequency(float Slider) { return FMath::Lerp(1.f, 4.f, Slider); }
	static float CurveAmplitude(float Slider) { return FMath::Lerp(0.25f, 1.f, Slider); }
}

void UHCTaskOverlayWidget::Setup(AHCTaskStation* InStation)
{
	Station = InStation;
	const FHCTaskDefinition* Def = InStation ? InStation->GetDefinition() : nullptr;
	const FHCTaskStepDef* Step = InStation ? InStation->GetStepDef() : nullptr;
	Kind = Step ? Step->Overlay : EHCTaskOverlayKind::None;
	Duration = Step ? FMath::Max(1.f, Step->Duration) : 6.f;
#if !UE_BUILD_SHIPPING
	bAutoSolveWanted = FParse::Param(FCommandLine::Get(), TEXT("HCAutoSolve"));
	FParse::Value(FCommandLine::Get(), TEXT("HCAutoSolveDelay="), AutoSolveDelay);
	FParse::Value(FCommandLine::Get(), TEXT("HCHintAfter="), HintAfterOverride);
	bAutoSolve = bAutoSolveWanted && AutoSolveDelay <= 0.f;
#endif
	SetIsFocusable(true);
	OpenedAt = FPlatformTime::Seconds();

	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.f));
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	auto Fill = [](UOverlaySlot* OverlaySlot)
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	};

	// A device screen in the middle of the view: ink glass, a hairline frame with bone corners, the station's red
	// as accent. The room around it stays visible, so you still notice who walks up behind you.
	UOverlay* Frame = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	FramePanel = Frame;
	Fill(Frame->AddChildToOverlay(HCLook::Paint(Tree, FVector2D::ZeroVector, [](const FHCDraw& D, const FHCPaintState&)
	{
		D.Box(FVector2D::ZeroVector, D.Size, HCLook::Alpha(HCLook::Ink, 0.94f));
		D.Frame(FVector2D::ZeroVector, D.Size, 1.f, HCLook::Alpha(HCLook::BoneMuted, 0.4f));
		D.Brackets(FVector2D::ZeroVector, D.Size, 18.f, 2.f, HCLook::Alpha(HCLook::Bone, 0.85f));
	})));
	UVerticalBox* Shell = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Fill(Frame->AddChildToOverlay(Shell));
	Fill(Frame->AddChildToOverlay(HCLook::Paint(Tree, FVector2D::ZeroVector, [](const FHCDraw& D, const FHCPaintState&) { D.ScanLines(0.1f); })));

	const FString Title = HCLook::Upper(Def ? Def->DisplayName : NSLOCTEXT("HC", "TaskFallback", "Aufgabe"));
	const FString Room = Step ? HCLook::Upper(HCText::Room(Step->Room)) : FString();
	HCUI::AddRow(Shell, HCLook::Paint(Tree, FVector2D(0.0, 54.0), [Weak, Title, Room](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->PaintHeader(D, Title, Room);
		}
	}), 0.f);

	UBorder* Body = HCUI::Panel(Tree, FLinearColor(0.f, 0.f, 0.f, 0.f), FMargin(26.f, 18.f, 26.f, 18.f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Body->SetContent(Box);
	HCUI::AddRow(Shell, Body, 0.f);

	// What to do here, and which step of the task this is. A task of one step is named in the header already.
	const bool bSteps = Def && Def->Steps.Num() > 1;
	if (Step && !Step->Label.IsEmpty() && (bSteps || !HCLook::Upper(Step->Label).Equals(Title)))
	{
		UHorizontalBox* StepRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(StepRow, HCLook::Text(Tree, Step->Label, 17.f, HCLook::Bone, true), 12.f, true);
		if (bSteps)
		{
			HCUI::AddColumn(StepRow, HCLook::Label(Tree, FText::Format(NSLOCTEXT("HC", "TaskStepOfFmt", "Schritt {0} / {1}"), InStation->GetStepIndex() + 1, Def->Steps.Num()),
				11.f, HCLook::BoneMuted, 220), 0.f);
		}
		HCUI::AddRow(Box, StepRow, 4.f);
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
	case EHCTaskOverlayKind::Wires:
		Instruction = NSLOCTEXT("HC", "WiresHelp", "Zieh jedes Kabel in die Buchse seiner Farbe. Eine falsche Buchse gibt einen Kurzschluss: dann fliegen alle Kabel wieder raus.");
		break;
	case EHCTaskOverlayKind::Switches:
		Instruction = NSLOCTEXT("HC", "SwitchesHelp", "Stell die Schalter so, wie die Lampen zeigen: Lampe an = Schalter oben. Achtung: jeder Schalter kippt seinen rechten Nachbarn mit.");
		break;
	case EHCTaskOverlayKind::Memory:
		Instruction = NSLOCTEXT("HC", "MemoryHelp", "Merk dir, in welcher Reihenfolge die Felder aufleuchten, und tippe sie genauso an. Drei Runden, jede länger.");
		break;
	default:
		Instruction = NSLOCTEXT("HC", "ButtonsHelp", "Scanne die Kisten in der angegebenen Reihenfolge.");
		break;
	}
	HCUI::AddRow(Box, HCLook::Text(Tree, Instruction, 13.f, HCLook::BoneMuted), 18.f);

	ContentBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, ContentBox, 16.f);

	HCUI::AddRow(Box, HCLook::Paint(Tree, FVector2D(0.0, 22.0), [Weak](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->PaintProgress(D);
		}
	}), 4.f);
	StatusText = HCLook::Label(Tree, FText::GetEmpty(), 12.f, HCLook::Bone, 200);
	HCUI::AddRow(Box, StatusText, 16.f);
	// The tip for somebody who has been at it for a long time (NativeTick shows it)
	HintText = HCLook::Text(Tree, FText::GetEmpty(), 13.f, HCTaskLook::Amber());
	HintText->SetVisibility(ESlateVisibility::Collapsed);
	HCUI::AddRow(Box, HintText, 16.f);

	HCUI::AddRow(Box, HCLook::Rule(Tree, HCLook::Alpha(HCLook::Bone, 0.12f)), 12.f);
	UHorizontalBox* Footer = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	HCUI::AddColumn(Footer, HCLook::Text(Tree, FText::Format(NSLOCTEXT("HC", "OverlayCancelHintFmt", "Esc oder {0}/{1}/{2}/{3} bricht ab"), HCKeys::ShortName(EHCBinding::Forward),
		HCKeys::ShortName(EHCBinding::Left), HCKeys::ShortName(EHCBinding::Back), HCKeys::ShortName(EHCBinding::Right)), 11.f, HCLook::BoneMuted), 10.f, true);
	HCUI::AddColumn(Footer, HCLook::Button(Tree, NSLOCTEXT("HC", "OverlayCancel", "ABBRECHEN"), HCLook::Alpha(HCLook::Bone, 0.85f), [Weak]()
	{
		if (UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->Cancel();
		}
	}, FVector2D(132.0, 32.0), 11.f), 0.f);
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
	case EHCTaskOverlayKind::Wires:
		BuildWires(Step ? FMath::Clamp(Step->ButtonCount, 3, 6) : 4);
		break;
	case EHCTaskOverlayKind::Switches:
		BuildSwitches(Step ? FMath::Clamp(Step->ButtonCount, 4, 8) : 6);
		break;
	case EHCTaskOverlayKind::Memory:
		BuildMemory(Step ? FMath::Clamp(Step->ButtonCount, 4, 8) : 6); // 8 symbols exist
		break;
	default:
		BuildButtons(Step ? FMath::Clamp(Step->ButtonCount, 2, 4) : 3);
		break;
	}
	HCUI::Place(Root, Frame, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), HCTaskLook::PanelWidth);
}

void UHCTaskOverlayWidget::PaintHeader(const FHCDraw& D, const FString& Title, const FString& Room) const
{
	const FVector2D Size = D.Size;
	D.Box(FVector2D::ZeroVector, Size, HCLook::Plate);
	D.Box(FVector2D(22.0, (Size.Y - 9.0) * 0.5), FVector2D(9.0, 9.0), HCLook::Blood);
	const FSlateFontInfo RoomFont = HCLook::Mono(11.f, 240);
	const FVector2D RoomSize = FHCDraw::Measure(Room, RoomFont);
	D.TextRight(Room, Size.X - 22.0, (Size.Y - RoomSize.Y) * 0.5, RoomFont, HCLook::BoneMuted);
	const FSlateFontInfo TitleFont = HCLook::Caps(16.f, 220);
	const FString Shown = FHCDraw::Fit(Title, TitleFont, Size.X - 44.0 - 40.0 - RoomSize.X);
	D.Text(Shown, FVector2D(42.0, (Size.Y - FHCDraw::Measure(Shown, TitleFont).Y) * 0.5), TitleFont, HCLook::Bone);
	D.Box(FVector2D(0.0, Size.Y - 1.0), FVector2D(Size.X, 1.0), HCLook::Blood);
}

void UHCTaskOverlayWidget::PaintProgress(const FHCDraw& D) const
{
	// A hairline that fills; green only while the puzzle is in the green
	const double BarWidth = D.Size.X - 58.0;
	const float Shown = FMath::Clamp(Progress, 0.f, 1.f);
	D.Box(FVector2D(0.0, 9.0), FVector2D(BarWidth, 4.0), HCLook::Alpha(HCLook::Bone, 0.14f));
	D.Box(FVector2D(0.0, 9.0), FVector2D(BarWidth * Shown, 4.0), bAdvancing ? HCLook::Signal : HCLook::Alpha(HCLook::Bone, 0.75f));
	// One notch per tile of a sequence
	for (int32 Notch = 1; Notch < Order.Num(); ++Notch)
	{
		D.Box(FVector2D(BarWidth * Notch / Order.Num() - 1.0, 7.0), FVector2D(2.0, 8.0), HCLook::Ink);
	}
	D.TextRight(FString::Printf(TEXT("%d %%"), FMath::RoundToInt(Shown * 100.f)), D.Size.X, 3.0, HCLook::Mono(11.f), bAdvancing ? HCLook::Signal : HCLook::BoneMuted);
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

// ---------------------------------------------------------------------------
// Sliders (valves and the two knobs of the signal)

void UHCTaskOverlayWidget::AddSliderRow(const FText& RowLabel, int32 Index, bool bShowTarget)
{
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	const FString Caption = HCLook::Upper(RowLabel);
	auto Move = [Weak, Index](const FVector2D& Local, const FVector2D& Size)
	{
		if (UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->SetSlider(Index, Local, Size);
		}
	};
	UHCPaintBox* Row = HCLook::Control(WidgetTree, FVector2D(0.0, 44.0),
		[Weak, Index, Caption, bShowTarget](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PaintSlider(D, S, Index, Caption, bShowTarget);
			}
		}, Move);
	Row->OnDragged = Move;
	HCUI::AddRow(ContentBox, Row, 6.f);
}

void UHCTaskOverlayWidget::SetSlider(int32 Index, const FVector2D& Local, const FVector2D& Size)
{
	const double TrackWidth = Size.X - HCTaskLook::TrackLeft - HCTaskLook::TrackRight;
	if (Values.IsValidIndex(Index) && TrackWidth > 1.0 && !bFinished)
	{
		// In steps of one percent, like the readout
		Values[Index] = FMath::RoundToFloat(FMath::Clamp(static_cast<float>((Local.X - HCTaskLook::TrackLeft) / TrackWidth), 0.f, 1.f) * 100.f) / 100.f;
	}
}

bool UHCTaskOverlayWidget::IsSliderGood(int32 Index) const
{
	return Values.IsValidIndex(Index) && Targets.IsValidIndex(Index) && FMath::Abs(Values[Index] - Targets[Index]) <= SliderTolerance();
}

float UHCTaskOverlayWidget::SliderTolerance() const
{
	const float Tolerance = Kind == EHCTaskOverlayKind::Curve ? HCTaskLook::CurveTolerance : HCTaskLook::DialTolerance;
	return IsHintActive() ? Tolerance * HCTaskLook::HintTolerance : Tolerance;
}

// ---------------------------------------------------------------------------
// The tip

bool UHCTaskOverlayWidget::IsHintActive() const
{
	const double Open = FPlatformTime::Seconds() - OpenedAt;
	if (HintAfterOverride >= 0.f)
	{
		return Open > HintAfterOverride;
	}
	return !bAutoSolveWanted && Open > Duration * 1.5 + HCTaskLook::HintGraceSeconds;
}

FText UHCTaskOverlayWidget::GetHintText() const
{
	switch (Kind)
	{
	case EHCTaskOverlayKind::Dials:
		return NSLOCTEXT("HC", "HintDials", "Tipp: Schieb jeden Regler zwischen seine beiden Marken, bis er grün wird. Die Zonen sind jetzt breiter.");
	case EHCTaskOverlayKind::Curve:
		return NSLOCTEXT("HC", "HintCurve", "Tipp: Erst die Frequenz angleichen (gleich viele Wellen), dann die Stärke (gleiche Höhe). Ungefähr reicht jetzt.");
	case EHCTaskOverlayKind::Temperature:
		return NSLOCTEXT("HC", "HintTemperature", "Tipp: Nur kurz kühlen und wieder loslassen. Die Temperatur steigt von selbst.");
	case EHCTaskOverlayKind::HoldPad:
		return NSLOCTEXT("HC", "HintHoldPad", "Tipp: Maustaste auf dem Feld oder die Leertaste gedrückt halten und nicht loslassen.");
	case EHCTaskOverlayKind::Transfer:
		return NSLOCTEXT("HC", "HintTransfer", "Tipp: Einmal starten und am Terminal stehen bleiben. Bewegen bricht ab.");
	case EHCTaskOverlayKind::Wires:
		return NSLOCTEXT("HC", "HintWires", "Tipp: Kabel anklicken, dann blinkt seine Buchse. Ein Fehler wirft jetzt nichts mehr raus.");
	case EHCTaskOverlayKind::Switches:
		return NSLOCTEXT("HC", "HintSwitches", "Tipp: Arbeite von links nach rechts. Falsche Schalter sind rot markiert.");
	case EHCTaskOverlayKind::Memory:
		return NSLOCTEXT("HC", "HintMemory", "Tipp: Das nächste Feld leuchtet jetzt schwach.");
	default:
		return NSLOCTEXT("HC", "HintTiles", "Tipp: Das nächste Feld blinkt.");
	}
}

void UHCTaskOverlayWidget::PaintSlider(const FHCDraw& D, const FHCPaintState& S, int32 Index, const FString& RowLabel, bool bShowTarget) const
{
	if (!Values.IsValidIndex(Index) || !Targets.IsValidIndex(Index))
	{
		return;
	}
	const double Mid = D.Size.Y * 0.5;
	const double X0 = HCTaskLook::TrackLeft;
	const double TrackWidth = D.Size.X - HCTaskLook::TrackLeft - HCTaskLook::TrackRight;
	const float Value = Values[Index];
	const bool bGood = IsSliderGood(Index);
	const bool bHot = S.bHovered || S.bPressed;
	const FLinearColor Lit = bGood ? HCLook::Signal : HCLook::Bone;

	const FSlateFontInfo LabelFont = HCLook::Caps(11.f, 200);
	D.Text(RowLabel, FVector2D(0.0, Mid - FHCDraw::Measure(RowLabel, LabelFont).Y * 0.5), LabelFont, bHot ? HCLook::Bone : HCLook::BoneMuted);

	// Track with a scale under it
	D.Box(FVector2D(X0, Mid - 1.0), FVector2D(TrackWidth, 2.0), HCLook::Alpha(HCLook::Bone, 0.18f));
	for (int32 Tick = 0; Tick <= 10; ++Tick)
	{
		D.Box(FVector2D(X0 + TrackWidth * Tick / 10.0 - 0.5, Mid + 8.0), FVector2D(1.0, Tick % 5 == 0 ? 6.0 : 3.0), HCLook::Alpha(HCLook::Bone, 0.28f));
	}
	if (bShowTarget)
	{
		// The zone to hit: brackets that turn green once the handle sits inside
		const float Tolerance = SliderTolerance();
		const double ZoneX = X0 + TrackWidth * (Targets[Index] - Tolerance);
		const double ZoneWidth = TrackWidth * 2.0 * Tolerance;
		D.Box(FVector2D(ZoneX, Mid - 10.0), FVector2D(ZoneWidth, 20.0), HCLook::Alpha(Lit, bGood ? 0.22f : 0.1f));
		D.Box(FVector2D(ZoneX, Mid - 10.0), FVector2D(2.0, 20.0), HCLook::Alpha(Lit, 0.9f));
		D.Box(FVector2D(ZoneX + ZoneWidth - 2.0, Mid - 10.0), FVector2D(2.0, 20.0), HCLook::Alpha(Lit, 0.9f));
	}
	D.Box(FVector2D(X0, Mid - 1.0), FVector2D(TrackWidth * Value, 2.0), HCLook::Alpha(Lit, 0.7f));
	const double HandleWidth = bHot ? 6.0 : 4.0;
	D.Box(FVector2D(X0 + TrackWidth * Value - HandleWidth * 0.5, Mid - 14.0), FVector2D(HandleWidth, 28.0), Lit);

	// Readout on the right
	if (bShowTarget)
	{
		const FSlateFontInfo ValueFont = HCLook::Mono(13.f);
		const FString ValueText = FString::Printf(TEXT("%d %%"), FMath::RoundToInt(Value * 100.f));
		D.TextRight(ValueText, D.Size.X, Mid - FHCDraw::Measure(ValueText, ValueFont).Y * 0.5, ValueFont, Lit);
	}
	else
	{
		const FSlateFontInfo ValueFont = HCLook::Caps(10.f, 200);
		const FString ValueText = HCLook::Upper(bGood ? NSLOCTEXT("HC", "CurveGood", "passt") : NSLOCTEXT("HC", "CurveBad", "nachstellen"));
		D.TextRight(ValueText, D.Size.X, Mid - FHCDraw::Measure(ValueText, ValueFont).Y * 0.5, ValueFont, bGood ? HCLook::Signal : HCLook::BoneMuted);
	}
}

// ---------------------------------------------------------------------------
// Dials

void UHCTaskOverlayWidget::BuildDials()
{
	for (int32 i = 0; i < 3; ++i)
	{
		const float Start = FMath::FRandRange(0.1f, 0.9f);
		Values.Add(Start);
		Targets.Add(RollTarget(Start));
		AddSliderRow(FText::Format(NSLOCTEXT("HC", "ValveN", "Ventil {0}"), i + 1), i, true);
	}
}

bool UHCTaskOverlayWidget::TickDials()
{
	// The pressure shifts three times: one valve gets a new target
	if (ShiftsDone < 3 && Progress >= 0.25f * (ShiftsDone + 1))
	{
		const int32 Index = FMath::RandRange(0, Targets.Num() - 1);
		Targets[Index] = RollTarget(Targets[Index]);
		++ShiftsDone;
	}
	int32 InZone = 0;
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		if (bAutoSolve)
		{
			Values[i] = Targets[i];
		}
		InZone += IsSliderGood(i) ? 1 : 0;
	}
	const bool bAll = InZone == Values.Num();
	StatusText->SetText(bAll ? NSLOCTEXT("HC", "DialsGood", "Druck stabilisiert sich ...") : NSLOCTEXT("HC", "DialsBad", "Ventile nachstellen"));
	StatusText->SetColorAndOpacity(FSlateColor(bAll ? HCLook::Signal : HCLook::Bone));
	return bAll;
}

// ---------------------------------------------------------------------------
// Signal curve

void UHCTaskOverlayWidget::BuildCurve()
{
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	HCUI::AddRow(ContentBox, HCLook::Paint(WidgetTree, FVector2D(0.0, 150.0), [Weak](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->PaintScope(D);
		}
	}), 12.f);

	const FText Labels[2] = { NSLOCTEXT("HC", "CurveFreq", "Frequenz"), NSLOCTEXT("HC", "CurveAmp", "Stärke") };
	for (int32 i = 0; i < 2; ++i)
	{
		const float Start = FMath::FRandRange(0.1f, 0.9f);
		Values.Add(Start);
		Targets.Add(RollTarget(Start));
		AddSliderRow(Labels[i], i, false);
	}
}

void UHCTaskOverlayWidget::PaintScope(const FHCDraw& D) const
{
	if (Values.Num() < 2 || Targets.Num() < 2)
	{
		return;
	}
	const FVector2D Size = D.Size;
	D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Ink, 0.85f));
	// Graticule of an oscilloscope
	for (int32 Column = 1; Column < 8; ++Column)
	{
		D.Box(FVector2D(Size.X * Column / 8.0, 0.0), FVector2D(1.0, Size.Y), HCLook::Alpha(HCLook::Bone, Column == 4 ? 0.12f : 0.05f));
	}
	for (int32 Row = 1; Row < 4; ++Row)
	{
		D.Box(FVector2D(0.0, Size.Y * Row / 4.0), FVector2D(Size.X, 1.0), HCLook::Alpha(HCLook::Bone, Row == 2 ? 0.12f : 0.05f));
	}
	D.Frame(FVector2D::ZeroVector, Size, 1.f, HCLook::Alpha(HCLook::BoneMuted, 0.35f));

	auto MakeCurve = [&Size](float FrequencySlider, float AmplitudeSlider)
	{
		constexpr int32 Segments = 96;
		TArray<FVector2D> Points;
		Points.Reserve(Segments + 1);
		const float Frequency = HCTaskLook::CurveFrequency(FrequencySlider);
		const float Amplitude = HCTaskLook::CurveAmplitude(AmplitudeSlider);
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float T = static_cast<float>(i) / Segments;
			Points.Add(FVector2D(8.0 + T * (Size.X - 16.0), Size.Y * 0.5 - FMath::Sin(T * 2.f * PI * Frequency) * Amplitude * Size.Y * 0.42));
		}
		return Points;
	};
	// The signal dark and broad, the own curve bright and thin; green once they lie on each other
	const bool bLocked = IsSliderGood(0) && IsSliderGood(1);
	D.Lines(MakeCurve(Targets[0], Targets[1]), HCLook::Alpha(HCLook::Bone, 0.26f), 5.f);
	D.Lines(MakeCurve(Values[0], Values[1]), bLocked ? HCLook::Signal : HCLook::Bone, 2.f);
}

bool UHCTaskOverlayWidget::TickCurve()
{
	// The signal drifts twice
	if (ShiftsDone < 2 && Progress >= (ShiftsDone == 0 ? 0.4f : 0.75f))
	{
		Targets[0] = RollTarget(Targets[0]);
		Targets[1] = RollTarget(Targets[1]);
		++ShiftsDone;
	}
	bool bAll = true;
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		if (bAutoSolve)
		{
			Values[i] = Targets[i];
		}
		bAll &= IsSliderGood(i);
	}
	StatusText->SetText(bAll ? NSLOCTEXT("HC", "CurveLocked", "Signal erfasst ...") : NSLOCTEXT("HC", "CurveSearch", "Signal suchen"));
	StatusText->SetColorAndOpacity(FSlateColor(bAll ? HCLook::Signal : HCLook::Bone));
	return bAll;
}

// ---------------------------------------------------------------------------
// Temperature

void UHCTaskOverlayWidget::BuildTemperature()
{
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	Temperature = 0.85f;
	HCUI::AddRow(ContentBox, HCLook::Paint(WidgetTree, FVector2D(0.0, 80.0), [Weak](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->PaintGauge(D);
		}
	}), 12.f);

	CoolButton = HCLook::Control(WidgetTree, FVector2D(0.0, 50.0),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				// The space bar holds the button down just like the mouse
				FHCPaintState Held = S;
				Held.bPressed |= Widget->bCoolKeyDown;
				HCLook::DrawButton(D, Held, HCLook::Upper(NSLOCTEXT("HC", "Cool", "KÜHLEN (gedrückt halten)")), HCLook::GhostBlue, true, 12.f);
			}
		},
		[](const FVector2D&, const FVector2D&) {});
	HCUI::AddRow(ContentBox, CoolButton, 0.f);
}

void UHCTaskOverlayWidget::PaintGauge(const FHCDraw& D) const
{
	const bool bGood = Temperature >= HCTaskLook::TemperatureLow && Temperature <= HCTaskLook::TemperatureHigh;
	const FLinearColor State = bGood ? HCLook::Signal : Temperature > HCTaskLook::TemperatureHigh ? HCLook::Blood : HCLook::GhostBlue;
	const double Width = D.Size.X;
	D.Text(HCLook::Upper(FText::Format(NSLOCTEXT("HC", "TempValue", "Temperatur {0} %"), FMath::RoundToInt(Temperature * 100.f))), FVector2D(0.0, 0.0), HCLook::Caps(14.f, 180), State);

	const double Y = 36.0;
	const double Height = 16.0;
	D.Box(FVector2D(0.0, Y), FVector2D(Width, Height), HCLook::Alpha(HCLook::Bone, 0.08f));
	D.Frame(FVector2D(0.0, Y), FVector2D(Width, Height), 1.f, HCLook::Alpha(HCLook::BoneMuted, 0.35f));
	// The green zone, with its limits as numbers
	const double BandX = Width * HCTaskLook::TemperatureLow;
	const double BandWidth = Width * (HCTaskLook::TemperatureHigh - HCTaskLook::TemperatureLow);
	D.Box(FVector2D(BandX, Y - 5.0), FVector2D(BandWidth, Height + 10.0), HCLook::Alpha(HCLook::Signal, bGood ? 0.26f : 0.12f));
	D.Box(FVector2D(BandX, Y - 5.0), FVector2D(2.0, Height + 10.0), HCLook::Alpha(HCLook::Signal, 0.9f));
	D.Box(FVector2D(BandX + BandWidth - 2.0, Y - 5.0), FVector2D(2.0, Height + 10.0), HCLook::Alpha(HCLook::Signal, 0.9f));
	const FSlateFontInfo LimitFont = HCLook::Mono(10.f);
	D.TextCentered(FString::FromInt(FMath::RoundToInt(HCTaskLook::TemperatureLow * 100.f)), BandX, Y + Height + 8.0, LimitFont, HCLook::BoneMuted);
	D.TextCentered(FString::FromInt(FMath::RoundToInt(HCTaskLook::TemperatureHigh * 100.f)), BandX + BandWidth, Y + Height + 8.0, LimitFont, HCLook::BoneMuted);
	// The column and its needle
	D.Box(FVector2D(0.0, Y + 4.0), FVector2D(Width * Temperature, Height - 8.0), HCLook::Alpha(State, 0.5f));
	D.Box(FVector2D(Width * Temperature - 2.0, Y - 9.0), FVector2D(4.0, Height + 18.0), State);
}

bool UHCTaskOverlayWidget::TickTemperature(float DeltaTime)
{
	const bool bCooling = bCoolKeyDown || (CoolButton && CoolButton->IsBoxPressed());
	Temperature = FMath::Clamp(Temperature + (bCooling ? -0.38f : 0.2f) * DeltaTime, 0.f, 1.f);
	if (bAutoSolve)
	{
		Temperature = (HCTaskLook::TemperatureLow + HCTaskLook::TemperatureHigh) * 0.5f;
	}
	const bool bGood = Temperature >= HCTaskLook::TemperatureLow && Temperature <= HCTaskLook::TemperatureHigh;
	const bool bHot = Temperature > HCTaskLook::TemperatureHigh;
	StatusText->SetText(bGood ? NSLOCTEXT("HC", "TempGood", "Kühlung stabil ...")
		: bHot ? NSLOCTEXT("HC", "TempHot", "Zu warm – kühlen") : NSLOCTEXT("HC", "TempCold", "Zu kalt – loslassen"));
	StatusText->SetColorAndOpacity(FSlateColor(bGood ? HCLook::Signal : bHot ? HCLook::Blood : HCLook::GhostBlue));
	return bGood;
}

// ---------------------------------------------------------------------------
// Tile menus: crates, numbers, filter. One shared rule: the right tile starts a segment that takes its
// share of the step duration; a wrong one starts the whole thing over.

void UHCTaskOverlayWidget::BuildButtonGrid(const TArray<FText>& Labels, int32 Columns, bool bShuffleOrder, bool bShufflePositions, float FontSize, float TileHeight)
{
	UWidgetTree* Tree = WidgetTree;
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	const int32 Count = Labels.Num();
	ButtonLabels = Labels;
	TArray<int32> Positions;
	for (int32 i = 0; i < Count; ++i)
	{
		Order.Add(i);
		Positions.Add(i);
	}
	auto Shuffle = [](TArray<int32>& Items)
	{
		for (int32 i = Items.Num() - 1; i > 0; --i)
		{
			Items.Swap(i, FMath::RandRange(0, i));
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
	UHorizontalBox* Row = nullptr;
	const int32 Cells = FMath::DivideAndRoundUp(Count, Columns) * Columns;
	for (int32 Cell = 0; Cell < Cells; ++Cell)
	{
		if (Cell % Columns == 0)
		{
			Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			HCUI::AddRow(ContentBox, Row, Cell + Columns < Cells ? 8.f : 0.f);
		}
		const float Gap = (Cell % Columns) + 1 < Columns ? 8.f : 0.f;
		if (Cell >= Count)
		{
			// Empty cell, so the tiles of a short last row keep their width
			HCUI::AddColumn(Row, HCLook::Paint(Tree, FVector2D(0.0, TileHeight), nullptr), Gap, true);
			continue;
		}
		const int32 Index = Positions[Cell];
		UHCPaintBox* Tile = HCLook::Control(Tree, FVector2D(0.0, TileHeight),
			[Weak, Index, FontSize](const FHCDraw& D, const FHCPaintState& S)
			{
				if (const UHCTaskOverlayWidget* Widget = Weak.Get())
				{
					Widget->PaintTile(D, S, Index, FontSize);
				}
			},
			[Weak, Index](const FVector2D&, const FVector2D&)
			{
				if (UHCTaskOverlayWidget* Widget = Weak.Get())
				{
					Widget->PressButton(Index);
				}
			});
		Tile->WantsHand = [Weak, Index]()
		{
			const UHCTaskOverlayWidget* Widget = Weak.Get();
			return Widget && Widget->SegmentProgress < 0.f && !Widget->IsTileDone(Index);
		};
		HCUI::AddColumn(Row, Tile, Gap, true);
	}
}

bool UHCTaskOverlayWidget::IsTileDone(int32 Index) const
{
	const int32 Position = Order.IndexOfByKey(Index);
	return Position != INDEX_NONE && Position < OrderIndex;
}

void UHCTaskOverlayWidget::PaintTile(const FHCDraw& D, const FHCPaintState& S, int32 Index, float FontSize) const
{
	if (!ButtonLabels.IsValidIndex(Index))
	{
		return;
	}
	const bool bDone = IsTileDone(Index);
	const bool bRunning = SegmentProgress >= 0.f && Order.IsValidIndex(OrderIndex) && Order[OrderIndex] == Index;
	const bool bHot = !bDone && !bFinished && SegmentProgress < 0.f && (S.bHovered || S.bPressed);
	const float Wrong = Index == WrongIndex ? 1.f - FMath::Clamp(static_cast<float>((S.Time - WrongAt) / 0.6), 0.f, 1.f) : 0.f;
	const FVector2D Size = D.Size;

	D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Plate, bDone ? 0.5f : 0.95f));
	if (bRunning)
	{
		// The tile fills while it is read
		D.Box(FVector2D::ZeroVector, FVector2D(Size.X * FMath::Clamp(SegmentProgress, 0.f, 1.f), Size.Y), HCLook::Alpha(HCLook::Signal, 0.3f));
	}
	if (Wrong > 0.f)
	{
		D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Blood, 0.5f * Wrong));
	}
	else if (bHot)
	{
		D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Bone, 0.09f));
	}
	// The tip: the tile that is next blinks
	const bool bTip = !bFinished && SegmentProgress < 0.f && Order.IsValidIndex(OrderIndex) && Order[OrderIndex] == Index && IsHintActive();
	const float Blink = 0.45f + 0.55f * FMath::Abs(FMath::Sin(static_cast<float>(S.Time) * 4.f));
	const FLinearColor Edge = Wrong > 0.f ? HCLook::Blood : bRunning ? HCLook::Signal : bDone ? HCLook::Alpha(HCLook::Signal, 0.35f)
		: bTip ? HCLook::Alpha(HCTaskLook::Amber(), Blink) : bHot ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, 0.5f);
	D.Frame(FVector2D::ZeroVector, Size, bTip ? 2.f : 1.f, Edge);
	D.Box(FVector2D(0.0, Size.Y - 2.0), FVector2D(Size.X, 2.0), Edge);

	const FSlateFontInfo Font = HCLook::Caps(FontSize, FontSize > 20.f ? 0 : 160);
	const FString Label = FHCDraw::Fit(HCLook::Upper(ButtonLabels[Index]), Font, Size.X - 16.0);
	D.TextCentered(Label, Size.X * 0.5, (Size.Y - FHCDraw::Measure(Label, Font).Y) * 0.5, Font,
		bDone ? HCLook::Alpha(HCLook::BoneMuted, 0.6f) : bRunning ? HCLook::Signal : HCLook::Bone);
	if (bDone)
	{
		D.Check(FVector2D(Size.X - 15.0, 14.0), 5.f, HCLook::Signal, 2.f);
	}
}

void UHCTaskOverlayWidget::PaintOrder(const FHCDraw& D) const
{
	// The order to scan in, as a row of chips: done ones are ticked off, the next one is framed
	const double Mid = D.Size.Y * 0.5;
	const FSlateFontInfo LabelFont = HCLook::Caps(11.f, 220);
	const FString Label = HCLook::Upper(NSLOCTEXT("HC", "OrderLabel", "Reihenfolge"));
	const FVector2D LabelSize = D.Text(Label, FVector2D(0.0, Mid - FHCDraw::Measure(Label, LabelFont).Y * 0.5), LabelFont, HCLook::BoneMuted);
	const FSlateFontInfo LetterFont = HCLook::Caps(16.f, 0);
	double X = LabelSize.X + 18.0;
	for (int32 Position = 0; Position < Order.Num(); ++Position)
	{
		const bool bDone = Position < OrderIndex;
		const bool bNext = Position == OrderIndex;
		const FVector2D Chip(X, Mid - 16.0);
		const FVector2D ChipSize(36.0, 32.0);
		D.Box(Chip, ChipSize, HCLook::Alpha(HCLook::Plate, 0.95f));
		D.Frame(Chip, ChipSize, bNext ? 2.f : 1.f, bDone ? HCLook::Alpha(HCLook::Signal, 0.45f) : bNext ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, 0.4f));
		const FString Letter = FString::Chr(static_cast<TCHAR>(TEXT('A') + Order[Position]));
		D.TextCentered(Letter, X + ChipSize.X * 0.5, Chip.Y + (ChipSize.Y - FHCDraw::Measure(Letter, LetterFont).Y) * 0.5, LetterFont,
			bDone ? HCLook::Alpha(HCLook::Signal, 0.7f) : bNext ? HCLook::Bone : HCLook::BoneMuted);
		X += ChipSize.X;
		if (Position + 1 < Order.Num())
		{
			D.Lines({ FVector2D(X + 8.0, Mid - 5.0), FVector2D(X + 13.0, Mid), FVector2D(X + 8.0, Mid + 5.0) }, HCLook::Alpha(HCLook::BoneMuted, 0.7f), 1.5f);
			X += 22.0;
		}
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
	// The chips read the order while they are painted, so they can stand above the grid that shuffles it
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	HCUI::AddRow(ContentBox, HCLook::Paint(WidgetTree, FVector2D(0.0, 36.0), [Weak](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->PaintOrder(D);
		}
	}), 14.f);
	BuildButtonGrid(Labels, Count, true, false, 15.f, 60.f);
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
	BuildButtonGrid(Labels, Count > 5 ? (Count + 1) / 2 : Count, false, true, 26.f, 72.f);
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
	BuildButtonGrid(Labels, 2, false, true, 12.f, 60.f);
	StatusText->SetText(NSLOCTEXT("HC", "FilterStart", "Beginne mit Schritt 1"));
}

void UHCTaskOverlayWidget::PressButton(int32 Index)
{
	if (bFinished || SegmentProgress >= 0.f || !Order.IsValidIndex(OrderIndex) || IsTileDone(Index))
	{
		return; // a tile is being read, or this one is through already
	}
	HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.45f);
	if (Order[OrderIndex] == Index)
	{
		SegmentProgress = 0.f;
		return;
	}
	// Wrong one: start over
	OrderIndex = 0;
	Progress = 0.f;
	WrongIndex = Index;
	WrongAt = FPlatformTime::Seconds();
	StatusText->SetText(WrongText);
	StatusText->SetColorAndOpacity(FSlateColor(HCLook::Blood));
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
		StatusText->SetColorAndOpacity(FSlateColor(HCLook::Signal));
		if (SegmentProgress >= 1.f)
		{
			++OrderIndex;
			SegmentProgress = -1.f;
			if (OrderIndex < Count)
			{
				StatusText->SetText(NextText);
				StatusText->SetColorAndOpacity(FSlateColor(HCLook::Bone));
			}
		}
	}
	Progress = (OrderIndex + FMath::Clamp(SegmentProgress, 0.f, 1.f)) / FMath::Max(1, Count);
}

// ---------------------------------------------------------------------------
// Hold pad and transfer

void UHCTaskOverlayWidget::BuildHoldPad()
{
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	HoldPad = HCLook::Control(WidgetTree, FVector2D(0.0, 156.0),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PaintHoldPad(D, S);
			}
		},
		[](const FVector2D&, const FVector2D&) {});
	HCUI::AddRow(ContentBox, HoldPad, 0.f);
}

void UHCTaskOverlayWidget::PaintHoldPad(const FHCDraw& D, const FHCPaintState& S) const
{
	const bool bHolding = bAutoSolve || bCoolKeyDown || S.bPressed;
	const bool bHot = bHolding || S.bHovered;
	const FVector2D Size = D.Size;
	const float Shown = FMath::Clamp(Progress, 0.f, 1.f);
	D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Plate, 0.95f));
	if (bHolding)
	{
		D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Signal, 0.08f));
	}
	const FLinearColor Edge = bHolding ? HCLook::Signal : bHot ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, 0.5f);
	D.Frame(FVector2D::ZeroVector, Size, 1.f, Edge);
	D.Box(FVector2D(0.0, Size.Y - 2.0), FVector2D(Size.X, 2.0), Edge);

	// A ring that closes while the field is held, around the lines of a palm scanner
	const FVector2D Center(Size.X * 0.5, 62.0);
	const float Turn = static_cast<float>(FMath::Fmod(S.Time * 0.35, 1.0));
	for (int32 Ring = 0; Ring < 3; ++Ring)
	{
		const float Radius = 12.f + 9.f * Ring;
		const float Start = bHolding ? Turn * (Ring % 2 == 0 ? 1.f : -1.f) + 0.2f * Ring : 0.15f * Ring;
		D.Arc(Center, Radius, Start, Start + 0.62f, HCLook::Alpha(bHolding ? HCLook::Signal : HCLook::Bone, bHolding ? 0.55f : 0.22f), 2.f);
	}
	D.Arc(Center, 44.f, 0.f, 1.f, HCLook::Alpha(HCLook::Bone, 0.14f), 3.f);
	D.Arc(Center, 44.f, 0.f, Shown, bHolding ? HCLook::Signal : HCLook::Alpha(HCLook::Bone, 0.8f), 4.f);

	const FSlateFontInfo Font = HCLook::Caps(12.f, 220);
	D.TextCentered(HCLook::Upper(NSLOCTEXT("HC", "HoldPad", "HIER GEDRÜCKT HALTEN")), Size.X * 0.5, 122.0, Font, bHot ? HCLook::Bone : HCLook::BoneMuted);
}

void UHCTaskOverlayWidget::TickHoldPad(float DeltaTime)
{
	// Mouse on the field or the space bar; letting go only pauses, nothing is lost
	const bool bHolding = bAutoSolve || bCoolKeyDown || (HoldPad && HoldPad->IsBoxPressed());
	if (bHolding)
	{
		Progress += DeltaTime / Duration;
	}
	StatusText->SetText(bHolding ? FText::Format(NSLOCTEXT("HC", "HoldRunning", "Vorgang läuft ... {0} %"), FMath::Clamp(FMath::RoundToInt(Progress * 100.f), 0, 100))
		: Progress > 0.f ? NSLOCTEXT("HC", "HoldPaused", "Unterbrochen – weiter gedrückt halten") : NSLOCTEXT("HC", "HoldStart", "Feld gedrückt halten"));
	StatusText->SetColorAndOpacity(FSlateColor(bHolding ? HCLook::Signal : Progress > 0.f ? HCLook::Blood : HCLook::Bone));
}

void UHCTaskOverlayWidget::BuildTransfer()
{
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	UHCPaintBox* Pad = HCLook::Control(WidgetTree, FVector2D(0.0, 64.0),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PaintTransfer(D, S);
			}
		},
		[Weak](const FVector2D&, const FVector2D&)
		{
			if (UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->HandleStart();
			}
		});
	Pad->WantsHand = [Weak]()
	{
		const UHCTaskOverlayWidget* Widget = Weak.Get();
		return Widget && !Widget->bTransferRunning;
	};
	HCUI::AddRow(ContentBox, Pad, 0.f);
	StatusText->SetText(NSLOCTEXT("HC", "TransferIdle", "Bereit zur Übertragung"));
}

void UHCTaskOverlayWidget::PaintTransfer(const FHCDraw& D, const FHCPaintState& S) const
{
	if (!bTransferRunning)
	{
		HCLook::DrawButton(D, S, HCLook::Upper(NSLOCTEXT("HC", "TransferStart", "ÜBERTRAGUNG STARTEN")), HCLook::Bone, true, 13.f);
		return;
	}
	// One block per packet; the one on its way blinks
	const int32 Packets = FMath::Clamp(FMath::RoundToInt(Duration), 1, 24);
	const float Shown = FMath::Clamp(Progress, 0.f, 1.f);
	const int32 Sent = FMath::FloorToInt(Shown * Packets);
	const double Gap = 5.0;
	const double BlockWidth = (D.Size.X - Gap * (Packets - 1)) / Packets;
	const float Blink = 0.45f + 0.4f * FMath::Abs(FMath::Sin(static_cast<float>(S.Time) * 6.f));
	for (int32 Packet = 0; Packet < Packets; ++Packet)
	{
		const FVector2D Pos((BlockWidth + Gap) * Packet, 12.0);
		const FVector2D BlockSize(BlockWidth, D.Size.Y - 24.0);
		D.Box(Pos, BlockSize, Packet < Sent ? HCLook::Alpha(HCLook::Signal, 0.85f) : Packet == Sent ? HCLook::Alpha(HCLook::Signal, Blink) : HCLook::Alpha(HCLook::Bone, 0.1f));
		D.Frame(Pos, BlockSize, 1.f, HCLook::Alpha(Packet <= Sent ? HCLook::Signal : HCLook::BoneMuted, 0.5f));
	}
}

void UHCTaskOverlayWidget::HandleStart()
{
	if (!bTransferRunning && !bFinished)
	{
		bTransferRunning = true;
		HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.45f);
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
		StatusText->SetColorAndOpacity(FSlateColor(HCLook::Signal));
	}
}

// ---------------------------------------------------------------------------
// Cables: drag every coloured cable from its plug on the left to the socket of its colour on the right

void UHCTaskOverlayWidget::BuildWires(int32 Count)
{
	auto Shuffle = [](TArray<int32>& Items)
	{
		for (int32 i = Items.Num() - 1; i > 0; --i)
		{
			Items.Swap(i, FMath::RandRange(0, i));
		}
	};
	for (int32 Wire = 0; Wire < Count; ++Wire)
	{
		WirePlugs.Add(Wire);
		WireSockets.Add(Wire);
		WireConnected.Add(false);
	}
	Shuffle(WirePlugs);
	// The sockets never mirror the plugs: at least the lines have to cross
	for (int32 Try = 0; Try < 8 && WireSockets == WirePlugs; ++Try)
	{
		Shuffle(WireSockets);
	}

	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	UHCPaintBox* Panel = HCLook::Control(WidgetTree, FVector2D(0.0, HCTaskLook::WireRowHeight * Count),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PaintWires(D, S);
			}
		},
		[Weak](const FVector2D& Local, const FVector2D& Size)
		{
			if (UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PressWires(Local, Size);
			}
		});
	Panel->OnDragged = [Weak](const FVector2D& Local, const FVector2D& Size)
	{
		if (UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->WireDragPos = Local;
			Widget->WirePanelSize = Size;
		}
	};
	Panel->OnReleased = [Weak](bool)
	{
		if (UHCTaskOverlayWidget* Widget = Weak.Get())
		{
			Widget->ReleaseWires();
		}
	};
	HCUI::AddRow(ContentBox, Panel, 0.f);
}

void UHCTaskOverlayWidget::PressWires(const FVector2D& Local, const FVector2D& Size)
{
	WireDragPos = Local;
	WirePanelSize = Size;
	const int32 Row = FMath::FloorToInt32(Local.Y / HCTaskLook::WireRowHeight);
	if (bFinished || !WirePlugs.IsValidIndex(Row))
	{
		return;
	}
	if (Local.X <= HCTaskLook::WireCapWidth + HCTaskLook::WireReach)
	{
		// A plug on the left: take its cable in the hand (drag it, or click the socket next)
		const int32 Wire = WirePlugs[Row];
		if (!WireConnected[Wire])
		{
			WireSelected = Wire;
			bWireDragging = true;
			HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.35f);
		}
	}
	else if (Local.X >= Size.X - HCTaskLook::WireCapWidth - HCTaskLook::WireReach)
	{
		PlugWire(Row);
	}
}

void UHCTaskOverlayWidget::ReleaseWires()
{
	if (!bWireDragging)
	{
		return;
	}
	bWireDragging = false;
	// Let go over a socket: plug it in. Let go anywhere else: the cable stays in the hand for a click.
	const int32 Row = FMath::FloorToInt32(WireDragPos.Y / HCTaskLook::WireRowHeight);
	if (WireDragPos.X >= WirePanelSize.X - HCTaskLook::WireCapWidth - HCTaskLook::WireReach && WireSockets.IsValidIndex(Row))
	{
		PlugWire(Row);
	}
}

void UHCTaskOverlayWidget::PlugWire(int32 SocketRow)
{
	if (bFinished || WireSelected == INDEX_NONE || !WireSockets.IsValidIndex(SocketRow))
	{
		return;
	}
	const int32 Wire = WireSelected;
	WireSelected = INDEX_NONE;
	bWireDragging = false;
	HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.5f);
	if (WireSockets[SocketRow] == Wire)
	{
		WireConnected[Wire] = true;
		return;
	}
	// Wrong socket: a short circuit pulls every cable out again (not once the tip is on)
	WrongIndex = SocketRow;
	WrongAt = FPlatformTime::Seconds();
	if (!IsHintActive())
	{
		for (bool& bConnected : WireConnected)
		{
			bConnected = false;
		}
	}
}

bool UHCTaskOverlayWidget::TickWires()
{
	int32 Done = 0;
	for (bool& bConnected : WireConnected)
	{
		bConnected |= bAutoSolve;
		Done += bConnected ? 1 : 0;
	}
	const bool bAll = Done == WireConnected.Num();
	const bool bShort = WrongIndex != INDEX_NONE && FPlatformTime::Seconds() - WrongAt < 1.4;
	// No "ß" in a status line: the label shows capitals, and "ß" -> "SS" changes the length (ensure in Slate's text layout)
	StatusText->SetText(bAll ? NSLOCTEXT("HC", "WiresGood", "Strom läuft ...")
		: bShort ? (Done > 0 ? NSLOCTEXT("HC", "WiresWrong", "Falsche Buchse") : NSLOCTEXT("HC", "WiresShort", "Kurzschluss – alle Kabel neu stecken"))
		: WireSelected != INDEX_NONE ? NSLOCTEXT("HC", "WiresPick", "Buchse mit derselben Farbe wählen")
		: FText::Format(NSLOCTEXT("HC", "WiresCountFmt", "{0} von {1} Kabeln verbunden"), Done, WireConnected.Num()));
	StatusText->SetColorAndOpacity(FSlateColor(bAll ? HCLook::Signal : bShort ? HCLook::Blood : HCLook::Bone));
	return bAll;
}

void UHCTaskOverlayWidget::PaintWires(const FHCDraw& D, const FHCPaintState& S) const
{
	const int32 Count = WirePlugs.Num();
	const FVector2D Size = D.Size;
	const double RowHeight = HCTaskLook::WireRowHeight;
	const double Cap = HCTaskLook::WireCapWidth;
	auto RowY = [RowHeight](int32 Row) { return (Row + 0.5) * RowHeight; };
	const bool bMouse = (S.bHovered || S.bPressed) && !bFinished;
	const int32 MouseRow = bMouse ? FMath::FloorToInt32(S.Mouse.Y / RowHeight) : INDEX_NONE;
	const bool bMouseLeft = bMouse && S.Mouse.X <= Cap + HCTaskLook::WireReach;
	const bool bMouseRight = bMouse && S.Mouse.X >= Size.X - Cap - HCTaskLook::WireReach;
	const bool bTip = IsHintActive();
	const float Blink = 0.4f + 0.6f * FMath::Abs(FMath::Sin(static_cast<float>(S.Time) * 4.f));

	// The open panel: dark, with the cable duct behind
	D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Ink, 0.85f));
	D.Box(FVector2D(Size.X * 0.5 - 30.0, 0.0), FVector2D(60.0, Size.Y), HCLook::Alpha(HCLook::Bone, 0.04f));
	D.Frame(FVector2D::ZeroVector, Size, 1.f, HCLook::Alpha(HCLook::BoneMuted, 0.35f));

	// The cables that are plugged in, and the one in the hand
	for (int32 LeftRow = 0; LeftRow < Count; ++LeftRow)
	{
		const int32 Wire = WirePlugs[LeftRow];
		const FLinearColor Color = HCTaskLook::WireColor(Wire);
		const FVector2D From(Cap, RowY(LeftRow));
		if (WireConnected[Wire])
		{
			const double ToY = RowY(WireSockets.IndexOfByKey(Wire));
			D.Lines({ From, From + FVector2D(26.0, 0.0), FVector2D(Size.X - Cap - 26.0, ToY), FVector2D(Size.X - Cap, ToY) }, Color, 10.f);
		}
		else if (Wire == WireSelected && (bWireDragging || bMouse))
		{
			D.Lines({ From, From + FVector2D(18.0, 0.0), bWireDragging ? WireDragPos : S.Mouse }, Color, 10.f);
		}
		else
		{
			// Loose end
			D.Lines({ From, From + FVector2D(34.0, 0.0), From + FVector2D(48.0, 10.0) }, HCLook::Alpha(Color, 0.85f), 10.f);
		}
	}

	// Plugs and sockets: a cap in the colour with the cable's symbol (for everybody who cannot tell the colours apart)
	auto DrawCap = [&D, &RowY, Cap](double X, int32 Row, int32 Wire, bool bLit, bool bHot, float Wrong, float Tip)
	{
		const FLinearColor Color = HCTaskLook::WireColor(Wire);
		const FVector2D Pos(X, RowY(Row) - 17.0);
		const FVector2D CapSize(Cap, 34.0);
		D.Box(Pos, CapSize, HCLook::Alpha(HCLook::Plate, 0.98f));
		D.Box(Pos + FVector2D(0.0, 9.0), FVector2D(Cap, 16.0), HCLook::Alpha(Color, bLit ? 1.f : 0.75f));
		if (Wrong > 0.f)
		{
			D.Box(Pos, CapSize, HCLook::Alpha(HCLook::Blood, 0.6f * Wrong));
		}
		D.Frame(Pos, CapSize, bHot || Tip > 0.f ? 2.f : 1.f, Tip > 0.f ? HCLook::Alpha(HCTaskLook::Amber(), Tip) : bHot ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, 0.6f));
		D.Box(Pos + FVector2D(Cap * 0.5 - 11.0, 6.0), FVector2D(22.0, 22.0), HCLook::Alpha(HCLook::Ink, 0.9f));
		D.Symbol(Wire, Pos + FVector2D(Cap * 0.5, 17.0), 7.f, Color, 2.f);
	};
	for (int32 Row = 0; Row < Count; ++Row)
	{
		const int32 Plug = WirePlugs[Row];
		DrawCap(0.0, Row, Plug, WireConnected[Plug] || Plug == WireSelected, bMouseLeft && MouseRow == Row && !WireConnected[Plug], 0.f, 0.f);
		const int32 Socket = WireSockets[Row];
		const float Wrong = Row == WrongIndex ? 1.f - FMath::Clamp(static_cast<float>((S.Time - WrongAt) / 0.7), 0.f, 1.f) : 0.f;
		const bool bWanted = bTip && WireSelected == Socket;
		DrawCap(Size.X - Cap, Row, Socket, WireConnected[Socket], WireSelected != INDEX_NONE && bMouseRight && MouseRow == Row, Wrong, bWanted ? Blink : 0.f);
	}
}

// ---------------------------------------------------------------------------
// Switches: bring every switch to what its lamp shows. A switch also flips its right neighbour, so the row is
// solved from left to right.

void UHCTaskOverlayWidget::BuildSwitches(int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		const bool bWant = FMath::RandBool();
		SwitchTarget.Add(bWant);
		SwitchOn.Add(bWant);
	}
	// Start a few presses away from the pattern. Different switches pressed once each never cancel out.
	TArray<int32> Presses;
	for (int32 i = 0; i < Count; ++i)
	{
		Presses.Add(i);
	}
	for (int32 i = Presses.Num() - 1; i > 0; --i)
	{
		Presses.Swap(i, FMath::RandRange(0, i));
	}
	for (int32 i = 0; i < FMath::Min(Count, 3); ++i)
	{
		SwitchOn[Presses[i]] = !SwitchOn[Presses[i]];
		if (SwitchOn.IsValidIndex(Presses[i] + 1))
		{
			SwitchOn[Presses[i] + 1] = !SwitchOn[Presses[i] + 1];
		}
	}

	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	UHCPaintBox* Panel = HCLook::Control(WidgetTree, FVector2D(0.0, 146.0),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PaintSwitches(D, S);
			}
		},
		[Weak](const FVector2D& Local, const FVector2D& Size)
		{
			if (UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PressSwitch(Local, Size);
			}
		});
	HCUI::AddRow(ContentBox, Panel, 0.f);
}

void UHCTaskOverlayWidget::PressSwitch(const FVector2D& Local, const FVector2D& Size)
{
	const int32 Count = SwitchOn.Num();
	if (bFinished || Count == 0 || Size.X < 1.0)
	{
		return;
	}
	const int32 Index = FMath::Clamp(FMath::FloorToInt32(Local.X / (Size.X / Count)), 0, Count - 1);
	SwitchOn[Index] = !SwitchOn[Index];
	if (SwitchOn.IsValidIndex(Index + 1))
	{
		SwitchOn[Index + 1] = !SwitchOn[Index + 1];
	}
	HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.45f);
}

bool UHCTaskOverlayWidget::TickSwitches()
{
	// Halfway through two lamps change their mind
	if (ShiftsDone == 0 && Progress >= 0.5f && SwitchTarget.Num() >= 2)
	{
		const int32 First = FMath::RandRange(0, SwitchTarget.Num() - 1);
		const int32 Second = (First + FMath::RandRange(1, SwitchTarget.Num() - 1)) % SwitchTarget.Num();
		SwitchTarget[First] = !SwitchTarget[First];
		SwitchTarget[Second] = !SwitchTarget[Second];
		++ShiftsDone;
	}
	int32 Wrong = 0;
	for (int32 i = 0; i < SwitchOn.Num(); ++i)
	{
		if (bAutoSolve)
		{
			SwitchOn[i] = SwitchTarget[i];
		}
		Wrong += SwitchOn[i] != SwitchTarget[i] ? 1 : 0;
	}
	const bool bAll = Wrong == 0;
	StatusText->SetText(bAll ? NSLOCTEXT("HC", "SwitchesGood", "Stromkreise stabil ...") : FText::Format(NSLOCTEXT("HC", "SwitchesBadFmt", "{0} Schalter stehen falsch"), Wrong));
	StatusText->SetColorAndOpacity(FSlateColor(bAll ? HCLook::Signal : HCLook::Bone));
	return bAll;
}

void UHCTaskOverlayWidget::PaintSwitches(const FHCDraw& D, const FHCPaintState& S) const
{
	const int32 Count = SwitchOn.Num();
	if (Count == 0)
	{
		return;
	}
	const FVector2D Size = D.Size;
	const double Column = Size.X / Count;
	const int32 HotIndex = (S.bHovered || S.bPressed) && !bFinished ? FMath::Clamp(FMath::FloorToInt32(S.Mouse.X / Column), 0, Count - 1) : INDEX_NONE;
	const bool bTip = IsHintActive();
	const FLinearColor Amber = HCTaskLook::Amber();
	const FSlateFontInfo StateFont = HCLook::Caps(10.f, 200);
	const FString OnText = HCLook::Upper(NSLOCTEXT("HC", "SwitchOn", "an"));
	const FString OffText = HCLook::Upper(NSLOCTEXT("HC", "SwitchOff", "aus"));

	for (int32 i = 0; i < Count; ++i)
	{
		const double CenterX = (i + 0.5) * Column;
		const bool bOn = SwitchOn[i];
		const bool bWant = SwitchTarget[i];
		const bool bGood = bOn == bWant;
		// The switch under the mouse and the neighbour it takes along
		const bool bHot = i == HotIndex;
		const bool bPulled = HotIndex != INDEX_NONE && i == HotIndex + 1;

		// The lamp: lit = this switch belongs up
		const FVector2D Lamp(CenterX - 10.0, 4.0);
		D.Box(Lamp, FVector2D(20.0, 20.0), bWant ? HCLook::Alpha(Amber, 0.95f) : HCLook::Alpha(HCLook::Ink, 0.85f));
		D.Frame(Lamp, FVector2D(20.0, 20.0), 1.f, HCLook::Alpha(bWant ? Amber : HCLook::BoneMuted, 0.8f));

		// The slot and its lever
		const FVector2D Well(CenterX - 16.0, 36.0);
		const FVector2D WellSize(32.0, 80.0);
		D.Box(Well, WellSize, HCLook::Alpha(HCLook::Plate, 0.95f));
		if (bHot || bPulled)
		{
			D.Box(Well, WellSize, HCLook::Alpha(HCLook::Bone, bHot ? 0.1f : 0.05f));
		}
		const FLinearColor Edge = bGood ? HCLook::Signal : bTip ? HCLook::Blood : bHot ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, 0.6f);
		D.Frame(Well, WellSize, bHot ? 2.f : 1.f, Edge);
		D.Box(FVector2D(CenterX - 1.0, Well.Y + 8.0), FVector2D(2.0, WellSize.Y - 16.0), HCLook::Alpha(HCLook::Bone, 0.15f));
		const double KnobY = bOn ? Well.Y + 5.0 : Well.Y + WellSize.Y - 5.0 - 30.0;
		D.Box(FVector2D(Well.X + 5.0, KnobY), FVector2D(22.0, 30.0), bGood ? HCLook::Alpha(HCLook::Signal, 0.9f) : HCLook::Alpha(HCLook::Bone, 0.85f));

		const FString& State = bOn ? OnText : OffText;
		D.TextCentered(State, CenterX, 124.0, StateFont, bGood ? HCLook::Signal : HCLook::BoneMuted);

		// The link to the right neighbour
		if (i + 1 < Count)
		{
			const double LinkX = (i + 1) * Column;
			const FLinearColor Link = HCLook::Alpha(HCLook::BoneMuted, bHot ? 1.f : 0.45f);
			D.Lines({ FVector2D(LinkX - 5.0, 70.0), FVector2D(LinkX + 2.0, 76.0), FVector2D(LinkX - 5.0, 82.0) }, Link, 1.5f);
		}
	}
}

// ---------------------------------------------------------------------------
// Memory: fields light up in an order, the player repeats it. Three rounds, each one field longer.

int32 UHCTaskOverlayWidget::MemoryLength() const
{
	return HCTaskLook::MemoryFirstLength + MemoryRound;
}

void UHCTaskOverlayWidget::BuildMemory(int32 Count)
{
	MemoryTiles = Count;
	const int32 Rows = FMath::DivideAndRoundUp(Count, HCTaskLook::MemoryColumns);
	const TWeakObjectPtr<UHCTaskOverlayWidget> Weak(this);
	UHCPaintBox* Panel = HCLook::Control(WidgetTree, FVector2D(0.0, Rows * HCTaskLook::MemoryTileHeight + (Rows - 1) * HCTaskLook::MemoryGap),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PaintMemory(D, S);
			}
		},
		[Weak](const FVector2D& Local, const FVector2D& Size)
		{
			if (UHCTaskOverlayWidget* Widget = Weak.Get())
			{
				Widget->PressMemory(Local, Size);
			}
		});
	HCUI::AddRow(ContentBox, Panel, 0.f);
	StartMemoryRound(true);
}

void UHCTaskOverlayWidget::StartMemoryRound(bool bNewOrder)
{
	if (bNewOrder)
	{
		MemoryOrder.Reset();
		for (int32 i = 0; i < MemoryLength(); ++i)
		{
			// Never the same field twice in a row: it would look like one long flash
			int32 Tile = FMath::RandRange(0, MemoryTiles - 1);
			if (MemoryOrder.Num() > 0 && Tile == MemoryOrder.Last())
			{
				Tile = (Tile + 1 + FMath::RandRange(0, MemoryTiles - 2)) % MemoryTiles;
			}
			MemoryOrder.Add(Tile);
		}
	}
	MemoryInput = 0;
	bMemoryShowing = true;
	MemoryShowStart = FPlatformTime::Seconds();
}

void UHCTaskOverlayWidget::PressMemory(const FVector2D& Local, const FVector2D& Size)
{
	if (bFinished || bMemoryShowing || !MemoryOrder.IsValidIndex(MemoryInput) || Size.X < 1.0)
	{
		return;
	}
	const int32 Column = FMath::Clamp(FMath::FloorToInt32(Local.X / (Size.X / HCTaskLook::MemoryColumns)), 0, HCTaskLook::MemoryColumns - 1);
	const int32 Row = FMath::FloorToInt32(Local.Y / (HCTaskLook::MemoryTileHeight + HCTaskLook::MemoryGap));
	const int32 Tile = Row * HCTaskLook::MemoryColumns + Column;
	if (Tile < 0 || Tile >= MemoryTiles)
	{
		return;
	}
	HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.45f);
	if (MemoryOrder[MemoryInput] != Tile)
	{
		// Wrong field: the same order is shown again
		WrongIndex = Tile;
		WrongAt = FPlatformTime::Seconds();
		StartMemoryRound(false);
		return;
	}
	MemoryPressed = Tile;
	MemoryPressedAt = FPlatformTime::Seconds();
	if (++MemoryInput >= MemoryOrder.Num())
	{
		++MemoryRound;
		if (MemoryRound < HCTaskLook::MemoryRounds)
		{
			StartMemoryRound(true);
		}
	}
}

void UHCTaskOverlayWidget::TickMemory()
{
	const double Now = FPlatformTime::Seconds();
	if (MemoryRound >= HCTaskLook::MemoryRounds)
	{
		Progress = 1.f;
		return;
	}
	if (bMemoryShowing && Now - MemoryShowStart >= HCTaskLook::MemoryLead + HCTaskLook::MemoryStep * MemoryOrder.Num())
	{
		bMemoryShowing = false;
	}
	if (bAutoSolve && !bMemoryShowing && MemoryOrder.IsValidIndex(MemoryInput))
	{
		// Press the field that is next, in the middle of it
		const int32 Tile = MemoryOrder[MemoryInput];
		const FVector2D FakeSize(HCTaskLook::MemoryColumns * 100.0, 0.0);
		PressMemory(FVector2D((Tile % HCTaskLook::MemoryColumns + 0.5) * 100.0, (Tile / HCTaskLook::MemoryColumns + 0.5) * (HCTaskLook::MemoryTileHeight + HCTaskLook::MemoryGap)), FakeSize);
	}
	if (MemoryRound >= HCTaskLook::MemoryRounds)
	{
		Progress = 1.f;
		return;
	}
	Progress = (MemoryRound + static_cast<float>(MemoryInput) / FMath::Max(1, MemoryOrder.Num())) / HCTaskLook::MemoryRounds;
	const bool bWrongNow = WrongIndex != INDEX_NONE && Now - WrongAt < 1.2;
	StatusText->SetText(bWrongNow ? NSLOCTEXT("HC", "MemoryWrong", "Falsches Feld – schau noch einmal hin")
		: bMemoryShowing ? FText::Format(NSLOCTEXT("HC", "MemoryWatchFmt", "Runde {0} von {1}  ·  merken ..."), MemoryRound + 1, HCTaskLook::MemoryRounds)
		: FText::Format(NSLOCTEXT("HC", "MemoryRepeatFmt", "Runde {0} von {1}  ·  Feld {2} von {3} antippen"), MemoryRound + 1, HCTaskLook::MemoryRounds, MemoryInput + 1, MemoryOrder.Num()));
	StatusText->SetColorAndOpacity(FSlateColor(bWrongNow ? HCLook::Blood : bMemoryShowing ? HCTaskLook::Amber() : HCLook::Bone));
}

void UHCTaskOverlayWidget::PaintMemory(const FHCDraw& D, const FHCPaintState& S) const
{
	const FVector2D Size = D.Size;
	const int32 Columns = HCTaskLook::MemoryColumns;
	const double Gap = HCTaskLook::MemoryGap;
	const double TileWidth = (Size.X - Gap * (Columns - 1)) / Columns;
	const double TileHeight = HCTaskLook::MemoryTileHeight;
	const FLinearColor Amber = HCTaskLook::Amber();

	// Which field is lit right now while the order is shown
	int32 ShownTile = INDEX_NONE;
	if (bMemoryShowing)
	{
		const double Since = S.Time - MemoryShowStart - HCTaskLook::MemoryLead;
		const int32 Position = Since >= 0.0 ? FMath::FloorToInt32(Since / HCTaskLook::MemoryStep) : INDEX_NONE;
		// Dark for the last third of every step, so two fields in a row read as two
		if (MemoryOrder.IsValidIndex(Position) && FMath::Fmod(Since, HCTaskLook::MemoryStep) < HCTaskLook::MemoryStep * 0.68)
		{
			ShownTile = MemoryOrder[Position];
		}
	}
	const bool bInput = !bMemoryShowing && !bFinished && MemoryOrder.IsValidIndex(MemoryInput);
	const int32 TipTile = bInput && IsHintActive() ? MemoryOrder[MemoryInput] : INDEX_NONE;
	int32 HotTile = INDEX_NONE;
	if (bInput && (S.bHovered || S.bPressed))
	{
		const int32 Column = FMath::Clamp(FMath::FloorToInt32(S.Mouse.X / (Size.X / Columns)), 0, Columns - 1);
		HotTile = FMath::FloorToInt32(S.Mouse.Y / (TileHeight + Gap)) * Columns + Column;
	}

	for (int32 Tile = 0; Tile < MemoryTiles; ++Tile)
	{
		const FVector2D Pos((Tile % Columns) * (TileWidth + Gap), (Tile / Columns) * (TileHeight + Gap));
		const FVector2D TileSize(TileWidth, TileHeight);
		const float Pressed = Tile == MemoryPressed ? 1.f - FMath::Clamp(static_cast<float>((S.Time - MemoryPressedAt) / 0.35), 0.f, 1.f) : 0.f;
		const float Wrong = Tile == WrongIndex ? 1.f - FMath::Clamp(static_cast<float>((S.Time - WrongAt) / 0.6), 0.f, 1.f) : 0.f;
		const bool bLit = Tile == ShownTile;
		const bool bHot = Tile == HotTile;

		D.Box(Pos, TileSize, HCLook::Alpha(HCLook::Plate, 0.95f));
		if (bLit)
		{
			D.Box(Pos, TileSize, HCLook::Alpha(Amber, 0.85f));
		}
		else if (Wrong > 0.f)
		{
			D.Box(Pos, TileSize, HCLook::Alpha(HCLook::Blood, 0.55f * Wrong));
		}
		else if (Pressed > 0.f)
		{
			D.Box(Pos, TileSize, HCLook::Alpha(HCLook::Signal, 0.5f * Pressed));
		}
		else if (Tile == TipTile)
		{
			D.Box(Pos, TileSize, HCLook::Alpha(Amber, 0.16f));
		}
		else if (bHot)
		{
			D.Box(Pos, TileSize, HCLook::Alpha(HCLook::Bone, 0.09f));
		}
		const FLinearColor Edge = bLit ? Amber : Wrong > 0.f ? HCLook::Blood : bHot ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, bInput ? 0.6f : 0.3f);
		D.Frame(Pos, TileSize, 1.f, Edge);
		D.Box(Pos + FVector2D(0.0, TileHeight - 2.0), FVector2D(TileWidth, 2.0), Edge);
		D.Symbol(Tile, Pos + TileSize * 0.5, 13.f, bLit ? HCLook::Ink : HCLook::Alpha(HCLook::Bone, bInput ? 0.85f : 0.4f), 2.5f);
	}
}

// ---------------------------------------------------------------------------
void UHCTaskOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (FramePanel)
	{
		// The screen switches on instead of popping up
		FramePanel->SetRenderOpacity(HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - OpenedAt), 0.f, 0.16f));
	}
	if (bFinished || !StatusText)
	{
		return;
	}
	if (!Station.IsValid())
	{
		Cancel();
		return;
	}
	if (bAutoSolveWanted && !bAutoSolve && FPlatformTime::Seconds() - OpenedAt >= AutoSolveDelay)
	{
		bAutoSolve = true;
	}
	if (!bHintShown && HintText && IsHintActive())
	{
		bHintShown = true;
		HintText->SetText(GetHintText());
		HintText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	const float Before = Progress;
	switch (Kind)
	{
	case EHCTaskOverlayKind::Dials:
	case EHCTaskOverlayKind::Curve:
	case EHCTaskOverlayKind::Temperature:
	case EHCTaskOverlayKind::Wires:
	case EHCTaskOverlayKind::Switches:
	{
		const bool bInTheGreen = Kind == EHCTaskOverlayKind::Dials ? TickDials() : Kind == EHCTaskOverlayKind::Curve ? TickCurve()
			: Kind == EHCTaskOverlayKind::Wires ? TickWires() : Kind == EHCTaskOverlayKind::Switches ? TickSwitches() : TickTemperature(InDeltaTime);
		if (bInTheGreen)
		{
			Progress += InDeltaTime / Duration;
		}
		break;
	}
	case EHCTaskOverlayKind::Memory:
		TickMemory();
		break;
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
	bAdvancing = Progress > Before;
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

FReply UHCTaskOverlayWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	// Movement or Esc cancels every task at once (docs/01 §5)
	if (Key == EKeys::Escape || Key == EKeys::F1 || HCKeys::Is(EHCBinding::Forward, Key) || HCKeys::Is(EHCBinding::Back, Key)
		|| HCKeys::Is(EHCBinding::Left, Key) || HCKeys::Is(EHCBinding::Right, Key))
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
