#include "World/HCPlacement.h"

// A named namespace with names of its own: in a unity build everything at file level is seen by every other file
namespace HCPlace
{
	constexpr double GridCm = 100.0;
	/** How far two things may reach into each other before it counts, cm: the bounds of a model are a little generous. */
	constexpr double OverlapSlack = 3.0;
	/** A standing player: radius and height of the room they need, cm. */
	constexpr double PlayerRadius = 45.0;
	constexpr double PlayerHeight = 180.0;

	FVector2D Turned(const FVector2D& V, float YawDegrees)
	{
		const double Rad = FMath::DegreesToRadians(static_cast<double>(YawDegrees));
		const double C = FMath::Cos(Rad), S = FMath::Sin(Rad);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}

	/** Models that are upright cylinders: their square bounds would claim corners they do not fill. */
	bool IsRoundKey(FName Key)
	{
		static const FName Round[] = { TEXT("Drum"), TEXT("Nitrogen"), TEXT("OxygenTank"), TEXT("TankBig"), TEXT("EnergyNode"), TEXT("Bollard"), TEXT("Monolith") };
		for (const FName& Name : Round)
		{
			if (Key == Name)
			{
				return true;
			}
		}
		return false;
	}

	/** Lies flat on the floor and can be walked over (the loose cables). */
	bool IsFlatOnFloor(const FHCSolid& Solid)
	{
		return Solid.Kind == EHCSolidKind::Prop && !Solid.bBlocksWalking && Solid.ZMax < 10.f;
	}

	/** Which pairs of kinds must not reach into each other. */
	bool Matters(const FHCSolid& A, const FHCSolid& B)
	{
		using K = EHCSolidKind;
		const FHCSolid& First = A.Kind <= B.Kind ? A : B;
		const FHCSolid& Second = A.Kind <= B.Kind ? B : A;
		switch (First.Kind)
		{
		case K::Prop:
			switch (Second.Kind)
			{
			case K::Prop:
				// A loose cable on the floor may run under the edge of a crate: its bounds are a wide rectangle
				// around a thin curve, and nobody takes that for two things in each other
				return !IsFlatOnFloor(First) && !IsFlatOnFloor(Second);
			case K::Device: case K::Vent: case K::Wall:
				return true;
			default:
				// Ways and standing spots only have to be free of things one cannot walk through
				return First.bBlocksWalking;
			}
		case K::Device:
			return Second.Kind != K::Wall; // devices stand against walls on purpose
		case K::Vent:
			return Second.Kind != K::SafePoint;
		default:
			return false;
		}
	}

	/** Overlap of two solids in the floor plan, cm (0 or less: apart). */
	double Penetration2D(const FHCSolid& A, const FHCSolid& B)
	{
		if (A.bRound && B.bRound)
		{
			return A.Half.X + B.Half.X - FVector2D::Distance(A.Center, B.Center);
		}
		if (A.bRound != B.bRound)
		{
			const FHCSolid& Circle = A.bRound ? A : B;
			const FHCSolid& Box = A.bRound ? B : A;
			const FVector2D Local = Turned(Circle.Center - Box.Center, -Box.Yaw);
			const FVector2D Nearest(FMath::Clamp(Local.X, -Box.Half.X, Box.Half.X), FMath::Clamp(Local.Y, -Box.Half.Y, Box.Half.Y));
			const double Distance = FVector2D::Distance(Local, Nearest);
			if (Distance > UE_KINDA_SMALL_NUMBER)
			{
				return Circle.Half.X - Distance;
			}
			// The centre is inside the box: the way out through the nearest side, plus the radius
			return Circle.Half.X + FMath::Min(Box.Half.X - FMath::Abs(Local.X), Box.Half.Y - FMath::Abs(Local.Y));
		}
		// Two boxes: the smallest overlap along the four side directions (separating axes)
		const FVector2D Axes[4] = { Turned(FVector2D(1.0, 0.0), A.Yaw), Turned(FVector2D(0.0, 1.0), A.Yaw), Turned(FVector2D(1.0, 0.0), B.Yaw), Turned(FVector2D(0.0, 1.0), B.Yaw) };
		const FVector2D Between = B.Center - A.Center;
		double Smallest = TNumericLimits<double>::Max();
		for (const FVector2D& Axis : Axes)
		{
			const double ReachA = FMath::Abs(FVector2D::DotProduct(Axes[0], Axis)) * A.Half.X + FMath::Abs(FVector2D::DotProduct(Axes[1], Axis)) * A.Half.Y;
			const double ReachB = FMath::Abs(FVector2D::DotProduct(Axes[2], Axis)) * B.Half.X + FMath::Abs(FVector2D::DotProduct(Axes[3], Axis)) * B.Half.Y;
			Smallest = FMath::Min(Smallest, ReachA + ReachB - FMath::Abs(FVector2D::DotProduct(Between, Axis)));
		}
		return Smallest;
	}

