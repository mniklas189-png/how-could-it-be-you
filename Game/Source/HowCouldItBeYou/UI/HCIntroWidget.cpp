#include "UI/HCIntroWidget.h"

#include "Audio/HCProceduralAudio.h"
#include "Blueprint/WidgetTree.h"
#include "Core/HCGameState.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCPlayerController.h"
#include "UI/HCPaint.h"
#include "UI/HCUIStyle.h"
#include "EngineUtils.h"
#include "World/HCStationBuilder.h"

namespace
{
	// Prefixed names: HCHUD.cpp and the other painted screens have their own Ink, Paper ...; a unity build may put
	// several of these files into one translation unit, where equal names in anonymous namespaces collide.
	const FLinearColor IntroInk(0.004f, 0.005f, 0.007f, 1.f);
	const FLinearColor IntroPaper(0.87f, 0.89f, 0.92f, 1.f);
	const FLinearColor IntroMuted(0.46f, 0.49f, 0.54f, 1.f);
	const FLinearColor IntroAmber(1.f, 0.7f, 0.22f, 1.f);
	const FLinearColor IntroBlood(0.86f, 0.09f, 0.07f, 1.f);

	/** Typing speed of the story text, letters per second, and when it starts on a page. */
	constexpr float TypeSpeed = 52.f;
	constexpr float TypeDelay = 0.55f;
	constexpr float HeaderSpeed = 90.f;

	FLinearColor IntroAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, C.A * FMath::Clamp(A, 0.f, 1.f));
	}

	/** Cheap repeatable noise 0..1, for flicker. */
	float IntroHash(float X)
	{
		return FMath::Frac(FMath::Sin(X * 12.9898f) * 43758.547f);
	}
}

TArray<FHCStoryBeat> UHCIntroWidget::GetBeats(FName MapId)
{
	// The story of docs/01 §2 in short. Neutral grey NPCs only - nothing here points at a player.
	TArray<FHCStoryBeat> Beats;
	// The freighter Ikarus carries the sample from Europa to Kepler-7 (docs/01 §2): the same night, another place
	const bool bIkarus = MapId == TEXT("Ikarus");
	if (bIkarus)
	{
		Beats.Add({ NSLOCTEXT("HC", "Story1HeadIkarus", "JAHR 2148  ·  HESPERUS-INITIATIVE  ·  FRACHTER IKARUS"),
			NSLOCTEXT("HC", "Story1Ikarus", "Frachter Ikarus, unterwegs vom Jupitermond Europa nach Kepler-7. Im Bio-Labor fährt eine Probe aus dem Eis mit. Sie lag ruhig in ihrem Glas. Bis heute Nacht."), 7.5f });
	}
	else
	{
		Beats.Add({ NSLOCTEXT("HC", "Story1Head", "JAHR 2148  ·  HESPERUS-INITIATIVE  ·  KEPLER-7"),
			NSLOCTEXT("HC", "Story1", "Forschungsstation Kepler-7, im Orbit. Seit 41 Tagen untersucht eure Crew eine Probe aus dem Eis des Jupitermonds Europa. Sie lag ruhig in ihrem Glas. Bis heute Nacht."), 7.5f });
	}
	Beats.Add({ NSLOCTEXT("HC", "Story2Head", "TAG 41  ·  03:12 STATIONSZEIT  ·  BIO-LABOR"),
		NSLOCTEXT("HC", "Story2", "Das Glas sprang. Was darin lag, berührte Ingenieur Brandt, und plötzlich standen zwei Brandts im Labor. Kommandant Hale hob seine Lampe. Von ihm blieb nur seine Marke."), 8.f });
	Beats.Add({ bIkarus ? NSLOCTEXT("HC", "Story3HeadIkarus", "TAG 41  ·  03:20  ·  BRÜCKE") : NSLOCTEXT("HC", "Story3Head", "TAG 41  ·  03:20  ·  KOMMANDOZENTRALE"),
		NSLOCTEXT("HC", "Story3", "Ihr habt euch eingeschlossen und zählt die Gesichter. Alle sind da. Aber das Ding kopiert Haut, Anzug und Ausweis. Nur die Stimme nicht: Es spricht immer mit der seines ersten Wirts."), 8.f });
	Beats.Add({ NSLOCTEXT("HC", "Story4Head", "LAGE"),
		NSLOCTEXT("HC", "Story4", "Haltet die Station am Laufen und meldet jeden Toten. Beim Alarm zwingt das Dekon-Licht das Ding zurück in seine erste Gestalt. Dann stimmt ihr ab, wer in die Luftschleuse geht."), 8.f });
	Beats.Add({ NSLOCTEXT("HC", "StoryFinal", "ES IST EINER VON EUCH."), NSLOCTEXT("HC", "StoryFinalSub", "TRAUT NIEMANDEM."), 4.5f, true });
	return Beats;
}

