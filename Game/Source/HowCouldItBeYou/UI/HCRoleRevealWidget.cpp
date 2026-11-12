#include "UI/HCRoleRevealWidget.h"

#include "Audio/HCProceduralAudio.h"
#include "Blueprint/WidgetTree.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UI/HCPaint.h"
#include "UI/HCUIStyle.h"

namespace
{
	// Prefixed names: HCHUD.cpp and the other painted screens have their own Ink, Paper ...; a unity build may put
	// several of these files into one translation unit, where equal names in anonymous namespaces collide.
	const FLinearColor CardInk(0.004f, 0.005f, 0.007f, 1.f);
	const FLinearColor CardPaper(0.88f, 0.9f, 0.93f, 1.f);
	const FLinearColor CardMuted(0.46f, 0.49f, 0.54f, 1.f);
	const FLinearColor CardMonsterRed(0.9f, 0.1f, 0.08f, 1.f);
	const FLinearColor CardCrewBlue(0.42f, 0.78f, 1.f, 1.f);
	const FLinearColor CardColor(0.025f, 0.029f, 0.038f, 0.94f);

	FLinearColor CardAlpha(const FLinearColor& C, float A)
	{
		return FLinearColor(C.R, C.G, C.B, C.A * FMath::Clamp(A, 0.f, 1.f));
	}

	/** 0..1 from Delay on, over Duration, eased out. */
	float CardAppear(float Time, float Delay, float Duration)
	{
		const float X = FMath::Clamp((Time - Delay) / Duration, 0.f, 1.f);
		return 1.f - FMath::Square(1.f - X);
	}

	float CardHash(float X)
	{
		return FMath::Frac(FMath::Sin(X * 12.9898f) * 43758.547f);
	}

	struct FRoleGoal
	{
		FText Text;
		bool bHasKey = false;
		EHCBinding Key = EHCBinding::Interact;
	};
}

void UHCRoleRevealWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	HCUI::MakeRoot(WidgetTree, FLinearColor::Transparent);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UHCRoleRevealWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (RevealedAt > 0.0)
	{
		return;
	}
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	// Show the role only once our own role with the current serial arrived (docs/02 §5.4)
	if (GS && PS && PS->GetRoleInfo().MatchSerial == GS->GetMatchSerial())
	{
		RevealedAt = FPlatformTime::Seconds();
		// One sound, equally loud for both roles: nobody can hear a role through someone's microphone
		HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Thump, 0.6f);
	}
}