	FHCSolid MakeBox(const FString& Name, EHCSolidKind Kind, const FVector2D& CenterCm, float Yaw, double HalfX, double HalfY, double ZMin, double ZMax)
	{
		FHCSolid S;
		S.Name = Name;
		S.Kind = Kind;
		S.Center = CenterCm;
		S.Half = FVector2D(HalfX, HalfY);
		S.Yaw = Yaw;
		S.ZMin = static_cast<float>(ZMin);
		S.ZMax = static_cast<float>(ZMax);
		return S;
	}

	FHCSolid MakeRound(const FString& Name, EHCSolidKind Kind, const FVector2D& CenterCm, double Radius, double ZMin, double ZMax)
	{
		FHCSolid S = MakeBox(Name, Kind, CenterCm, 0.f, Radius, Radius, ZMin, ZMax);
		S.bRound = true;
		return S;
	}

	FVector2D Facing(float YawDegrees)
	{
		return Turned(FVector2D(1.0, 0.0), YawDegrees);
	}

	FString RoomAt(const FHCStationLayout& Layout, const FHCStationGrid& Grid, const FVector2D& Meters)
	{
		const int32 Space = Grid.GetSpaceAt(FMath::FloorToInt32(Meters.X), FMath::FloorToInt32(Meters.Y));
		return Layout.Spaces.IsValidIndex(Space) ? Layout.Spaces[Space].Name.ToString() : FString(TEXT("nowhere"));
	}

