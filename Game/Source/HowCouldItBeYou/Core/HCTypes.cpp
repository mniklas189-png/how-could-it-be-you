#include "Core/HCTypes.h"

namespace HCIdentity
{
	static int32 Wrap(int32 Index)
	{
		return ((Index % Count) + Count) % Count;
	}

	FLinearColor Color(int32 Index)
	{
		// Order of docs/01 §13: red, blue, yellow, green, orange, violet, white, turquoise
		static const FLinearColor Colors[Count] = {
			FLinearColor(0.85f, 0.15f, 0.12f),
			FLinearColor(0.10f, 0.35f, 0.90f),
			FLinearColor(0.95f, 0.80f, 0.10f),
			FLinearColor(0.10f, 0.70f, 0.25f),
			FLinearColor(0.95f, 0.45f, 0.05f),
			FLinearColor(0.55f, 0.20f, 0.80f),
			FLinearColor(0.90f, 0.90f, 0.90f),
			FLinearColor(0.10f, 0.75f, 0.80f),
		};
		return Colors[Wrap(Index)];
	}

	FText ColorName(int32 Index)
	{
		static const TCHAR* Names[Count] = { TEXT("Rot"), TEXT("Blau"), TEXT("Gelb"), TEXT("Grün"), TEXT("Orange"), TEXT("Violett"), TEXT("Weiß"), TEXT("Türkis") };
		return FText::FromString(Names[Wrap(Index)]);
	}

	FText SymbolName(int32 Index)
	{
		static const TCHAR* Names[Count] = { TEXT("Dreieck"), TEXT("Kreis"), TEXT("Quadrat"), TEXT("Kreuz"), TEXT("Stern"), TEXT("Raute"), TEXT("Balken"), TEXT("Welle") };
		return FText::FromString(Names[Wrap(Index)]);
	}

	FText RoleName(EHCRole Role)
	{
		return Role == EHCRole::Monster ? NSLOCTEXT("HC", "RoleMonster", "Der Wandler") : NSLOCTEXT("HC", "RoleCrew", "Crew");
	}

	const TCHAR* PhaseName(EHCMatchPhase Phase)
	{
		switch (Phase)
		{
		case EHCMatchPhase::Lobby:      return TEXT("Lobby");
		case EHCMatchPhase::Loading:    return TEXT("Loading");
		case EHCMatchPhase::Intro:      return TEXT("Intro");
		case EHCMatchPhase::RoleReveal: return TEXT("RoleReveal");
		case EHCMatchPhase::Playing:    return TEXT("Playing");
		case EHCMatchPhase::Meeting:    return TEXT("Meeting");
		case EHCMatchPhase::Ejection:   return TEXT("Ejection");
		case EHCMatchPhase::GameOver:   return TEXT("GameOver");
		}
		return TEXT("?");
	}
}
