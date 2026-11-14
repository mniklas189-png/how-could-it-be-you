// The small plan in the corner of the HUD (AHCHUD::DrawMiniMap). It has its own file because it is drawn like the
// station map (UI/HCStationMapWidget): same rooms, walls, signs and colours, only on the canvas and much smaller.

#include "UI/HCHUD.h"

#include "CanvasItem.h"
#include "Characters/HCCharacter.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameState.h"
#include "Core/HCGraphics.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCPlayerState.h"
#include "Core/HCText.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GlobalRenderResources.h"
#include "Rendering/SlateRenderer.h"
#include "Sabotage/HCPowerSector.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"
#include "UI/HCFpsOverlay.h"
#include "UI/HCLook.h"
#include "UI/HCPlanShape.h"
#include "World/HCStationBuilder.h"

namespace HCMiniMap
{
	/** Floors, walls and doors of the map that is shown; built again when another map is loaded. */
	FHCPlanShape Shape;

	/** Capitals with letter spacing; the canvas draws them letter by letter. Returns the width; bDraw = false only measures. */
	float Tracked(UCanvas* Canvas, const FString& Text, float X, float Y, const FSlateFontInfo& Font, const FLinearColor& Color, float Tracking, bool bDraw = true)
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		float Width = 0.f;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Char = Text[Index];
			const FString Letter = FString::ConstructFromPtrSize(&Char, 1);
			if (bDraw && !FChar::IsWhitespace(Char))
			{
				FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(X + Width), Y), FText::FromString(Letter), Font, Color);
				Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.85f * Color.A), FVector2D(1.f, 1.f));
				Canvas->DrawItem(Item);
			}
			Width += static_cast<float>(Measure->Measure(Letter, Font).X) + (Index + 1 < Text.Len() ? Tracking : 0.f);
		}
		return Width;
	}

	/** Collects filled triangles and draws them in one go. */
	struct FShapes
	{
		TArray<FCanvasUVTri> Tris;

		void Add(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& ColorA, const FLinearColor& ColorB, const FLinearColor& ColorC)
		{
			FCanvasUVTri Tri;
			Tri.V0_Pos = A;
			Tri.V1_Pos = B;
			Tri.V2_Pos = C;
			Tri.V0_Color = ColorA;
			Tri.V1_Color = ColorB;
			Tri.V2_Color = ColorC;
			Tris.Add(Tri);
		}

		void Add(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
		{
			Add(A, B, C, Color, Color, Color);
		}

		void Draw(UCanvas* Canvas)
		{
			if (!Tris.IsEmpty())
			{
				FCanvasTriangleItem Item(Tris, GWhiteTexture);
				Item.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(Item);
				Tris.Reset();
			}
		}
	};
}