	/** Everything that does not come from a prop model: devices, hatches, walls, doorways, standing spots, seats. */
	void AddFixedSolids(const FHCStationLayout& Layout, const FHCStationGrid& Grid, double FaceDepth, TArray<FHCSolid>& Out)
	{
		// A device with its back to a wall: X is its depth (the front looks along +X), Y its width. ShiftCm moves the
		// box along the front direction when the device is not centred on its origin.
		auto Device = [&](const FString& Name, const FVector2D& Position, float Yaw, double BackCm, double HalfDepth, double HalfWidth, double ZMin, double ZMax, double ShiftCm, double StandCm)
		{
			const FVector2D At = FHCPlacement::OffWall(Position, Yaw, BackCm, FaceDepth) * GridCm;
			Out.Add(MakeBox(Name, EHCSolidKind::Device, At + Facing(Yaw) * ShiftCm, Yaw, HalfDepth, HalfWidth, ZMin, ZMax));
			if (StandCm > 0.0)
			{
				// Free area of 120 x 120 cm where the player stands (docs/04 §2.1)
				Out.Add(MakeBox(FString::Printf(TEXT("the standing spot of %s"), *Name), EHCSolidKind::StandSpot, At + Facing(Yaw) * StandCm, Yaw, 60.0, 60.0, 0.0, PlayerHeight));
			}
		};
		for (const FHCTaskStationDef& Def : Layout.TaskStations)
		{
			Device(FString::Printf(TEXT("task station %s#%d"), *Def.TaskId.ToString(), Def.StepIndex), Def.Position, Def.YawDegrees, 30.0, 30.0, 60.0, 0.0, 135.0, 0.0, 110.0);
		}
		for (const FHCPropDef& Def : Layout.ChargingStations)
		{
			Device(FString::Printf(TEXT("charging station %s"), *Def.Name.ToString()), Def.Position, Def.YawDegrees, 20.0, 26.0, 50.0, 0.0, 140.0, 6.0, 110.0);
		}
		for (const FHCFuseBoxDef& Def : Layout.FuseBoxes)
		{
			Device(FString::Printf(TEXT("fuse box %s"), *Def.Name.ToString()), Def.Position, Def.YawDegrees, 4.0, 15.0, 30.0, 70.0, 180.0, 11.0, 100.0);
		}
		for (const FHCPropDef& Def : Layout.SecurityConsoles)
		{
			Device(FString::Printf(TEXT("camera desk %s"), *Def.Name.ToString()), Def.Position, Def.YawDegrees, 35.0, 35.0, 130.0, 0.0, 246.0, 0.0, 110.0);
		}
		for (const FHCPropDef& Def : Layout.EmergencyButtons)
		{
			Out.Add(MakeBox(FString::Printf(TEXT("emergency button %s"), *Def.Name.ToString()), EHCSolidKind::Device, Def.Position * GridCm, Def.YawDegrees, 35.0, 38.0, 0.0, 110.0));
		}
		for (const FHCPropDef& Def : Layout.AdminTables)
		{
			const FString Name = FString::Printf(TEXT("plan table %s"), *Def.Name.ToString());
			Out.Add(MakeRound(Name, EHCSolidKind::Device, Def.Position * GridCm, 95.0, 0.0, 110.0));
			Out.Add(MakeBox(FString::Printf(TEXT("the standing spot of %s"), *Name), EHCSolidKind::StandSpot, Def.Position * GridCm + Facing(Def.YawDegrees) * 175.0, Def.YawDegrees, 60.0, 60.0, 0.0, PlayerHeight));
		}
		for (const FHCVentDef& Def : Layout.Vents)
		{
			// The hatch itself is flat, but the Wandler climbs out right above it: that room stays free
			Out.Add(MakeBox(FString::Printf(TEXT("vent %s"), *Def.Id.ToString()), EHCSolidKind::Vent, Def.Position * GridCm, Def.YawDegrees, 50.0, 50.0, 0.0, PlayerHeight));
		}

		// Walls as they are seen, and the ways through the doors
		TArray<FHCWallEdge> Edges;
		Grid.CollectEdges(Edges);
		for (const FHCWallEdge& Edge : Edges)
		{
			const bool bDoor = Edge.DoorIndex != INDEX_NONE;
			const FVector2D Center = Edge.bVertical ? FVector2D(Edge.Cell.X, Edge.Cell.Y + 0.5) : FVector2D(Edge.Cell.X + 0.5, Edge.Cell.Y);
			const double ZMin = bDoor ? Layout.DoorHeight : 0.0;
			if (Edge.Height - ZMin > 1.0)
			{
				Out.Add(MakeBox(FString::Printf(TEXT("the wall at (%.1f, %.1f)"), Center.X, Center.Y), EHCSolidKind::Wall, Center * GridCm, 0.f,
					Edge.bVertical ? FaceDepth : 50.0, Edge.bVertical ? 50.0 : FaceDepth, ZMin, Edge.Height));
			}
		}
		for (const FHCDoorDef& Door : Layout.Doors)
		{
			const double HalfOpening = (Door.Width * GridCm - Layout.WallThickness) * 0.5;
			const FVector2D Center = Door.bVertical ? FVector2D(Door.Start.X, Door.Start.Y + Door.Width * 0.5) : FVector2D(Door.Start.X + Door.Width * 0.5, Door.Start.Y);
			// 90 cm of free way on both sides of the wall line
			Out.Add(MakeBox(FString::Printf(TEXT("the doorway %s"), *Door.Name.ToString()), EHCSolidKind::DoorWay, Center * GridCm, 0.f,
				Door.bVertical ? 90.0 : HalfOpening, Door.bVertical ? HalfOpening : 90.0, 0.0, Layout.DoorHeight));
		}

		// The eight seats of the meeting (AHCStationBuilder::GetSpawnTransforms): 3 m around the middle of the room
		if (const FHCSpace* Room = Layout.FindSpace(Layout.SpawnRoom))
		{
			const FVector2D Middle((Room->Rect.Min.X + Room->Rect.Max.X) * 0.5, (Room->Rect.Min.Y + Room->Rect.Max.Y) * 0.5);
			for (int32 Seat = 0; Seat < 8; ++Seat)
			{
				const double Angle = 2.0 * UE_DOUBLE_PI * Seat / 8.0;
				Out.Add(MakeRound(FString::Printf(TEXT("seat %d of the meeting room"), Seat + 1), EHCSolidKind::SpawnPoint,
					(Middle + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 3.0) * GridCm, PlayerRadius, 0.0, PlayerHeight));
			}
		}
	}
}

double FHCPlacement::Penetration(const FHCSolid& A, const FHCSolid& B)
{
	using namespace HCPlace;
	const double Upright = FMath::Min(A.ZMax, B.ZMax) - FMath::Max(A.ZMin, B.ZMin);
	if (Upright <= 0.0)
	{
		return Upright;
	}
	return FMath::Min(Upright, Penetration2D(A, B));
}

FVector2D FHCPlacement::OffWall(const FVector2D& Position, float YawDegrees, double BackCm, double WallFaceDepthCm)
{
	using namespace HCPlace;
	const FVector2D Ahead = Facing(YawDegrees);
	const bool bAlongX = FMath::Abs(Ahead.X) > FMath::Abs(Ahead.Y);
	const double Coord = bAlongX ? Position.X : Position.Y;
	// Distance from the centre back to the wall line behind it
	const double Gap = (bAlongX ? Ahead.X : Ahead.Y) > 0.0 ? Coord - FMath::FloorToDouble(Coord) : FMath::CeilToDouble(Coord) - Coord;
	const double Back = Gap * GridCm - BackCm;
	const double Need = WallFaceDepthCm + 1.5;
	return Back < Need ? Position + Ahead * ((Need - Back) / GridCm) : Position;
}

