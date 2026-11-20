#pragma once

#include "CoreMinimal.h"
#include "World/HCStationLayout.h"

/**
 * Where every loose thing of a station stands - the props put by hand, the generated odds and ends in corners and
 * along corridors - and a check that nothing stands inside something else. Pure data like the layout, so the check
 * runs as a unit test for every map (Tests/HCPlacementTests.cpp); AHCStationBuilder builds exactly what comes out
 * of here, so what the test sees is what the players get.
 */

/** Bounds of a prop model in its own space, cm (as the model file has them, before any turn or scale). */
struct HOWCOULDITBEYOU_API FHCPropShape
{
	FVector Min = FVector::ZeroVector;
	FVector Max = FVector::ZeroVector;
	/** Turn (yaw, degrees) that brings the front of the model to +X. */
	float OwnYaw = 0.f;
};

/** Looks up the model of a prop key; false = there is no such model and the prop is left out. */
using FHCShapeSource = TFunctionRef<bool(FName Key, FHCPropShape& OutShape)>;

/** A prop as it is really built. */
struct HOWCOULDITBEYOU_API FHCBuiltProp
{
	FName Key;
	/** Pivot in grid units (meters), after it was moved clear of the walls. */
	FVector2D Position = FVector2D::ZeroVector;
	/** Final turn of the model (yaw of the layout plus the model's own), degrees. */
	float Yaw = 0.f;
	/** Pivot above the floor, cm. */
	float Height = 0.f;
	float Scale = 1.f;
	/** Has an unseen box as collision. */
	bool bSolid = true;
	/** Came from the generator (corner groups, corridor things), not from the layout's own list. */
	bool bGenerated = false;
	FHCPropShape Shape;
};

/** A small warm light next to a prop. */
struct HOWCOULDITBEYOU_API FHCBuiltLamp
{
	/** Grid units (meters). */
	FVector2D Position = FVector2D::ZeroVector;
	/** cm above the floor. */
	float Height = 125.f;
	EHCSector Sector = EHCSector::Command;
	/** A battery lamp somebody left on: it keeps burning when the sector loses power. */
	bool bBattery = false;
};

enum class EHCSolidKind : uint8
{
	/** A prop model. */
	Prop,
	/** Something that is used: task station, charger, fuse box, emergency button, camera desk, plan table. */
	Device,
	/** A hatch of the ventilation shafts, and the room above it where the Wandler climbs out. */
	Vent,
	/** A wall as it is seen: the real wall pieces stand far in front of the wall line. */
	Wall,
	/** The opening of a door and the way through it. */
	DoorWay,
	/** Where a player stands to use a device. */
	StandSpot,
	/** A seat of the meeting, which is also a spawn point. */
	SpawnPoint,
	/** The point of a room players are rescued to (/unstuck, falling out of the world). */
	SafePoint
};

/** Something that takes up room: a box (or an upright cylinder) standing on the floor plan. All lengths in cm. */
struct HOWCOULDITBEYOU_API FHCSolid
{
	FString Name;
	EHCSolidKind Kind = EHCSolidKind::Prop;
	FVector2D Center = FVector2D::ZeroVector;
	/** Half size along its own X and Y. */
	FVector2D Half = FVector2D::ZeroVector;
	float Yaw = 0.f;
	float ZMin = 0.f;
	float ZMax = 0.f;
	/** A cylinder with the radius Half.X. */
	bool bRound = false;
	/** Props only: players cannot walk through it. */
	bool bBlocksWalking = true;
};

struct HOWCOULDITBEYOU_API FHCPlacement
{
	TArray<FHCBuiltProp> Props;
	TArray<FHCBuiltLamp> Lamps;
	/** Everything that takes up room, the props included. */
	TArray<FHCSolid> Solids;
	/** Generated things that were left out because their place was taken (not a problem, just for the log). */
	TArray<FString> LeftOut;
	/** How far the visible walls stand in front of the wall lines, cm. */
	double WallFaceDepth = 10.0;

	/**
	 * Places everything. Generated props give way: one that would stand in a wall, a device, a doorway or another
	 * prop is left out together with its group, so the generator can never cause an overlap.
	 */
	static FHCPlacement Build(const FHCStationLayout& Layout, FHCShapeSource Shapes, double WallFaceDepthCm);

	/** Human-readable problems: things that stand inside each other, in a wall, in a doorway, in the floor or ceiling. Empty = fine. */
	TArray<FString> FindProblems(const FHCStationLayout& Layout) const;

	/** A point of the space (grid units) where a player can stand: its middle, or the nearest free spot if something stands there. */
	FVector2D FindSafeSpot(const FHCStationLayout& Layout, int32 SpaceIndex, bool* bOutFound = nullptr) const;

	/** Is there room for a standing player (circle of RadiusCm) at this point (cm)? Props without collision do not count. */
	bool IsFreeForPlayer(const FVector2D& PointCm, double RadiusCm) const;

	/**
	 * Things that stand with their back to a wall are placed against the wall line. The real wall pieces stand much
	 * further out, so each thing moves forward until its back (BackCm behind its centre) is clear of them.
	 */
	static FVector2D OffWall(const FVector2D& Position, float YawDegrees, double BackCm, double WallFaceDepthCm);

	/** How deep two solids reach into each other, cm (0 or less: they are apart). Exposed for the unit test. */
	static double Penetration(const FHCSolid& A, const FHCSolid& B);

	/** The room a prop takes up. */
	static FHCSolid SolidOf(const FHCBuiltProp& Prop);
};