void AHCHUD::DrawMiniMap()
{
	// Small plan of the station in the top right corner, the counterpart of the task list on the left. It shows
	// the rooms, where you stand and your own open tasks - no other players and no remains: finding out who is
	// where stays the job of your own eyes.
	const APawn* Pawn = GetOwningPawn();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	if (!Pawn || !GS || GS->GetMatchPhase() != EHCMatchPhase::Playing || !FSlateApplication::IsInitialized())
	{
		return;
	}
	if (!MiniMapBuilder.IsValid())
	{
		for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
		{
			MiniMapBuilder = *It;
		}
	}
	const AHCStationBuilder* Builder = MiniMapBuilder.Get();
	if (!Builder || Builder->GetLayout().SizeX <= 0 || Builder->GetLayout().Spaces.Num() < 2)
	{
		return;
	}
	const FHCStationLayout& Layout = Builder->GetLayout();
	FHCPlanShape& Shape = HCMiniMap::Shape;
	if (!Shape.IsFor(Layout))
	{
		Shape.Build(Layout);
	}
	const FLinearColor& MapInk = HCLook::Ink;
	const FLinearColor& MapBone = HCLook::Bone;
	const FLinearColor& MapBoneMuted = HCLook::BoneMuted;
	const FLinearColor& MapBlood = HCLook::Blood;

	// Wide enough for the room names, but never more than a fifth of the screen
	const float Width = FMath::RoundToFloat(FMath::Clamp(Canvas->ClipX * 0.2f, 250.f, 360.f));
	const float Scale = Width / Layout.SizeX;
	const float Height = FMath::RoundToFloat(Layout.SizeY * Scale);
	const float X0 = FMath::RoundToFloat(Canvas->ClipX - Width - 26.f);
	// The head sits on the line of the task list. Only the FPS counter with its second line (settings, picture page:
	// "with details") reaches down to it on a big screen; then the plan moves down a little.
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	const float Top = 50.f + (GI && FMath::RoundToInt32(GI->GetOption(HCGraphics::FpsCounterKey, 0.f)) >= HCFps::Detailed ? 10.f : 0.f);
	const float Y0 = Top + 38.f;
	// Whole pixels, so that the hairlines of the walls stay sharp
	auto PX = [X0, Scale](double GridX) { return FMath::RoundToFloat(X0 + static_cast<float>(GridX) * Scale); };
	auto PY = [Y0, Scale](double GridY) { return FMath::RoundToFloat(Y0 + static_cast<float>(GridY) * Scale); };
	const double Time = GetWorld()->GetTimeSeconds();
	const float Blink = 0.6f + 0.4f * FMath::Sin(static_cast<float>(Time) * 7.f);

	// Which sectors are dark
	bool bDarkSector[8] = {};
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		if (!It->IsPowered() && It->GetSectorIndex() < UE_ARRAY_COUNT(bDarkSector))
		{
			bDarkSector[It->GetSectorIndex()] = true;
		}
	}
	auto IsDark = [&bDarkSector](const FHCSpace& Space)
	{
		const uint8 Index = static_cast<uint8>(Space.Sector);
		return Index < UE_ARRAY_COUNT(bDarkSector) && bDarkSector[Index];
	};
	const FName Here = Builder->GetSpaceNameAt(Pawn->GetActorLocation());
	const FHCSpace* HereSpace = Layout.FindSpace(Here);

	// The soft dark band of the task list, so both corners of the screen read as one HUD
	DrawSoftRect(X0 - 30.f, Top - 18.f, Width + 60.f, 38.f + Height + 36.f, 30.f, 18.f, HCLook::Alpha(MapInk, 0.62f));

	// Head: where you are, the key of the large plan on the right, a hairline under both
	UFont* HudFont = GEngine->GetMediumFont();
	const FSlateFontInfo HeadFont(HudFont, 12, TEXT("BoldCondensed"));
	const FSlateFontInfo SmallFont(HudFont, 9, TEXT("BoldCondensed"));
	const FSlateFontInfo RoomFont(HudFont, 8, TEXT("BoldCondensed"));
	const FSlateFontInfo RoomFontSmall(HudFont, 7, TEXT("BoldCondensed"));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FString Key = HCKeys::ShortName(EHCBinding::Map).ToUpper().ToString();
	const FVector2D KeySize = Measure->Measure(Key, SmallFont);
	const float CapWidth = static_cast<float>(KeySize.X) + 12.f;
	const float CapX = X0 + Width - CapWidth;
	const FLinearColor CapEdge = HCLook::Alpha(MapBoneMuted, 0.8f);
	DrawRect(CapEdge, CapX, Top - 1.f, CapWidth, 1.f);
	DrawRect(CapEdge, CapX, Top + 17.f, CapWidth, 2.f);
	DrawRect(CapEdge, CapX, Top, 1.f, 17.f);
	DrawRect(CapEdge, CapX + CapWidth - 1.f, Top, 1.f, 17.f);
	FCanvasTextItem KeyItem(FVector2D(FMath::RoundToFloat(CapX + 6.f), FMath::RoundToFloat(Top + (17.f - static_cast<float>(KeySize.Y)) * 0.5f)), FText::FromString(Key), SmallFont, MapBoneMuted);
	Canvas->DrawItem(KeyItem);
	{
		const FString Prefix = NSLOCTEXT("HC", "HudWhere", "ORT").ToString();
		const float PrefixWidth = HCMiniMap::Tracked(Canvas, Prefix, 0.f, 0.f, HeadFont, MapBoneMuted, 2.5f, false) + 12.f;
		const float Room = CapX - X0 - 14.f;
		// The full name if there is room for it, else the short one, else without the word in front
		FString Name = HereSpace ? HCText::Room(Here).ToUpper().ToString() : FString(TEXT("–"));
		if (HereSpace && PrefixWidth + HCMiniMap::Tracked(Canvas, Name, 0.f, 0.f, HeadFont, MapBone, 2.5f, false) > Room)
		{
			Name = HCText::RoomShort(Here).ToUpper().ToString();
		}
		float Cursor = X0;
		if (PrefixWidth + HCMiniMap::Tracked(Canvas, Name, 0.f, 0.f, HeadFont, MapBone, 2.5f, false) <= Room)
		{
			HCMiniMap::Tracked(Canvas, Prefix, Cursor, Top, HeadFont, MapBoneMuted, 2.5f);
			Cursor += PrefixWidth;
		}
		HCMiniMap::Tracked(Canvas, Name, Cursor, Top, HeadFont, HereSpace && IsDark(*HereSpace) ? MapBlood : MapBone, 2.5f);
	}
	DrawRect(HCLook::Alpha(MapBone, 0.16f), X0, Top + 26.f, Width, 1.f);

	// Floors: rooms lighter than corridors, the space you stand in lighter still, a sector without power in blood
	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num() && SpaceIndex < Shape.Floors.Num(); ++SpaceIndex)
	{
		const FHCSpace& Space = Layout.Spaces[SpaceIndex];
		const FLinearColor Fill = IsDark(Space) ? HCLook::Alpha(MapBlood, Space.bCorridor ? 0.24f : 0.4f)
			: &Space == HereSpace ? HCLook::Alpha(MapBone, Space.bCorridor ? 0.26f : 0.36f)
			: HCLook::Alpha(MapBoneMuted, Space.bCorridor ? 0.16f : 0.3f);
		for (const FIntRect& Part : Shape.Floors[SpaceIndex])
		{
			DrawRect(Fill, PX(Part.Min.X), PY(Part.Min.Y), PX(Part.Max.X) - PX(Part.Min.X), PY(Part.Max.Y) - PY(Part.Min.Y));
		}
	}
	// Walls as hairlines; the doors are the gaps in them
	const FLinearColor WallColor = HCLook::Alpha(MapBone, 0.5f);
	for (const TPair<FVector2D, FVector2D>& Wall : Shape.Walls)
	{
		const float AX = PX(Wall.Key.X), AY = PY(Wall.Key.Y);
		DrawRect(WallColor, AX, AY, FMath::Max(1.f, PX(Wall.Value.X) - AX), FMath::Max(1.f, PY(Wall.Value.Y) - AY));
	}

	// A lightning bolt: the sign for "no power here"
	auto DrawBolt = [this](float CX, float CY, float Size, const FLinearColor& Color)
	{
		DrawLine(CX + 0.3f * Size, CY - Size, CX - 0.4f * Size, CY + 0.12f * Size, Color, 2.f);
		DrawLine(CX - 0.4f * Size, CY + 0.12f * Size, CX + 0.4f * Size, CY - 0.12f * Size, Color, 2.f);
		DrawLine(CX + 0.4f * Size, CY - 0.12f * Size, CX - 0.3f * Size, CY + Size, Color, 2.f);
	};
	// Names and signs on top of all rooms, so that no neighbour covers them
	for (const FHCSpace& Space : Layout.Spaces)
	{
		if (Space.bCorridor)
		{
			continue;
		}
		const bool bDark = IsDark(Space);
		const float RX = PX(Space.Rect.Min.X), RY = PY(Space.Rect.Min.Y);
		const float RW = PX(Space.Rect.Max.X) - RX, RH = PY(Space.Rect.Max.Y) - RY;
		// Room name in the middle of the room, where no task station stands (short forms where the room is small
		// on the plan); without letter spacing and then smaller if it would not fit
		const FString Label = HCText::RoomShort(Space.Name).ToUpper().ToString();
		const FSlateFontInfo* Font = &RoomFont;
		float Tracking = 1.f;
		float LabelWidth = HCMiniMap::Tracked(Canvas, Label, 0.f, 0.f, *Font, MapBone, Tracking, false);
		if (LabelWidth > RW - 4.f)
		{
			Tracking = 0.f;
			LabelWidth = HCMiniMap::Tracked(Canvas, Label, 0.f, 0.f, *Font, MapBone, Tracking, false);
		}
		if (LabelWidth > RW - 2.f)
		{
			Font = &RoomFontSmall;
			LabelWidth = HCMiniMap::Tracked(Canvas, Label, 0.f, 0.f, *Font, MapBone, Tracking, false);
		}
		const FLinearColor LabelColor = bDark || &Space == HereSpace ? MapBone : HCLook::Alpha(MapBone, 0.62f);
		const float LabelY = FMath::RoundToFloat(RY + (RH - 11.f) * 0.5f + (bDark ? 7.f : 0.f));
		HCMiniMap::Tracked(Canvas, Label, FMath::RoundToFloat(RX + (RW - LabelWidth) * 0.5f), LabelY, *Font, LabelColor, Tracking);
		if (bDark)
		{
			DrawBolt(RX + RW * 0.5f, LabelY - 8.f, 6.f, HCLook::Alpha(MapBone, Blink));
		}
	}
	// Where the power comes back: the fuse box of a dark sector, framed and blinking
	for (const FHCFuseBoxDef& Box : Layout.FuseBoxes)
	{
		const uint8 Sector = static_cast<uint8>(Box.Sector);
		if (Sector < UE_ARRAY_COUNT(bDarkSector) && bDarkSector[Sector])
		{
			const float BX = PX(Box.Position.X), BY = PY(Box.Position.Y);
			const FLinearColor Frame = HCLook::Alpha(MapBone, Blink);
			DrawRect(HCLook::Alpha(MapInk, 0.85f), BX - 8.f, BY - 10.f, 16.f, 20.f);
			DrawRect(Frame, BX - 8.f, BY - 10.f, 16.f, 1.f);
			DrawRect(Frame, BX - 8.f, BY + 9.f, 16.f, 1.f);
			DrawRect(Frame, BX - 8.f, BY - 9.f, 1.f, 18.f);
			DrawRect(Frame, BX + 7.f, BY - 9.f, 1.f, 18.f);
			DrawBolt(BX, BY, 6.f, Frame);
		}
	}

	// Your own open tasks: the little boxes of the task list. Bone = ready, blue outline = not ready yet. Nobody else's.
	const AHCPlayerState* OwnState = PlayerOwner ? PlayerOwner->GetPlayerState<AHCPlayerState>() : nullptr;
	if (const UHCTaskComponent* OwnTasks = OwnState ? OwnState->GetTaskComponent() : nullptr)
	{
		for (TActorIterator<AHCTaskStation> It(GetWorld()); It; ++It)
		{
			float Wait = 0.f;
			if (OwnTasks->FindPendingIndexAt(*It, &Wait) != INDEX_NONE)
			{
				const FVector At = Builder->GetActorTransform().InverseTransformPosition(It->GetActorLocation()) / 100.0;
				const float TX = PX(At.X), TY = PY(At.Y);
				DrawRect(HCLook::Alpha(MapInk, 0.9f), TX - 5.f, TY - 5.f, 9.f, 9.f);
				if (Wait > 0.f)
				{
					const FLinearColor Later = HCLook::Alpha(HCLook::GhostBlue, 0.9f);
					DrawRect(Later, TX - 4.f, TY - 4.f, 7.f, 1.f);
					DrawRect(Later, TX - 4.f, TY + 2.f, 7.f, 1.f);
					DrawRect(Later, TX - 4.f, TY - 3.f, 1.f, 5.f);
					DrawRect(Later, TX + 2.f, TY - 3.f, 1.f, 5.f);
				}
				else
				{
					DrawRect(MapBone, TX - 4.f, TY - 4.f, 7.f, 7.f);
				}
			}
		}
	}

	// You: an arrow that points where you look, with a faint beam in front of it (a ghost's is blue)
	const FVector Local = Builder->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation()) / 100.0;
	const FVector2D Me(X0 + static_cast<float>(Local.X) * Scale, Y0 + static_cast<float>(Local.Y) * Scale);
	const float Yaw = FMath::DegreesToRadians((PlayerOwner ? PlayerOwner->GetControlRotation().Yaw : 0.f) - Builder->GetActorRotation().Yaw);
	const FVector2D Ahead(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D Aside(-Ahead.Y, Ahead.X);
	const FLinearColor MeColor = Cast<AHCCharacter>(Pawn) ? MapBone : HCLook::GhostBlue;
	HCMiniMap::FShapes Shapes;
	{
		const FLinearColor Near = HCLook::Alpha(MeColor, 0.34f);
		const FLinearColor Far = HCLook::Alpha(MeColor, 0.f);
		const double Reach = 34.0;
		const FVector2D Left = Me + (Ahead * 0.82 - Aside * 0.57) * Reach;
		const FVector2D Right = Me + (Ahead * 0.82 + Aside * 0.57) * Reach;
		const FVector2D Middle = Me + Ahead * Reach;
		Shapes.Add(Me, Left, Middle, Near, Far, Far);
		Shapes.Add(Me, Middle, Right, Near, Far, Far);
	}
	auto AddArrow = [&](double Grow, const FLinearColor& Color)
	{
		const FVector2D Tip = Me + Ahead * (8.0 + Grow * 1.6);
		const FVector2D Notch = Me - Ahead * (2.0 + Grow * 0.4);
		const FVector2D WingLeft = Me - Ahead * (5.5 + Grow) - Aside * (5.5 + Grow);
		const FVector2D WingRight = Me - Ahead * (5.5 + Grow) + Aside * (5.5 + Grow);
		Shapes.Add(Tip, WingLeft, Notch, Color);
		Shapes.Add(Tip, Notch, WingRight, Color);
	};
	AddArrow(2.0, HCLook::Alpha(MapInk, 0.9f));
	AddArrow(0.0, MeColor);
	Shapes.Draw(Canvas);
}