FHCSolid FHCPlacement::SolidOf(const FHCBuiltProp& Prop)
{
	using namespace HCPlace;
	const FVector Size = (Prop.Shape.Max - Prop.Shape.Min) * Prop.Scale;
	const FVector Middle = (Prop.Shape.Min + Prop.Shape.Max) * 0.5 * Prop.Scale;
	FHCSolid S = MakeBox(Prop.Key.ToString(), EHCSolidKind::Prop, Prop.Position * GridCm + Turned(FVector2D(Middle.X, Middle.Y), Prop.Yaw), Prop.Yaw,
		Size.X * 0.5, Size.Y * 0.5, Prop.Height + Prop.Shape.Min.Z * Prop.Scale, Prop.Height + Prop.Shape.Max.Z * Prop.Scale);
	S.bBlocksWalking = Prop.bSolid;
	if (IsRoundKey(Prop.Key))
	{
		S.bRound = true;
		S.Half = FVector2D(FMath::Max(S.Half.X, S.Half.Y));
	}
	return S;
}

FHCPlacement FHCPlacement::Build(const FHCStationLayout& Layout, FHCShapeSource Shapes, double WallFaceDepthCm)
{
	using namespace HCPlace;
	FHCPlacement Out;
	Out.WallFaceDepth = WallFaceDepthCm;
	const FHCStationGrid Grid(Layout);
	AddFixedSolids(Layout, Grid, WallFaceDepthCm, Out.Solids);

	// Nothing may reach into the visible walls: the real wall pieces stand far in front of the wall line
	const double WallClear = (WallFaceDepthCm + 2.0) / GridCm;

	// X, Y in metres of the layout, Z in cm above the floor; Yaw turns the long side (X) of the prop.
	// False = there is no model for the key.
	auto Resolve = [&](FName Key, const FVector2D& Where, float Yaw, double Z, bool bSolid, double Scale, bool bGenerated, FHCBuiltProp& Prop)
	{
		FHCPropShape Shape;
		if (!Shapes(Key, Shape))
		{
			return false;
		}
		Prop.Key = Key;
		Prop.Yaw = Yaw + Shape.OwnYaw;
		Prop.Height = static_cast<float>(Z);
		Prop.Scale = static_cast<float>(Scale);
		Prop.bSolid = bSolid;
		Prop.bGenerated = bGenerated;
		Prop.Shape = Shape;
		// The whole footprint stays inside the visible walls of its space; a prop too close is moved out, not turned
		FVector2D Spot = Where;
		const int32 SpaceIndex = Grid.GetSpaceAt(FMath::FloorToInt32(Where.X), FMath::FloorToInt32(Where.Y));
		if (Layout.Spaces.IsValidIndex(SpaceIndex))
		{
			FBox2D Footprint(ForceInit);
			for (const double X : { Shape.Min.X, Shape.Max.X })
			{
				for (const double Y : { Shape.Min.Y, Shape.Max.Y })
				{
					Footprint += Turned(FVector2D(X, Y) * Scale, Prop.Yaw);
				}
			}
			// The part of the space the prop stands in (rooms of several parts)
			FIntRect R = Layout.Spaces[SpaceIndex].Rect;
			for (const FIntRect& Part : Layout.Spaces[SpaceIndex].GetRects())
			{
				if (Where.X >= Part.Min.X && Where.X < Part.Max.X && Where.Y >= Part.Min.Y && Where.Y < Part.Max.Y)
				{
					R = Part;
					break;
				}
			}
			const double MinX = R.Min.X + WallClear - Footprint.Min.X / GridCm, MaxX = R.Max.X - WallClear - Footprint.Max.X / GridCm;
			const double MinY = R.Min.Y + WallClear - Footprint.Min.Y / GridCm, MaxY = R.Max.Y - WallClear - Footprint.Max.Y / GridCm;
			if (MinX <= MaxX)
			{
				Spot.X = FMath::Clamp(Spot.X, MinX, MaxX);
			}
			if (MinY <= MaxY)
			{
				Spot.Y = FMath::Clamp(Spot.Y, MinY, MaxY);
			}
		}
		Prop.Position = Spot;
		return true;
	};
	auto Named = [&](const FHCBuiltProp& Prop)
	{
		FHCSolid Solid = SolidOf(Prop);
		Solid.Name = FString::Printf(TEXT("%s at (%.1f, %.1f) in %s"), *Prop.Key.ToString(), Prop.Position.X, Prop.Position.Y, *RoomAt(Layout, Grid, Prop.Position));
		return Solid;
	};
	auto Keep = [&](const FHCBuiltProp& Prop)
	{
		Out.Props.Add(Prop);
		Out.Solids.Add(Named(Prop));
	};
	auto StandLamp = [&](const FVector2D& Where, EHCSector Sector, double Height, bool bBattery = false)
	{
		FHCBuiltLamp Lamp;
		Lamp.Position = Where;
		Lamp.Height = static_cast<float>(Height);
		Lamp.Sector = Sector;
		Lamp.bBattery = bBattery;
		Out.Lamps.Add(Lamp);
	};

	// --- Props put by hand (the lobby, the furnished rooms)
	for (const FHCPlacedProp& Def : Layout.PlacedProps)
	{
		const bool bBollard = Def.Key == TEXT("Bollard");
		const double Scale = bBollard && FMath::IsNearlyEqual(Def.Scale, 1.f) ? 2.4 : Def.Scale;
		FHCBuiltProp Prop;
		if (!Resolve(Def.Key, Def.Position, Def.YawDegrees, Def.Height, Def.bSolid, Scale, false, Prop))
		{
			continue;
		}
		Keep(Prop);
		const int32 SpaceIndex = Grid.GetSpaceAt(FMath::FloorToInt32(Def.Position.X), FMath::FloorToInt32(Def.Position.Y));
		if (Def.bLamp && Layout.Spaces.IsValidIndex(SpaceIndex))
		{
			// The light itself sits a little towards the middle of the room
			const FIntRect& R = Layout.Spaces[SpaceIndex].Rect;
			const FVector2D Middle((R.Min.X + R.Max.X) * 0.5, (R.Min.Y + R.Max.Y) * 0.5);
			StandLamp(Def.Position + (Middle - Def.Position).GetSafeNormal() * 0.6, Layout.Spaces[SpaceIndex].Sector, 125.0);
		}
	}

	// --- Generated things. A group is placed whole or not at all: if one of its pieces would stand in something
	//     that is already there, the group is left out. The pieces of one group are not checked against each other
	//     here (FindProblems does that, so a badly drawn group shows up in the test instead of vanishing).
	struct FItem
	{
		FName Key;
		FVector2D Where;
		float Yaw = 0.f;
		double Z = 0.0;
		bool bSolid = true;
		double Scale = 1.0;
	};
	auto PutGroup = [&](const FString& What, const TArray<FItem>& Items)
	{
		TArray<FHCBuiltProp> Group;
		for (const FItem& Item : Items)
		{
			FHCBuiltProp Prop;
			if (Resolve(Item.Key, Item.Where, Item.Yaw, Item.Z, Item.bSolid, Item.Scale, true, Prop))
			{
				Group.Add(Prop);
			}
		}
		for (const FHCBuiltProp& Prop : Group)
		{
			const FHCSolid Solid = Named(Prop);
			for (const FHCSolid& Other : Out.Solids)
			{
				if (Matters(Solid, Other) && Penetration(Solid, Other) > OverlapSlack)
				{
					Out.LeftOut.Add(FString::Printf(TEXT("%s: %s would stand in %s"), *What, *Solid.Name, *Other.Name));
					return false;
				}
			}
		}
		for (const FHCBuiltProp& Prop : Group)
		{
			Keep(Prop);
		}
		return Group.Num() > 0;
	};
	// Is a door or anything you use within Margin metres of the rectangle?
	auto Busy = [&](const FVector2D& A, const FVector2D& B, double Margin)
	{
		const FBox2D Rect(FVector2D(FMath::Min(A.X, B.X) - Margin, FMath::Min(A.Y, B.Y) - Margin), FVector2D(FMath::Max(A.X, B.X) + Margin, FMath::Max(A.Y, B.Y) + Margin));
		for (const FHCTaskStationDef& Def : Layout.TaskStations) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCPropDef& Def : Layout.ChargingStations) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCPropDef& Def : Layout.EmergencyButtons) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCFuseBoxDef& Def : Layout.FuseBoxes) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCPropDef& Def : Layout.SecurityConsoles) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCPropDef& Def : Layout.AdminTables) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCVentDef& Def : Layout.Vents) { if (Rect.IsInside(Def.Position)) { return true; } }
		for (const FHCDoorDef& Door : Layout.Doors)
		{
			const FVector2D Start(Door.Start.X, Door.Start.Y);
			const FVector2D Along = Door.bVertical ? FVector2D(0.0, Door.Width) : FVector2D(Door.Width, 0.0);
			for (const double T : { 0.0, 0.5, 1.0 })
			{
				if (Rect.IsInside(Start + Along * T)) { return true; }
			}
		}
		return false;
	};

	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num(); ++SpaceIndex)
	{
		const FHCSpace& Space = Layout.Spaces[SpaceIndex];
		const FIntRect& R = Space.Rect;
		if (Space.bCorridor ? !Layout.bAutoCorridorProps : !Layout.bAutoRoomProps)
		{
			continue;
		}
		if (!Space.bCorridor)
		{
			// Rooms: a small group in every corner that is free. Nobody has to pass through a corner.
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const bool bRight = (Corner & 1) != 0, bLow = (Corner & 2) != 0;
				const FVector2D Origin(bRight ? R.Max.X : R.Min.X, bLow ? R.Max.Y : R.Min.Y);
				const double SX = bRight ? -1.0 : 1.0, SY = bLow ? -1.0 : 1.0;
				// U runs along the wall the group leans on (3.4 m), V into the room (2.2 m)
				const bool bAlongX = ((SpaceIndex + Corner) & 1) == 0;
				auto At = [&](double U, double V) { return bAlongX ? Origin + FVector2D(SX * U, SY * V) : Origin + FVector2D(SX * V, SY * U); };
				if (Busy(Origin, At(3.4, 2.2), 1.8))
				{
					continue;
				}
				const float YawU = bAlongX ? (SX > 0.0 ? 0.f : 180.f) : (SY > 0.0 ? 90.f : 270.f);
				const FString What = FString::Printf(TEXT("corner %d of %s"), Corner + 1, *Space.Name.ToString());
				// A standing lamp from the light pack in the corner itself: without it nobody would see the group.
				// It hangs on the power of its sector. Just clear of the ledges, which meet in the corner.
				if (PutGroup(What + TEXT(" (lamp)"), { { TEXT("Bollard"), At(0.5, 0.5), YawU, 0.0, true, 2.4 } }))
				{
					StandLamp(At(1.0, 1.0), Space.Sector, 125.0);
				}
				switch ((SpaceIndex * 5 + Corner * 3) % 6)
				{
				case 0:
					PutGroup(What, { { TEXT("TankCovered"), At(1.12, 1.08), YawU }, { TEXT("OxygenTank"), At(2.0, 0.6), YawU + 30.f } });
					break;
				case 1:
					PutGroup(What, { { TEXT("Case"), At(1.25, 0.9), YawU }, { TEXT("Toolbox"), At(1.3, 0.9), YawU + 20.f, 50.0 }, { TEXT("Drum"), At(2.35, 0.75), YawU } });
					break;
				case 2:
					PutGroup(What, { { TEXT("Pallet"), At(1.6, 1.6), YawU }, { TEXT("WireReel"), At(1.6, 1.6), YawU + 35.f, 17.4 }, { TEXT("Drum"), At(2.95, 0.7), YawU } });
					break;
				case 3:
					PutGroup(What, { { TEXT("Drum"), At(0.9, 0.82), YawU }, { TEXT("Drum"), At(1.5, 0.74), YawU + 50.f }, { TEXT("Drum"), At(0.86, 1.44), YawU + 110.f },
						{ TEXT("Extinguisher"), At(2.0, 0.6), YawU + 90.f } });
					break;
				case 4:
					if (PutGroup(What, { { TEXT("WorkLight"), At(1.1, 1.1), YawU + 45.f }, { TEXT("Case"), At(2.5, 0.85), YawU } }))
					{
						FHCPropShape Unused;
						if (Shapes(TEXT("WorkLight"), Unused))
						{
							// A battery lamp somebody left on: it keeps burning when the sector loses power
							StandLamp(At(1.5, 1.5), Space.Sector, 150.0, true);
						}
					}
					break;
				default:
					PutGroup(What, { { TEXT("EnergyNode"), At(1.0, 1.0), YawU }, { TEXT("OxygenTank"), At(1.75, 0.6), YawU }, { TEXT("OxygenTank"), At(2.0, 0.62), YawU + 40.f } });
					break;
				}
			}
			continue;
		}
		// Corridors: something small against a wall every few metres, on alternating sides, and loose cables
		const bool bAlongX = R.Width() >= R.Height();
		const int32 Length = bAlongX ? R.Width() : R.Height();
		int32 Count = 0;
		for (int32 Step = 3; Step + 2 < Length; Step += 6, ++Count)
		{
			const bool bHighSide = ((SpaceIndex + Count) & 1) != 0;
			const int32 A = (bAlongX ? R.Min.X : R.Min.Y) + Step;
			const int32 SideCell = bAlongX ? (bHighSide ? R.Max.Y - 1 : R.Min.Y) : (bHighSide ? R.Max.X - 1 : R.Min.X);
			const int32 Behind = SideCell + (bHighSide ? 1 : -1);
			// A real wall behind it, not the mouth of another corridor
			bool bWall = true;
			for (int32 D = -1; D <= 1; ++D)
			{
				const int32 Other = bAlongX ? Grid.GetSpaceAt(A + D, Behind) : Grid.GetSpaceAt(Behind, A + D);
				bWall &= Other == INDEX_NONE || !Grid.IsOpenBetween(SpaceIndex, Other);
			}
			const double WallLine = bHighSide ? SideCell + 1.0 : SideCell;
			const double Outward = bHighSide ? -1.0 : 1.0;
			auto At = [&](double FromWall, double Shift)
			{
				return bAlongX ? FVector2D(A + 0.5 + Shift, WallLine + Outward * FromWall) : FVector2D(WallLine + Outward * FromWall, A + 0.5 + Shift);
			};
			const float YawAlong = bAlongX ? 0.f : 90.f;
			const FString What = FString::Printf(TEXT("%s, %d m"), *Space.Name.ToString(), Step);
			const FVector2D Middle = bAlongX ? FVector2D(A + 2.5, (R.Min.Y + R.Max.Y) * 0.5) : FVector2D((R.Min.X + R.Max.X) * 0.5, A + 2.5);
			if ((Count & 1) == 0 && !Busy(Middle, Middle, 2.2))
			{
				PutGroup(What + TEXT(" (cable)"), { { TEXT("Cable"), Middle, YawAlong + (bHighSide ? 180.f : 0.f), 0.6, false } });
			}
			if (!bWall || Busy(At(0.55, 0.0), At(0.55, 0.0), 2.6))
			{
				continue;
			}
			switch ((SpaceIndex + Count * 3) % 5)
			{
			case 0:
				PutGroup(What, { { TEXT("Extinguisher"), At(0.5, 0.0), YawAlong + 90.f } });
				break;
			case 1:
				PutGroup(What, { { TEXT("Drum"), At(0.6, 0.0), YawAlong }, { TEXT("Drum"), At(0.62, 0.62), YawAlong + 70.f } });
				break;
			case 2:
				PutGroup(What, { { TEXT("OxygenTank"), At(0.5, 0.0), YawAlong }, { TEXT("OxygenTank"), At(0.5, 0.3), YawAlong + 60.f } });
				break;
			case 3:
				PutGroup(What, { { TEXT("Toolbox"), At(0.55, 0.0), YawAlong + 12.f }, { TEXT("RolledMat"), At(0.5, 0.95), YawAlong + 90.f } });
				break;
			default:
				PutGroup(What, { { TEXT("Case"), At(0.72, 0.0), YawAlong } });
				break;
			}
		}
	}
	return Out;
}

