#pragma once

#include "CoreMinimal.h"

struct FHCStationLayout;

/** One door on a drawn plan. */
struct HOWCOULDITBEYOU_API FHCPlanDoor
{
	/** The id of the door actor (AHCDoor::DoorId). */
	FName Id;
	/** Both ends of the opening in grid units (meters). */
	FVector2D From = FVector2D::ZeroVector;
	FVector2D To = FVector2D::ZeroVector;
	bool bSabotageable = false;

	FVector2D Middle() const { return (From + To) * 0.5; }
};

/**
 * What a drawn plan of the station needs besides the layout itself: the floor of every space as rectangles that
 * do not overlap, the walls as long strokes with the doors left open, and the doors. Worked out once per map;
 * the minimap of the HUD and the station map (M) draw from it, so both show the same plan.
 */
struct HOWCOULDITBEYOU_API FHCPlanShape
{
	/** Floor of each space (same order as FHCStationLayout::Spaces): no two rectangles overlap, holes are left out. */
	TArray<TArray<FIntRect>> Floors;
	/** Wall strokes in grid units. */
	TArray<TPair<FVector2D, FVector2D>> Walls;
	TArray<FHCPlanDoor> Doors;

	/** Was it built for this layout? */
	bool IsFor(const FHCStationLayout& Layout) const;
	void Build(const FHCStationLayout& Layout);

	const FHCPlanDoor* FindDoor(FName Id) const;

private:
	FName MapName;
	FIntPoint Size = FIntPoint::ZeroValue;
	int32 NumSpaces = 0;
};
