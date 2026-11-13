#include "World/HCMapCatalog.h"

#include "Core/HCGameSettings.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/PackageName.h"

namespace HCMaps
{
	const TArray<FHCMapInfo>& All()
	{
		// Built on first use: the texts follow the language that is active then; the lobby asks again every refresh
		static TArray<FHCMapInfo> Maps;
		static FString Culture;
		const FString Now = FInternationalization::Get().GetCurrentCulture()->GetName();
		if (Maps.IsEmpty() || Culture != Now)
		{
			Culture = Now;
			Maps.Reset();
			Maps.Add({ TEXT("Kepler7"), UHCGameSettings::Get()->MatchMap,
				NSLOCTEXT("HC", "MapKepler7", "Kepler-7"),
				NSLOCTEXT("HC", "MapKepler7Info", "Die Ring-Station: Gänge im Ring um das Atrium, gerade Wege, viele Abkürzungen."),
				NSLOCTEXT("HC", "MapKepler7Place", "Kepler-7") });
			Maps.Add({ TEXT("Ikarus"), TEXT("/Game/HowCouldItBeYou/Maps/Ikarus/Map_Ikarus"),
				NSLOCTEXT("HC", "MapIkarus", "Ikarus"),
				NSLOCTEXT("HC", "MapIkarusInfo", "Der Frachter: verwinkelte Räume, Container-Gassen, Kameras und ein Lageplan, der zählt, wo wie viele sind."),
				NSLOCTEXT("HC", "MapIkarusPlace", "Frachter Ikarus") });
		}
		return Maps;
	}

	int32 Find(const FString& IdOrName)
	{
		const TArray<FHCMapInfo>& Maps = All();
		for (int32 Index = 0; Index < Maps.Num(); ++Index)
		{
			const FString Short = FPackageName::GetShortName(Maps[Index].Path);
			if (IdOrName.Equals(Maps[Index].Id.ToString(), ESearchCase::IgnoreCase) || IdOrName.Equals(Short, ESearchCase::IgnoreCase)
				|| IdOrName.EndsWith(Short, ESearchCase::IgnoreCase))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	const FHCMapInfo& Get(int32 Index)
	{
		const TArray<FHCMapInfo>& Maps = All();
		return Maps.IsValidIndex(Index) ? Maps[Index] : Maps[0];
	}

	const FHCMapInfo& ForLayout(FName LayoutName)
	{
		return Get(Find(LayoutName.ToString()));
	}
}