bool FHCPlacement::IsFreeForPlayer(const FVector2D& PointCm, double RadiusCm) const
{
	using namespace HCPlace;
	const FHCSolid Player = MakeRound(TEXT("player"), EHCSolidKind::SafePoint, PointCm, RadiusCm, 0.0, PlayerHeight);
	for (const FHCSolid& Solid : Solids)
	{
		const bool bInTheWay = Solid.Kind == EHCSolidKind::Wall || Solid.Kind == EHCSolidKind::Device || (Solid.Kind == EHCSolidKind::Prop && Solid.bBlocksWalking);
		if (bInTheWay && Penetration(Player, Solid) > 0.0)
		{
			return false;
		}
	}
	return true;
}

FVector2D FHCPlacement::FindSafeSpot(const FHCStationLayout& Layout, int32 SpaceIndex, bool* bOutFound) const
{
	using namespace HCPlace;
	if (bOutFound)
	{
		*bOutFound = true;
	}
	if (!Layout.Spaces.IsValidIndex(SpaceIndex))
	{
		if (bOutFound)
		{
			*bOutFound = false;
		}
		return FVector2D::ZeroVector;
	}
	const FHCSpace& Space = Layout.Spaces[SpaceIndex];
	const FVector2D Middle((Space.Rect.Min.X + Space.Rect.Max.X) * 0.5, (Space.Rect.Min.Y + Space.Rect.Max.Y) * 0.5);
	if (IsFreeForPlayer(Middle * GridCm, PlayerRadius))
	{
		return Middle;
	}
	// Something stands in the middle (a table, the emergency button): the nearest free spot of the same space instead
	FVector2D Best = Middle;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FIntRect& Part : Space.GetRects())
	{
		for (int32 Y = Part.Min.Y * 2; Y <= Part.Max.Y * 2; ++Y)
		{
			for (int32 X = Part.Min.X * 2; X <= Part.Max.X * 2; ++X)
			{
				const FVector2D Candidate(X * 0.5, Y * 0.5);
				const double Distance = FVector2D::DistSquared(Candidate, Middle);
				if (Distance < BestDistance && Space.ContainsCell(FMath::FloorToInt32(Candidate.X), FMath::FloorToInt32(Candidate.Y))
					&& IsFreeForPlayer(Candidate * GridCm, PlayerRadius + 5.0))
				{
					BestDistance = Distance;
					Best = Candidate;
				}
			}
		}
	}
	if (bOutFound)
	{
		*bOutFound = BestDistance < TNumericLimits<double>::Max();
	}
	return Best;
}