FName UHCIntroWidget::GetStoryMap() const
{
	// The first (only) station builder of the world; a plain check instead of a loop that runs once (clang -Werror)
	TActorIterator<AHCStationBuilder> It(GetWorld());
	return It ? It->GetLayout().MapName : NAME_None;
}

float UHCIntroWidget::GetStorySeconds(bool bFull)
{
	const TArray<FHCStoryBeat> Beats = GetBeats();
	float Total = 0.f;
	for (const FHCStoryBeat& Beat : Beats)
	{
		Total += Beat.Seconds;
	}
	// Later rounds: only the situation and the closing line
	return bFull ? Total : Beats.Last(1).Seconds + Beats.Last().Seconds;
}

void UHCIntroWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Empty root: everything is painted, the widget only has to fill the screen
	HCUI::MakeRoot(WidgetTree, FLinearColor::Transparent);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	OpenedAt = FPlatformTime::Seconds();
}

float UHCIntroWidget::GetStoryTime() const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS || GS->GetMatchPhase() != EHCMatchPhase::Intro)
	{
		return -1.f;
	}
	// The story always ends with the phase; a shorter phase simply starts later in it
	return GetStorySeconds(true) - GS->GetPhaseRemainingSeconds();
}

void UHCIntroWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const float Time = GetStoryTime();
	if (Time < 0.f)
	{
		return;
	}
	// A dull hit for every new page, the stinger for the closing line (the same for everyone)
	const TArray<FHCStoryBeat> Beats = GetBeats(GetStoryMap());
	float Start = 0.f;
	for (int32 i = 0; i < Beats.Num(); ++i)
	{
		if (Time >= Start && Time < Start + Beats[i].Seconds)
		{
			if (i != LastBeat)
			{
				LastBeat = i;
				HCProceduralAudio::Play2D(this, Beats[i].bFinal ? HCProceduralAudio::ESound::Stinger : HCProceduralAudio::ESound::Thump, Beats[i].bFinal ? 0.55f : 0.35f);
			}
			break;
		}
		Start += Beats[i].Seconds;
	}

	// Hold the interact key to vote for skipping
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!bSkipSent && PC)
	{
		const bool bDown = PC->IsInputKeyDown(HCKeys::Get(EHCBinding::Interact));
		SkipHold = bDown ? SkipHold + InDeltaTime / 0.8f : FMath::Max(0.f, SkipHold - InDeltaTime * 3.f);
		if (SkipHold >= 1.f)
		{
			bSkipSent = true;
			PC->ServerVoteSkipIntro();
		}
	}
}

