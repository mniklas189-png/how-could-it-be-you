#pragma once

#include "CoreMinimal.h"

/** A map the host can pick in the lobby. */
struct HOWCOULDITBEYOU_API FHCMapInfo
{
	/** Layout name of AHCStationBuilder (FHCStationLayout::MakeByName) and the saved choice. */
	FName Id;
	/** Package of the map (ServerTravel). */
	FString Path;
	FText Name;
	FText Description;
	/** Short place line, e.g. on the meeting screen ("Kepler-7"). */
	FText Place;
};

/** The maps of a round, in the order of the lobby buttons (index = the replicated choice). */
namespace HCMaps
{
	HOWCOULDITBEYOU_API const TArray<FHCMapInfo>& All();
	/** Index of a map by id or by package/short name ("Ikarus", "Map_Ikarus"), INDEX_NONE if unknown. */
	HOWCOULDITBEYOU_API int32 Find(const FString& IdOrName);
	/** A valid entry for any index (out of range gives the first map). */
	HOWCOULDITBEYOU_API const FHCMapInfo& Get(int32 Index);
	/** The entry of a station layout (Kepler7 for unknown names). */
	HOWCOULDITBEYOU_API const FHCMapInfo& ForLayout(FName LayoutName);
}