TArray<FString> FHCPlacement::FindProblems(const FHCStationLayout& Layout) const
{
	using namespace HCPlace;
	TArray<FString> Problems;
	const FHCStationGrid Grid(Layout);

	// Safe points are worked out from what stands in the rooms, so they only fail when a room is full
	TArray<FHCSolid> All = Solids;
	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num(); ++SpaceIndex)
	{
		bool bFound = false;
		const FVector2D Spot = FindSafeSpot(Layout, SpaceIndex, &bFound);
		if (!bFound)
		{
			Problems.Add(FString::Printf(TEXT("%s has no free spot to rescue a player to"), *Layout.Spaces[SpaceIndex].Name.ToString()));
		}
		All.Add(MakeRound(FString::Printf(TEXT("the safe point of %s"), *Layout.Spaces[SpaceIndex].Name.ToString()), EHCSolidKind::SafePoint, Spot * GridCm, PlayerRadius, 0.0, PlayerHeight));
	}

	for (int32 i = 0; i < All.Num(); ++i)
	{
		const FHCSolid& A = All[i];
		if (A.Kind != EHCSolidKind::Prop && A.Kind != EHCSolidKind::Device && A.Kind != EHCSolidKind::Vent)
		{
			continue; // walls, ways and spots are only ever the thing something else stands in
		}
		for (int32 j = 0; j < All.Num(); ++j)
		{
			const FHCSolid& B = All[j];
			const bool bBActive = B.Kind == EHCSolidKind::Prop || B.Kind == EHCSolidKind::Device || B.Kind == EHCSolidKind::Vent;
			if (j == i || (bBActive && j < i) || !Matters(A, B))
			{
				continue;
			}
			const double Depth = Penetration(A, B);
			if (Depth > OverlapSlack)
			{
				// Named from the thing that is easier to move: the prop stands in the device, not the device in the prop
				const bool bSwap = bBActive && B.Kind < A.Kind;
				Problems.Add(FString::Printf(TEXT("%s stands in %s (%.0f cm deep)"), bSwap ? *B.Name : *A.Name, bSwap ? *A.Name : *B.Name, Depth));
			}
		}
	}

	// Props in the floor, in the ceiling or outside the station
	for (const FHCBuiltProp& Prop : Props)
	{
		const FHCSolid Solid = SolidOf(Prop);
		const FString Name = FString::Printf(TEXT("%s at (%.1f, %.1f)"), *Prop.Key.ToString(), Prop.Position.X, Prop.Position.Y);
		const int32 SpaceIndex = Grid.GetSpaceAt(FMath::FloorToInt32(Prop.Position.X), FMath::FloorToInt32(Prop.Position.Y));
		if (!Layout.Spaces.IsValidIndex(SpaceIndex))
		{
			Problems.Add(FString::Printf(TEXT("%s stands outside the station"), *Name));
			continue;
		}
		if (Solid.ZMin < -OverlapSlack)
		{
			Problems.Add(FString::Printf(TEXT("%s in %s sinks %.0f cm into the floor"), *Name, *Layout.Spaces[SpaceIndex].Name.ToString(), -Solid.ZMin));
		}
		if (Solid.ZMax > Layout.Spaces[SpaceIndex].CeilingHeight + OverlapSlack)
		{
			Problems.Add(FString::Printf(TEXT("%s in %s reaches %.0f cm through the ceiling"), *Name, *Layout.Spaces[SpaceIndex].Name.ToString(),
				Solid.ZMax - Layout.Spaces[SpaceIndex].CeilingHeight));
		}
	}
	return Problems;
}