int32 UHCIntroWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	FHCPainter P(AllottedGeometry, OutDrawElements, MaxLayer);
	const FVector2D S = P.Size;
	P.Box(FVector2D::ZeroVector, S, IntroInk);

	const float Time = GetStoryTime();
	const float Real = static_cast<float>(FPlatformTime::Seconds() - OpenedAt);
	if (Time < 0.f)
	{
		return P.Layer;
	}
	const TArray<FHCStoryBeat> Beats = GetBeats(GetStoryMap());
	const float Chrome = FMath::Clamp(Real / 0.8f, 0.f, 1.f);

	// --- Find the page ---
	int32 Index = Beats.Num() - 1;
	float Start = 0.f;
	for (int32 i = 0; i < Beats.Num(); ++i)
	{
		if (Time < Start + Beats[i].Seconds)
		{
			Index = i;
			break;
		}
		Start += Beats[i].Seconds;
	}
	const FHCStoryBeat& Beat = Beats[Index];
	const float Local = FMath::Max(0.f, Time - Start);
	const float Fade = FMath::Min(FMath::Clamp(Local / 0.4f, 0.f, 1.f), FMath::Clamp((Beat.Seconds - Local) / 0.5f, 0.f, 1.f));

	if (!Beat.bFinal)
	{
		// --- A page of the log: header line, then the text typed out with a block cursor ---
		const double ColumnW = FMath::Min(1040.0, S.X - 200.0);
		const double X0 = (S.X - ColumnW) * 0.5;
		const FSlateFontInfo HeadFont = FHCPainter::Font(TEXT("Mono"), 19.f, 220);
		const FSlateFontInfo BodyFont = FHCPainter::Font(TEXT("Regular"), 31.f, 10);
		const double LineH = 50.0;
		const TArray<FString> Lines = FHCPainter::Wrap(Beat.Body.ToString(), BodyFont, static_cast<float>(ColumnW));
		const double BlockH = 30.0 + 34.0 + Lines.Num() * LineH;
		const double Y0 = S.Y * 0.47 - BlockH * 0.5;

		const FString Head = Beat.Header.ToString();
		const int32 HeadChars = FMath::Clamp(FMath::FloorToInt((Local - 0.1f) * HeaderSpeed), 0, Head.Len());
		P.Box(FVector2D(X0 - 22.0, Y0 + 1.0), FVector2D(4.0, 24.0), IntroAlpha(IntroAmber, Fade));
		P.Text(Head.Left(HeadChars), FVector2D(X0, Y0), HeadFont, IntroAlpha(IntroAmber, Fade * 0.95f));
		P.Box(FVector2D(X0, Y0 + 40.0), FVector2D(ColumnW, 1.0), IntroAlpha(IntroMuted, Fade * 0.25f));

		int32 Budget = FMath::FloorToInt(FMath::Max(0.f, Local - TypeDelay) * TypeSpeed);
		const bool bTyping = Budget < Beat.Body.ToString().Len();
		FVector2D CaretPos(X0, Y0 + 64.0);
		for (int32 L = 0; L < Lines.Num() && Budget > 0; ++L)
		{
			const FString Shown = Lines[L].Left(Budget);
			Budget -= Lines[L].Len() + 1;
			const FVector2D LinePos(X0, Y0 + 64.0 + L * LineH);
			P.Text(Shown, LinePos, BodyFont, IntroAlpha(IntroPaper, Fade), 0.6f);
			CaretPos = LinePos + FVector2D(FHCPainter::Measure(Shown, BodyFont).X + 6.0, 0.0);
		}
		// The cursor stays solid while typing and blinks once the page is complete
		const bool bCursorOn = bTyping || FMath::Fmod(Real, 1.f) < 0.55f;
		if (bCursorOn && Local > TypeDelay * 0.5f)
		{
			P.Box(CaretPos + FVector2D(0.0, 7.0), FVector2D(15.0, 32.0), IntroAlpha(IntroAmber, Fade * 0.85f));
		}
	}
	else
	{
		// --- The closing line: hits hard, flickers, shakes for a moment ---
		const float Flicker = Local < 0.55f ? (IntroHash(FMath::FloorToFloat(Local * 30.f)) > 0.3f ? 1.f : 0.15f) : 1.f;
		const float Shake = Local < 0.5f ? (1.f - Local / 0.5f) * 5.f : 0.f;
		const FVector2D Jitter(FMath::Sin(Local * 71.f) * Shake, FMath::Cos(Local * 53.f) * Shake * 0.5f);
		const float A = Fade * Flicker;
		const double CY = S.Y * 0.46;
		P.Glow(FVector2D(S.X * 0.5, CY + 44.0), FVector2D(1500.0, 360.0), IntroAlpha(FLinearColor(0.55f, 0.02f, 0.02f, 1.f), 0.55f * A));
		const FSlateFontInfo BigFont = FHCPainter::Font(TEXT("Black"), 84.f, 60);
		P.TextCentered(Beat.Header.ToString(), static_cast<float>(S.X * 0.5 + Jitter.X), static_cast<float>(CY + Jitter.Y), BigFont, IntroAlpha(IntroBlood, A), 0.8f);
		const float SubA = Fade * FMath::Clamp((Local - 1.3f) / 0.6f, 0.f, 1.f);
		P.TextCentered(Beat.Body.ToString(), static_cast<float>(S.X * 0.5), static_cast<float>(CY + 132.0), FHCPainter::Font(TEXT("Mono"), 20.f, 650), IntroAlpha(IntroMuted, SubA));
	}

	// --- Frame around everything: corner brackets, log title, page marks, skip hint ---
	const FSlateFontInfo SmallMono = FHCPainter::Font(TEXT("Mono"), 15.f, 220);
	P.CornerBrackets(FVector2D(48.0, 48.0), S - FVector2D(96.0, 96.0), 34.f, 2.f, IntroAlpha(IntroMuted, Chrome * 0.4f));
	const bool bDot = FMath::Fmod(Real, 1.1f) < 0.7f;
	P.Box(FVector2D(80.0, 82.0), FVector2D(10.0, 10.0), IntroAlpha(IntroBlood, Chrome * (bDot ? 1.f : 0.25f)));
	P.Text(NSLOCTEXT("HC", "StoryLogTitle", "KEPLER-7  ·  NOTFALLPROTOKOLL").ToString(), FVector2D(102.0, 76.0), SmallMono, IntroAlpha(IntroMuted, Chrome));
	const FString Agency = NSLOCTEXT("HC", "StoryAgency", "HESPERUS-INITIATIVE").ToString();
	P.Text(Agency, FVector2D(S.X - 80.0 - FHCPainter::Measure(Agency, SmallMono).X, 76.0), SmallMono, IntroAlpha(IntroMuted, Chrome * 0.7f));

	// Page marks: done, current (filling up), still to come
	const double SegW = 54.0, SegGap = 10.0;
	const double MarksW = Beats.Num() * SegW + (Beats.Num() - 1) * SegGap;
	for (int32 i = 0; i < Beats.Num(); ++i)
	{
		const FVector2D At((S.X - MarksW) * 0.5 + i * (SegW + SegGap), S.Y - 88.0);
		P.Box(At, FVector2D(SegW, 3.0), IntroAlpha(IntroMuted, Chrome * 0.3f));
		const double Filled = i < Index ? 1.0 : i == Index ? FMath::Clamp(Local / Beat.Seconds, 0.f, 1.f) : 0.0;
		P.Box(At, FVector2D(SegW * Filled, 3.0), IntroAlpha(IntroAmber, Chrome * 0.85f));
	}

	// Skip: key cap + text, a bar while held, and how many want to skip (seen by everyone)
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const FString SkipText = bSkipSent ? NSLOCTEXT("HC", "StorySkipSent", "DU WILLST ÜBERSPRINGEN").ToString() : NSLOCTEXT("HC", "StorySkipHold", "HALTEN: ÜBERSPRINGEN").ToString();
	const double SkipTextW = FHCPainter::Measure(SkipText, SmallMono).X;
	const double CapH = 30.0;
	const FString Key = HCKeys::ShortName(EHCBinding::Interact).ToString();
	const double CapW = FMath::Max(CapH, FHCPainter::Measure(Key, FHCPainter::Font(TEXT("Bold"), CapH * 0.5f)).X + CapH * 0.6);
	const double HintW = CapW + 14.0 + SkipTextW;
	const FVector2D HintPos(S.X - 80.0 - HintW, S.Y - 104.0);
	const float HintA = Chrome * (bSkipSent ? 0.55f : 0.9f);
	P.KeyCap(Key, HintPos, static_cast<float>(CapH), IntroAlpha(bSkipSent ? IntroMuted : IntroAmber, HintA));
	P.Text(SkipText, HintPos + FVector2D(CapW + 14.0, 6.0), SmallMono, IntroAlpha(IntroPaper, HintA * 0.8f));
	if (!bSkipSent && SkipHold > 0.f)
	{
		P.Box(HintPos + FVector2D(0.0, CapH + 10.0), FVector2D(HintW, 3.0), IntroAlpha(IntroMuted, 0.35f));
		P.Box(HintPos + FVector2D(0.0, CapH + 10.0), FVector2D(HintW * SkipHold, 3.0), IntroAmber);
	}
	if (GS && GS->GetIntroSkipVotes() > 0)
	{
		const FString Votes = FText::Format(NSLOCTEXT("HC", "StorySkipVotesFmt", "ÜBERSPRINGEN: {0} / {1}"), GS->GetIntroSkipVotes(), GS->PlayerArray.Num()).ToString();
		P.Text(Votes, FVector2D(S.X - 80.0 - FHCPainter::Measure(Votes, SmallMono).X, HintPos.Y + CapH + 22.0), SmallMono, IntroAlpha(IntroMuted, Chrome));
	}

	P.ScanLines(Real, 0.22f);
	return P.Layer;
}