int32 UHCRoleRevealWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	FHCPainter P(AllottedGeometry, OutDrawElements, MaxLayer);
	const FVector2D S = P.Size;
	P.Box(FVector2D::ZeroVector, S, CardInk);

	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	if (RevealedAt <= 0.0 || !GS || !PS)
	{
		return P.Layer;
	}
	const float T = static_cast<float>(FPlatformTime::Seconds() - RevealedAt);
	const float Remaining = GS->GetPhaseRemainingSeconds();
	const float Total = FMath::Max(1.f, UHCGameSettings::Get()->RoleRevealSeconds);
	const float Out = FMath::Clamp(Remaining / 0.35f, 0.f, 1.f);

	const bool bMonster = PS->GetRoleInfo().Role == EHCRole::Monster;
	const FLinearColor Role = bMonster ? CardMonsterRed : CardCrewBlue;
	const int32 Identity = FMath::Max(0, PS->GetIdentity());
	const FLinearColor IdColor = HCIdentity::Color(Identity);
	const double CX = S.X * 0.5;

	// --- Room light in the colour of the role ---
	const float Bg = CardAppear(T, 0.f, 0.8f) * Out;
	const FLinearColor RoleDark(Role.R * 0.3f, Role.G * 0.3f, Role.B * 0.3f, 1.f);
	P.Gradient(FVector2D::ZeroVector, S, { { 0.f, CardAlpha(RoleDark, 0.35f * Bg) }, { 0.45f, CardAlpha(RoleDark, 0.f) }, { 0.8f, CardAlpha(RoleDark, 0.f) }, { 1.f, CardAlpha(RoleDark, 0.25f * Bg) } }, false);
	P.CornerBrackets(FVector2D(48.0, 48.0), S - FVector2D(96.0, 96.0), 34.f, 2.f, CardAlpha(Role, 0.4f * Bg));
	const FSlateFontInfo SmallMono = FHCPainter::Font(TEXT("Mono"), 15.f, 220);
	P.Box(FVector2D(80.0, 82.0), FVector2D(10.0, 10.0), CardAlpha(Role, Bg));
	P.Text(NSLOCTEXT("HC", "RevealPrivate", "KEPLER-7  ·  NUR FÜR DEINE AUGEN").ToString(), FVector2D(102.0, 76.0), SmallMono, CardAlpha(CardMuted, Bg));

	// --- "DEINE ROLLE" between two lines ---
	const float KickA = CardAppear(T, 0.f, 0.4f) * Out;
	const FSlateFontInfo KickFont = FHCPainter::Font(TEXT("Mono"), 20.f, 600);
	const FVector2D KickSize = P.TextCentered(NSLOCTEXT("HC", "RevealKicker", "DEINE ROLLE").ToString(), static_cast<float>(CX), 168.f, KickFont, CardAlpha(CardMuted, KickA));
	P.Box(FVector2D(CX - KickSize.X * 0.5 - 150.0, 181.0), FVector2D(120.0, 1.0), CardAlpha(CardMuted, KickA * 0.6f));
	P.Box(FVector2D(CX + KickSize.X * 0.5 + 30.0, 181.0), FVector2D(120.0, 1.0), CardAlpha(CardMuted, KickA * 0.6f));

	// --- The role: the letters close up while it fades in, with a flicker and a glow ---
	const float TitleA = CardAppear(T, 0.2f, 0.5f) * Out * (T < 0.6f ? (CardHash(FMath::FloorToFloat(T * 32.f)) > 0.35f ? 1.f : 0.2f) : 1.f);
	const int32 Spacing = FMath::RoundToInt(FMath::Lerp(900.f, 160.f, CardAppear(T, 0.2f, 0.9f)));
	const FSlateFontInfo TitleFont = FHCPainter::Font(TEXT("Black"), 124.f, Spacing);
	const FString Title = bMonster ? NSLOCTEXT("HC", "RevealMonster", "DER WANDLER").ToString() : NSLOCTEXT("HC", "RevealCrew", "CREW").ToString();
	P.Glow(FVector2D(CX, 290.0), FVector2D(1500.0, 340.0), CardAlpha(Role, 0.22f * TitleA));
	if (bMonster)
	{
		// The Wandler's name never quite holds still: a faint second copy slips to the side
		const float Slip = 4.f + 3.f * FMath::Sin(T * 9.f);
		P.TextCentered(Title, static_cast<float>(CX - Slip), 204.f, TitleFont, CardAlpha(FLinearColor(1.f, 0.f, 0.15f, 1.f), 0.3f * TitleA));
	}
	P.TextCentered(Title, static_cast<float>(CX), 204.f, TitleFont, CardAlpha(Role, TitleA), 0.7f);

	// --- What it means, in one line ---
	const float TagA = CardAppear(T, 0.8f, 0.45f) * Out;
	const FText Tag = bMonster ? NSLOCTEXT("HC", "RevealMonsterGoal", "Töte. Täusche. Werde einer von ihnen.") : NSLOCTEXT("HC", "RevealCrewGoal", "Erledige deine Aufgaben. Finde den Wandler.");
	P.TextCentered(Tag.ToString(), static_cast<float>(CX), static_cast<float>(396.0 + (1.f - TagA) * 14.f), FHCPainter::Font(TEXT("Regular"), 29.f, 20), CardAlpha(CardPaper, TagA), 0.6f);
	P.Box(FVector2D(CX - 280.0 * TagA, 462.0), FVector2D(560.0 * TagA, 2.0), CardAlpha(Role, 0.55f * TagA));

	// --- Identity card: symbol tile, name, colour and symbol ---
	const float CardA = CardAppear(T, 1.15f, 0.45f) * Out;
	const double CardW = 800.0, CardH = 150.0;
	const FVector2D Card(CX - CardW * 0.5, 498.0 + (1.f - CardA) * 18.f);
	P.Box(Card, FVector2D(CardW, CardH), CardAlpha(CardColor, CardA));
	P.Frame(Card, FVector2D(CardW, CardH), 1.f, CardAlpha(Role, 0.3f * CardA));
	P.Box(Card, FVector2D(6.0, CardH), CardAlpha(IdColor, CardA));
	const FVector2D Tile = Card + FVector2D(30.0, 20.0);
	P.Box(Tile, FVector2D(110.0, 110.0), CardAlpha(FLinearColor(0.012f, 0.014f, 0.018f, 1.f), CardA));
	P.Frame(Tile, FVector2D(110.0, 110.0), 1.f, CardAlpha(IdColor, 0.45f * CardA));
	P.Symbol(Identity, Tile + FVector2D(55.0, 57.0), 33.f, CardAlpha(IdColor, CardA), 4.f);
	const double TextX = Tile.X + 140.0;
	P.Text((bMonster ? NSLOCTEXT("HC", "RevealSuitMonster", "DEINE ERSTE GESTALT") : NSLOCTEXT("HC", "RevealSuitCrew", "DEIN ANZUG")).ToString(),
		FVector2D(TextX, Card.Y + 24.0), FHCPainter::Font(TEXT("Mono"), 14.f, 300), CardAlpha(CardMuted, CardA));
	P.Text(PS->GetPlayerName(), FVector2D(TextX, Card.Y + 46.0), FHCPainter::Font(TEXT("Bold"), 40.f, 20), CardAlpha(CardPaper, CardA), 0.6f);
	P.Text(FText::Format(NSLOCTEXT("HC", "IdentFmt", "{0} · {1}"), HCIdentity::ColorName(Identity), HCIdentity::SymbolName(Identity)).ToString(),
		FVector2D(TextX, Card.Y + 102.0), FHCPainter::Font(TEXT("Medium"), 21.f, 40), CardAlpha(IdColor, CardA));

	// --- Three things to do, each with its key ---
	TArray<FRoleGoal> Goals;
	if (bMonster)
	{
		Goals.Add({ NSLOCTEXT("HC", "RevealMonsterTask1", "Töte, wenn dich niemand sieht"), true, EHCBinding::Kill });
		Goals.Add({ NSLOCTEXT("HC", "RevealMonsterTask2", "Nimm die Gestalt eines anderen an"), true, EHCBinding::Shapeshift });
		Goals.Add({ NSLOCTEXT("HC", "RevealMonsterTask3", "Sabotiere Strom und Türen über den Stationsplan"), true, EHCBinding::Map });
	}
	else
	{
		Goals.Add({ NSLOCTEXT("HC", "RevealCrewTask1", "Erledige die Aufgaben auf deiner Liste"), true, EHCBinding::TaskList });
		Goals.Add({ NSLOCTEXT("HC", "RevealCrewTask2", "Melde Überreste oder drück den Notfall-Knopf"), true, EHCBinding::Interact });
		Goals.Add({ NSLOCTEXT("HC", "RevealCrewTask3", "Wählt im Meeting den Wandler in die Schleuse"), false });
	}
	const FSlateFontInfo GoalFont = FHCPainter::Font(TEXT("Regular"), 22.f, 10);
	for (int32 i = 0; i < Goals.Num(); ++i)
	{
		const float GoalA = CardAppear(T, 1.6f + 0.22f * i, 0.4f) * Out;
		const FVector2D Row(Card.X + (1.f - GoalA) * 16.f, 694.0 + i * 52.0);
		// Key column on the left (the key cap centred in it, a small mark where no key belongs), the text after it
		constexpr double KeyColumn = 84.0;
		if (Goals[i].bHasKey)
		{
			const FString KeyName = HCKeys::ShortName(Goals[i].Key).ToString();
			const double CapW = FMath::Max(32.0, FHCPainter::Measure(KeyName, FHCPainter::Font(TEXT("Bold"), 16.f)).X + 19.2);
			P.KeyCap(KeyName, FVector2D(Row.X + (KeyColumn - 14.0 - CapW) * 0.5, Row.Y - 1.0), 32.f, CardAlpha(FLinearColor(1.f, 0.78f, 0.18f, 1.f), GoalA));
		}
		else
		{
			P.Box(Row + FVector2D((KeyColumn - 14.0 - 9.0) * 0.5, 12.0), FVector2D(9.0, 9.0), CardAlpha(Role, GoalA));
		}
		P.Text(Goals[i].Text.ToString(), Row + FVector2D(KeyColumn, 0.0), GoalFont, CardAlpha(CardPaper, GoalA * 0.92f));
		P.Box(FVector2D(Card.X, Row.Y + 43.0), FVector2D(CardW, 1.0), CardAlpha(CardMuted, 0.15f * GoalA));
	}

	// --- A tip that fits the role ---
	const float TipA = CardAppear(T, 2.6f, 0.6f) * Out;
	const FText Tip = bMonster ? NSLOCTEXT("HC", "RevealMonsterTip", "Deine Aufgabenliste ist nur Tarnung. Deine Stimme bleibt deine eigene.")
		: NSLOCTEXT("HC", "RevealCrewTip", "Der Wandler kopiert jedes Gesicht, aber nie die Stimme. Hör genau hin.");
	P.TextCentered(Tip.ToString(), static_cast<float>(CX), 868.f, FHCPainter::Font(TEXT("Italic"), 20.f, 10), CardAlpha(FLinearColor(0.64f, 0.67f, 0.72f, 1.f), TipA));

	// --- Countdown: a bar that runs out, and the seconds ---
	const float BarA = Bg;
	const double BarW = 800.0;
	P.Box(FVector2D(CX - BarW * 0.5, S.Y - 112.0), FVector2D(BarW, 3.0), CardAlpha(CardMuted, 0.25f * BarA));
	P.Box(FVector2D(CX - BarW * 0.5, S.Y - 112.0), FVector2D(BarW * FMath::Clamp(Remaining / Total, 0.f, 1.f), 3.0), CardAlpha(Role, 0.9f * BarA));
	P.TextCentered(FText::Format(NSLOCTEXT("HC", "RevealStartsFmt", "DIE RUNDE BEGINNT IN {0}"), FMath::CeilToInt(Remaining)).ToString(), static_cast<float>(CX),
		static_cast<float>(S.Y - 96.0), SmallMono, CardAlpha(CardMuted, BarA));

	P.ScanLines(T, 0.2f);
	return P.Layer;
}
