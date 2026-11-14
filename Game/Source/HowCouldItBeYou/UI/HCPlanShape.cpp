#include "UI/HCPlanShape.h"

#include "World/HCStationLayout.h"

bool FHCPlanShape::IsFor(const FHCStationLayout& Layout) const
{
	return NumSpaces > 0 && MapName == Layout.MapName && Size == FIntPoint(Layout.SizeX, Layout.SizeY) && NumSpaces == Layout.Spaces.Num();
}

const FHCPlanDoor* FHCPlanShape::FindDoor(FName Id) const
{
	return Doors.FindByPredicate([Id](const FHCPlanDoor& Door) { return Door.Id == Id; });
}

void FHCPlanShape::Build(const FHCStationLayout& Layout)
{
	MapName = Layout.MapName;
	Size = FIntPoint(Layout.SizeX, Layout.SizeY);
	NumSpaces = Layout.Spaces.Num();
	Floors.Reset();
	Walls.Reset();
	Doors.Reset();

	// Floors: the cells of a space row by row as runs, and runs that repeat in the next row grow downwards.
	// The parts of an L or a cross overlap in the layout; drawn half see-through they would show as a darker patch.
	Floors.SetNum(Layout.Spaces.Num());
	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num(); ++SpaceIndex)
	{
		const FHCSpace& Space = Layout.Spaces[SpaceIndex];
		FIntRect Bounds = Space.Rect;
		for (const FIntRect& Part : Space.ExtraRects)
		{
			Bounds.Min = Bounds.Min.ComponentMin(Part.Min);
			Bounds.Max = Bounds.Max.ComponentMax(Part.Max);
		}
		TArray<FIntRect>& Out = Floors[SpaceIndex];
		// Rectangles that ended in the row above and may still grow
		TArray<int32> Open;
		for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; ++Y)
		{
			TArray<int32> StillOpen;
			for (int32 X = Bounds.Min.X; X < Bounds.Max.X;)
			{
				if (!Space.ContainsCell(X, Y))
				{
					++X;
					continue;
				}
				int32 End = X + 1;
				while (End < Bounds.Max.X && Space.ContainsCell(End, Y))
				{
					++End;
				}
				const int32* Same = Open.FindByPredicate([&Out, X, End](int32 Index) { return Out[Index].Min.X == X && Out[Index].Max.X == End; });
				if (Same)
				{
					Out[*Same].Max.Y = Y + 1;
					StillOpen.Add(*Same);
				}
				else
				{
					StillOpen.Add(Out.Add(FIntRect(X, Y, End, Y + 1)));
				}
				X = End;
			}
			Open = MoveTemp(StillOpen);
		}
	}

	// Walls: join the unit edges of one line into long strokes; door edges stay open
	const FHCStationGrid Grid(Layout);
	TArray<FHCWallEdge> Edges;
	Grid.CollectEdges(Edges);
	TArray<FIntVector> Keys;
	for (const FHCWallEdge& Edge : Edges)
	{
		if (Edge.DoorIndex == INDEX_NONE)
		{
			Keys.Add(Edge.bVertical ? FIntVector(1, Edge.Cell.X, Edge.Cell.Y) : FIntVector(0, Edge.Cell.Y, Edge.Cell.X));
		}
	}
	Keys.Sort([](const FIntVector& A, const FIntVector& B) { return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z; });
	for (int32 Start = 0; Start < Keys.Num();)
	{
		int32 End = Start + 1;
		while (End < Keys.Num() && Keys[End].X == Keys[Start].X && Keys[End].Y == Keys[Start].Y && Keys[End].Z == Keys[End - 1].Z + 1)
		{
			++End;
		}
		const FIntVector& K = Keys[Start];
		const double Line = K.Y, From = K.Z, To = Keys[End - 1].Z + 1;
		Walls.Add(K.X == 1 ? TPair<FVector2D, FVector2D>(FVector2D(Line, From), FVector2D(Line, To)) : TPair<FVector2D, FVector2D>(FVector2D(From, Line), FVector2D(To, Line)));
		Start = End;
	}

	for (const FHCDoorDef& Def : Layout.Doors)
	{
		FHCPlanDoor Door;
		Door.Id = Def.Name;
		Door.bSabotageable = Def.bSabotageable;
		Door.From = FVector2D(Def.Start.X, Def.Start.Y);
		Door.To = Door.From + (Def.bVertical ? FVector2D(0.0, Def.Width) : FVector2D(Def.Width, 0.0));
		Doors.Add(Door);
	}
}
