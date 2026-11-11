#pragma once

#include "CoreMinimal.h"

/**
 * Pure data + logic for a greybox station, built on a 1 m grid.
 * Grid X maps to world X (cm = X * 100), grid Y maps to world Y.
 * No UObjects here, so it can be unit-tested without a world (see Tests/HCStationLayoutTests.cpp).
 */

enum class EHCSector : uint8
{
	Command,
	North,
	East,
	SouthWest
};

struct HOWCOULDITBEYOU_API FHCSpace
{
	FName Name;
	/** Cells [Min, Max) in grid units (1 unit = 100 cm). */
	FIntRect Rect;
	bool bCorridor = false;
	EHCSector Sector = EHCSector::Command;
	/** Ceiling height in cm. */
	float CeilingHeight = 400.f;
};

struct HOWCOULDITBEYOU_API FHCDoorDef
{
	FName Name;
	/**
	 * Horizontal door (bVertical = false): edge at y = Start.Y between rows Start.Y-1 and Start.Y,
	 * spanning cells x = Start.X .. Start.X + Width - 1.
	 * Vertical door (bVertical = true): edge at x = Start.X between columns Start.X-1 and Start.X,
	 * spanning cells y = Start.Y .. Start.Y + Width - 1.
	 */
	FIntPoint Start = FIntPoint::ZeroValue;
	bool bVertical = false;
	int32 Width = 3;
	bool bSabotageable = false;
};

struct HOWCOULDITBEYOU_API FHCPropDef
{
	FName Name;
	/** Position in grid units (meters), may be fractional. */
	FVector2D Position = FVector2D::ZeroVector;
	/** Direction the front faces (0 = +X). */
	float YawDegrees = 0.f;
};

/** A task station: one step of one task (docs/02 §9.1). Stands with its back to a wall. */
struct HOWCOULDITBEYOU_API FHCTaskStationDef
{
	FName TaskId;
	int32 StepIndex = 0;
	/** Centre of the console in grid units (meters). */
	FVector2D Position = FVector2D::ZeroVector;
	/** Direction the front faces (0 = +X). The player stands 1.1 m in front. */
	float YawDegrees = 0.f;
};

/** Fuse box of a power sector; hangs on a corridor wall of that sector (docs/01 §7 rule 7). */
struct HOWCOULDITBEYOU_API FHCFuseBoxDef
{
	FName Name;
	EHCSector Sector = EHCSector::North;
	/** Centre of the box in grid units (meters). */
	FVector2D Position = FVector2D::ZeroVector;
	/** Direction the front faces (0 = +X). */
	float YawDegrees = 0.f;
};

struct HOWCOULDITBEYOU_API FHCStationLayout
{
	FName MapName;
	int32 SizeX = 0;
	int32 SizeY = 0;
	TArray<FHCSpace> Spaces;
	TArray<FHCDoorDef> Doors;
	TArray<FHCPropDef> ChargingStations;
	/** Emergency button(s); must stand in the meeting room (docs/02 §8.1). */
	TArray<FHCPropDef> EmergencyButtons;
	TArray<FHCFuseBoxDef> FuseBoxes;
	TArray<FHCTaskStationDef> TaskStations;
	FName SpawnRoom;

	/** Door opening height in cm (docs/04 §2.1: >= 230). */
	float DoorHeight = 240.f;
	/** Wall thickness in cm. */
	float WallThickness = 20.f;

	const FHCSpace* FindSpace(FName Name) const;

	/** Kepler-7 greybox after docs/01 §11.1 (approx. 90 x 76 m). */
	static FHCStationLayout MakeKepler7();

	/** Small waiting room for the lobby map (one room, no doors). */
	static FHCStationLayout MakeLobby();

	/** By name ("Kepler7", "Lobby"); unknown names give Kepler7. */
	static FHCStationLayout MakeByName(FName Name);
};

/** Key for a grid edge: (X, Y, 0 = horizontal / 1 = vertical). */
using FHCEdgeKey = FIntVector;

struct HOWCOULDITBEYOU_API FHCWallEdge
{
	/** Horizontal edge: between (X, Y-1) and (X, Y). Vertical edge: between (X-1, Y) and (X, Y). */
	FIntPoint Cell = FIntPoint::ZeroValue;
	bool bVertical = false;
	/** Wall height in cm (max of the ceilings on both sides). */
	float Height = 0.f;
	/** Index into FHCStationLayout::Doors, or INDEX_NONE for a solid wall. */
	int32 DoorIndex = INDEX_NONE;
};

class HOWCOULDITBEYOU_API FHCStationGrid
{
public:
	explicit FHCStationGrid(const FHCStationLayout& InLayout);

	const FHCStationLayout& GetLayout() const { return Layout; }

	/** Index into Layout.Spaces, or INDEX_NONE for empty/outside. */
	int32 GetSpaceAt(int32 X, int32 Y) const;

	/** True if a player can walk from space A to space B without a door (same space, or both corridors). */
	bool IsOpenBetween(int32 A, int32 B) const;

	/** All edges that need geometry: solid walls and door edges (DoorIndex set). */
	void CollectEdges(TArray<FHCWallEdge>& OutEdges) const;

	/** Returns human-readable problems. Empty = layout is valid. */
	TArray<FString> Validate() const;

	/** Number of doors that lead into the given space (rooms need >= 2, docs/01 §11.1). */
	int32 CountDoorsOfSpace(int32 SpaceIndex) const;

	/** The two spaces a door connects (INDEX_NONE if the door edge is invalid). */
	void GetDoorSpaces(int32 DoorIndex, int32& OutA, int32& OutB) const;

	/** Can a player step from cell P to the 4-neighbour cell N? */
	bool CanStep(const FIntPoint& P, const FIntPoint& N, bool bSabotageableDoorsOpen = true) const;

	/** Breadth-first distances (in cells = meters) from Start to every cell; -1 = unreachable. */
	TArray<int32> ComputeDistances(const FIntPoint& Start, bool bSabotageableDoorsOpen = true) const;

	/** Shortest walk in meters between two cells (4-neighbour, so slightly longer than reality), -1 if none. */
	int32 ShortestPath(const FIntPoint& From, const FIntPoint& To) const;

	/** Center cell of a space. */
	FIntPoint GetSpaceCenter(FName SpaceName) const;

private:
	static FHCEdgeKey MakeKey(int32 X, int32 Y, bool bVertical) { return FHCEdgeKey(X, Y, bVertical ? 1 : 0); }

	const FHCStationLayout& Layout;
	TArray<int32> Cells;
	TArray<FString> BuildProblems;
	TMap<FHCEdgeKey, int32> DoorEdges;
};
