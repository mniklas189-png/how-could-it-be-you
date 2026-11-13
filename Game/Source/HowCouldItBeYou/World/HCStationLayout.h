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
	/** Cells [Min, Max) in grid units (1 unit = 100 cm). The main part: its middle carries the floor label and must be walkable. */
	FIntRect Rect;
	/** Further parts of the same space: rooms that are not plain boxes (L, T, cross, stepped bow). */
	TArray<FIntRect> ExtraRects;
	/** Cells cut out of the parts again: solid blocks inside the room (reactor core, container stacks), walled all round. */
	TArray<FIntRect> Holes;
	bool bCorridor = false;
	EHCSector Sector = EHCSector::Command;
	/** Ceiling height in cm. */
	float CeilingHeight = 400.f;
	/** Colour of the ceiling lights (cold white by default) and their brightness (dark hangar, dim monitor room). */
	FLinearColor LightColor = FLinearColor(0.82f, 0.9f, 1.f);
	float LightScale = 1.f;

	/** Rect followed by ExtraRects. */
	TArray<FIntRect, TInlineAllocator<4>> GetRects() const
	{
		TArray<FIntRect, TInlineAllocator<4>> Rects;
		Rects.Add(Rect);
		Rects.Append(ExtraRects);
		return Rects;
	}

	/** Is the cell part of the space (in one of the parts and in no hole)? */
	bool ContainsCell(int32 X, int32 Y) const
	{
		const FIntPoint P(X, Y);
		auto Inside = [&P](const FIntRect& R) { return P.X >= R.Min.X && P.X < R.Max.X && P.Y >= R.Min.Y && P.Y < R.Max.Y; };
		if (Holes.ContainsByPredicate(Inside))
		{
			return false;
		}
		return Inside(Rect) || ExtraRects.ContainsByPredicate(Inside);
	}
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

/** A prop from the model packs put by hand (AHCStationBuilder::BuildProps); only seen with the packs. */
struct HOWCOULDITBEYOU_API FHCPlacedProp
{
	/** Key in UHCModelSet::Props (Drum, Bollard, ...). */
	FName Key;
	/** Pivot in grid units (meters). */
	FVector2D Position = FVector2D::ZeroVector;
	/** Turn of the prop's long side (X), degrees. */
	float YawDegrees = 0.f;
	/** A warm standing lamp next to it (the bollards), on the power of its sector. */
	bool bLamp = false;
	/** Height of the pivot above the floor in cm (wall lights, things on a shelf). */
	float Height = 0.f;
	/** Even scale of the model. */
	float Scale = 1.f;
	/** Gets an unseen box as collision (off for flat things on the floor and things on walls). */
	bool bSolid = true;
};

/** A light put by hand for a set piece (reactor core, hologram glow); on the power of its sector. */
struct HOWCOULDITBEYOU_API FHCPlacedLight
{
	/** Grid units (meters). */
	FVector2D Position = FVector2D::ZeroVector;
	/** cm above the floor. */
	float Height = 200.f;
	FLinearColor Color = FLinearColor::White;
	float Candela = 40.f;
	float Radius = 800.f;
	/** Breathes slowly (the reactor core). */
	bool bPulse = false;
};

/** A security camera (docs/01 §6): hangs in a corridor or open hub, never in a task room; watched from the security console. */
struct HOWCOULDITBEYOU_API FHCCameraDef
{
	FName Id;
	/** What the monitor writes under the picture. */
	FText Label;
	/** Grid units (meters): where the camera hangs. */
	FVector2D Position = FVector2D::ZeroVector;
	/** cm above the floor. */
	float Height = 250.f;
	/** Where it looks (0 = +X), and how far down. */
	float Yaw = 0.f;
	float Pitch = -18.f;
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
	/** Props put by hand (themed furniture of a room). */
	TArray<FHCPlacedProp> PlacedProps;
	/** Rooms get generated groups in their corners (the same in every room: off where rooms are furnished by hand). */
	bool bAutoRoomProps = true;
	/** Corridors get small things along their walls and loose cables. */
	bool bAutoCorridorProps = true;
	TArray<FHCPlacedLight> PlacedLights;
	TArray<FHCCameraDef> Cameras;
	/** Desks with the camera monitors (front = the side the player stands on). */
	TArray<FHCPropDef> SecurityConsoles;
	/** Tables with the station plan that counts the people in every room (Among Us "Admin"). */
	TArray<FHCPropDef> AdminTables;
	FName SpawnRoom;

	/** Door opening height in cm (docs/04 §2.1: >= 230). */
	float DoorHeight = 240.f;
	/** Wall thickness in cm. */
	float WallThickness = 20.f;

	const FHCSpace* FindSpace(FName Name) const;

	/** Kepler-7 greybox after docs/01 §11.1 (approx. 90 x 76 m). */
	static FHCStationLayout MakeKepler7();

	/** Freighter Ikarus (docs/01 §11.3, redrawn): rooms of many shapes, security cameras and an admin table. */
	static FHCStationLayout MakeIkarus();

	/** Small waiting room for the lobby map (one room, no doors). */
	static FHCStationLayout MakeLobby();

	/** By name ("Kepler7", "Ikarus", "Lobby"); unknown names give Kepler7. */
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
