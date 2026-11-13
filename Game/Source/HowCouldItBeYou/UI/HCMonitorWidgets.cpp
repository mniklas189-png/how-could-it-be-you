#include "UI/HCMonitorWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/HCCharacter.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCText.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Sabotage/HCPowerSector.h"
#include "UI/HCHUD.h"
#include "UI/HCLook.h"
#include "World/HCMonitorDevices.h"
#include "World/HCSecurityCamera.h"
#include "World/HCStationBuilder.h"

namespace HCMonitorLook
{
	constexpr double Margin = 64.0;
	constexpr double HeaderHeight = 112.0;
	constexpr double FooterHeight = 64.0;
	/** Frames per second of the pictures: choppy like a real surveillance system, smoother for the one shown large. */
	constexpr float GridFps = 12.f;
	constexpr float LargeFps = 24.f;
	const FLinearColor Phosphor(0.62f, 0.86f, 0.74f, 1.f);
	const FLinearColor Holo(0.35f, 0.85f, 1.f, 1.f);
}

// ---------------------------------------------------------------------------

void UHCDeviceViewWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	TWeakObjectPtr<UHCDeviceViewWidget> Weak(this);
	UHCPaintBox* View = HCLook::Control(WidgetTree, FVector2D::ZeroVector,
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCDeviceViewWidget* Widget = Weak.Get())
			{
				Widget->PaintView(D, S);
			}
		},
		[Weak](const FVector2D& Local, const FVector2D& Size)
		{
			if (UHCDeviceViewWidget* Widget = Weak.Get())
			{
				Widget->OnViewPressed(Local, Size);
			}
		});
	View->WantsHand = []() { return false; };
	WidgetTree->RootWidget = View;
}

void UHCDeviceViewWidget::Setup(AHCMonitorDevice* InDevice)
{
	Device = InDevice;
}

AHCMonitorDevice* UHCDeviceViewWidget::GetDevice() const
{
	return Device.Get();
}

bool UHCDeviceViewWidget::ShouldClose() const
{
	const AHCMonitorDevice* Owner = Device.Get();
	const APlayerController* PC = GetOwningPlayer();
	const AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	return !Owner || !Owner->IsPowered() || !Character || !Character->IsAlive();
}

FReply UHCDeviceViewWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::F1 || HCKeys::Is(EHCBinding::Interact, Key))
	{
		RequestClose();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHCDeviceViewWidget::RequestClose()
{
	if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->CloseDeviceView();
	}
}

FString UHCDeviceViewWidget::ClockText() const
{
	// The ship's clock: it is the middle of the night shift, and it runs with the round
	const int32 Seconds = 2 * 3600 + 13 * 60 + FMath::FloorToInt32(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	return FString::Printf(TEXT("%02d:%02d:%02d"), (Seconds / 3600) % 24, (Seconds / 60) % 60, Seconds % 60);
}

void UHCDeviceViewWidget::PaintFrame(const FHCDraw& D, const FString& Title, const FString& Subtitle, const FString& Hint) const
{
	using namespace HCMonitorLook;
	D.Box(FVector2D::ZeroVector, D.Size, HCLook::Ink);
	D.Text(Title, FVector2D(Margin, 34.0), HCLook::Caps(24.f, 220), HCLook::Bone);
	D.Text(Subtitle, FVector2D(Margin, 72.0), HCLook::Body(14.f), HCLook::BoneMuted);
	D.Box(FVector2D(Margin, HeaderHeight - 12.0), FVector2D(D.Size.X - 2.0 * Margin, 1.0), HCLook::Alpha(HCLook::Bone, 0.25f));
	D.TextRight(ClockText(), D.Size.X - Margin, 38.0, HCLook::Mono(18.f, 120), HCLook::BoneMuted);
	D.Text(Hint, FVector2D(Margin, D.Size.Y - FooterHeight + 20.0), HCLook::Mono(12.f, 160), HCLook::BoneMuted);
}

// ---------------------------------------------------------------------------
// Camera monitors

int32 UHCSecurityViewWidget::OpenViews = 0;

void UHCSecurityViewWidget::NativeConstruct()
{
	Super::NativeConstruct();
	++OpenViews;
	RefreshCameras();
}

void UHCSecurityViewWidget::NativeDestruct()
{
	OpenViews = FMath::Max(0, OpenViews - 1);
	Super::NativeDestruct();
}

void UHCSecurityViewWidget::RefreshCameras()
{
	TArray<AHCSecurityCamera*> Found;
	AHCSecurityCamera::GetAll(GetWorld(), Found);
	Cameras.Reset();
	Brushes.Reset();
	Brushes.SetNum(Found.Num());
	for (int32 Index = 0; Index < Found.Num(); ++Index)
	{
		Cameras.Add(Found[Index]);
		UTextureRenderTarget2D* Feed = Found[Index]->GetFeed();
		Brushes[Index].SetResourceObject(Feed);
		Brushes[Index].ImageSize = Feed ? FVector2D(Feed->SizeX, Feed->SizeY) : FVector2D(640.0, 360.0);
		Brushes[Index].DrawAs = ESlateBrushDrawType::Image;
	}
	Focused = Cameras.IsValidIndex(Focused) ? Focused : -1;
}

void UHCSecurityViewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Render the pictures that are due; at most two per frame so the cost is spread
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Budget = 2;
	for (int32 Index = 0; Index < Cameras.Num() && Budget > 0; ++Index)
	{
		AHCSecurityCamera* Camera = Cameras[Index].Get();
		if (!Camera || (Focused >= 0 && Index != Focused))
		{
			continue;
		}
		const float Fps = Focused >= 0 ? HCMonitorLook::LargeFps : HCMonitorLook::GridFps;
		if (Now - Camera->GetLastCaptureTime() >= 1.0 / Fps)
		{
			Camera->CaptureNow();
			--Budget;
		}
	}
}

FReply UHCSecurityViewWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const int32 Count = Cameras.Num();
	if (Count > 0 && (Key == EKeys::Left || Key == EKeys::Right || HCKeys::Is(EHCBinding::Left, Key) || HCKeys::Is(EHCBinding::Right, Key)))
	{
		const int32 Step = Key == EKeys::Left || HCKeys::Is(EHCBinding::Left, Key) ? -1 : 1;
		Focused = Focused < 0 ? (Step > 0 ? 0 : Count - 1) : (Focused + Step + Count) % Count;
		return FReply::Handled();
	}
	if (Key == EKeys::SpaceBar || Key == EKeys::BackSpace || Key == EKeys::Zero)
	{
		Focused = -1;
		return FReply::Handled();
	}
	static const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Numbers); ++Index)
	{
		if (Key == Numbers[Index] && Index < Count)
		{
			Focused = Focused == Index ? -1 : Index;
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FBox2D UHCSecurityViewWidget::GetFeedRect(int32 Index, const FVector2D& Size) const
{
	using namespace HCMonitorLook;
	const FVector2D AreaPos(Margin, HeaderHeight + 8.0);
	const FVector2D AreaSize(Size.X - 2.0 * Margin, Size.Y - HeaderHeight - FooterHeight - 8.0);
	auto Fit = [](const FVector2D& Pos, const FVector2D& Box)
	{
		// 16:9 inside the box, centred
		const double W = FMath::Min(Box.X, Box.Y * 16.0 / 9.0);
		const FVector2D Picture(W, W * 9.0 / 16.0);
		const FVector2D At = Pos + (Box - Picture) * 0.5;
		return FBox2D(At, At + Picture);
	};
	if (Focused >= 0)
	{
		return Index == Focused ? Fit(AreaPos, AreaSize) : FBox2D(ForceInit);
	}
	const int32 Count = FMath::Max(1, Cameras.Num());
	const int32 Columns = Count <= 1 ? 1 : Count <= 4 ? 2 : 3;
	const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);
	constexpr double Gap = 18.0;
	const FVector2D Cell((AreaSize.X - Gap * (Columns - 1)) / Columns, (AreaSize.Y - Gap * (Rows - 1)) / Rows);
	const int32 Column = Index % Columns, Row = Index / Columns;
	return Fit(AreaPos + FVector2D(Column * (Cell.X + Gap), Row * (Cell.Y + Gap)), Cell);
}

void UHCSecurityViewWidget::PaintView(const FHCDraw& D, const FHCPaintState& S) const
{
	const FString Title = HCLook::Upper(NSLOCTEXT("HC", "SecurityTitle", "Sicherheit  ·  Kameras"));
	const FString Subtitle = NSLOCTEXT("HC", "SecuritySubtitle", "Live aus den Gängen. Solange du zuschaust, blinkt an jeder Kamera ein rotes Licht – und du stehst still.").ToString();
	const FString Hint = HCLook::Upper(FText::Format(NSLOCTEXT("HC", "SecurityHintFmt", "Klick oder 1–{0}: Kamera groß   ·   {1}/{2}: weiter   ·   Leertaste: alle   ·   {3} oder Esc: zurück"),
		FMath::Max(1, Cameras.Num()), HCKeys::ShortName(EHCBinding::Left), HCKeys::ShortName(EHCBinding::Right), HCKeys::ShortName(EHCBinding::Interact)));
	PaintFrame(D, Title, Subtitle, Hint);
	if (Cameras.IsEmpty())
	{
		D.TextCentered(HCLook::Upper(NSLOCTEXT("HC", "SecurityNoCameras", "Keine Kameras")), D.Size.X * 0.5, D.Size.Y * 0.5, HCLook::Caps(18.f), HCLook::BoneMuted);
		return;
	}
	for (int32 Index = 0; Index < Cameras.Num(); ++Index)
	{
		const FBox2D Rect = GetFeedRect(Index, D.Size);
		if (Rect.bIsValid)
		{
			PaintFeed(D, Index, Rect, (S.bHovered && Rect.IsInside(S.Mouse)));
		}
	}
}

void UHCSecurityViewWidget::PaintFeed(const FHCDraw& D, int32 Index, const FBox2D& Rect, bool bHovered) const
{
	using namespace HCMonitorLook;
	const AHCSecurityCamera* Camera = Cameras[Index].Get();
	const FVector2D Pos = Rect.Min, Size = Rect.GetSize();
	const double Time = GetWorld()->GetTimeSeconds();
	D.Box(Pos - FVector2D(3.0, 3.0), Size + FVector2D(6.0, 6.0), HCLook::Plate);
	const bool bLive = Camera && Camera->HasSignal() && Camera->GetLastCaptureTime() >= 0.0;
	if (bLive)
	{
		D.Image(Pos, Size, &Brushes[Index]);
		// Scan lines and a slow rolling band, like an old monitor
		for (double Y = 0.0; Y < Size.Y; Y += 5.0)
		{
			D.Box(FVector2D(Pos.X, Pos.Y + Y), FVector2D(Size.X, 1.0), FLinearColor(0.f, 0.f, 0.f, 0.16f));
		}
		const double Band = FMath::Fmod(Time * 0.07, 1.0) * Size.Y;
		D.Box(FVector2D(Pos.X, Pos.Y + Band), FVector2D(Size.X, FMath::Min(26.0, Size.Y - Band)), FLinearColor(0.7f, 1.f, 0.85f, 0.035f));
	}
	else
	{
		// No power in the camera's sector: snow and a message
		D.Box(Pos, Size, FLinearColor(0.015f, 0.016f, 0.018f, 1.f));
		const int32 Flakes = FMath::Clamp(static_cast<int32>(Size.X * Size.Y / 900.0), 40, 420);
		for (int32 Flake = 0; Flake < Flakes; ++Flake)
		{
			const FVector2D At(FMath::FRandRange(0.0, Size.X - 4.0), FMath::FRandRange(0.0, Size.Y - 2.0));
			const float Grey = FMath::FRandRange(0.1f, 0.55f);
			D.Box(Pos + At, FVector2D(FMath::FRandRange(2.0, 7.0), 2.0), FLinearColor(Grey, Grey, Grey, 0.8f));
		}
		D.TextCentered(HCLook::Upper(NSLOCTEXT("HC", "SecurityNoSignal", "Kein Signal")), Pos.X + Size.X * 0.5, Pos.Y + Size.Y * 0.5 - 12.0, HCLook::Caps(18.f, 260), HCLook::Bone);
		D.TextCentered(NSLOCTEXT("HC", "SecurityNoPower", "Stromausfall in diesem Sektor").ToString(), Pos.X + Size.X * 0.5, Pos.Y + Size.Y * 0.5 + 14.0, HCLook::Body(12.f), HCLook::BoneMuted);
	}
	// Name band at the top, REC and the clock on the picture
	const FString Name = Camera ? FString::Printf(TEXT("%s   %s"), *Camera->GetCameraId().ToString().ToUpper(), *HCLook::Upper(Camera->GetLabel())) : FString();
	const FSlateFontInfo Small = HCLook::Mono(Size.X > 900.0 ? 15.f : 12.f, 140);
	const FVector2D NameSize = FHCDraw::Measure(Name, Small);
	D.Box(Pos + FVector2D(10.0, 10.0), FVector2D(NameSize.X + 16.0, NameSize.Y + 8.0), FLinearColor(0.f, 0.f, 0.f, 0.55f));
	D.Text(Name, Pos + FVector2D(18.0, 14.0), Small, Phosphor);
	if (bLive)
	{
		const bool bBlink = FMath::Fmod(Time, 1.2) < 0.75;
		const FVector2D RecSize = FHCDraw::Measure(TEXT("REC"), Small);
		const double RecX = Pos.X + Size.X - 18.0 - RecSize.X;
		D.Text(TEXT("REC"), FVector2D(RecX, Pos.Y + 14.0), Small, Phosphor);
		if (bBlink)
		{
			D.Arc(FVector2D(RecX - 12.0, Pos.Y + 14.0 + RecSize.Y * 0.5), 3.5f, 0.f, 1.f, HCLook::Blood, 7.f);
		}
		D.Text(ClockText(), Pos + FVector2D(18.0, Size.Y - NameSize.Y - 16.0), Small, Phosphor);
	}
	D.Brackets(Pos + FVector2D(4.0, 4.0), Size - FVector2D(8.0, 8.0), 18.f, 1.5f, HCLook::Alpha(Phosphor, bHovered ? 0.9f : 0.4f));
	if (bHovered)
	{
		D.Frame(Pos - FVector2D(3.0, 3.0), Size + FVector2D(6.0, 6.0), 1.f, HCLook::Alpha(HCLook::Bone, 0.8f));
	}
}

void UHCSecurityViewWidget::OnViewPressed(const FVector2D& Local, const FVector2D& Size)
{
	for (int32 Index = 0; Index < Cameras.Num(); ++Index)
	{
		const FBox2D Rect = GetFeedRect(Index, Size);
		if (Rect.bIsValid && Rect.IsInside(Local))
		{
			Focused = Focused == Index ? -1 : Index;
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Admin table

void UHCAdminViewWidget::BuildWalls() const
{
	bWallsBuilt = true;
	const AHCStationBuilder* Builder = AHCMonitorDevice::FindBuilder(GetWorld());
	if (!Builder)
	{
		return;
	}
	const FHCStationGrid Grid(Builder->GetLayout());
	TArray<FHCWallEdge> Edges;
	Grid.CollectEdges(Edges);
	// Join the unit edges of one line into long strokes; door edges stay open
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
}

void UHCAdminViewWidget::PaintView(const FHCDraw& D, const FHCPaintState& S) const
{
	using namespace HCMonitorLook;
	const FString Title = HCLook::Upper(NSLOCTEXT("HC", "AdminTitle", "Leitstand  ·  Belegung"));
	const FString Subtitle = NSLOCTEXT("HC", "AdminSubtitle", "Wie viele in jedem Raum sind – nicht wer. Gänge werden nicht erfasst, Tote liegen auf den Sensoren wie Lebende.").ToString();
	const FString Hint = HCLook::Upper(FText::Format(NSLOCTEXT("HC", "AdminHintFmt", "{0} oder Esc: zurück"), HCKeys::ShortName(EHCBinding::Interact)));
	PaintFrame(D, Title, Subtitle, Hint);

	const AHCStationBuilder* Builder = AHCMonitorDevice::FindBuilder(GetWorld());
	const AHCAdminTable* Table = Cast<AHCAdminTable>(Device.Get());
	if (!Builder || Builder->GetLayout().SizeX <= 0)
	{
		return;
	}
	if (!bWallsBuilt)
	{
		BuildWalls();
	}
	const FHCStationLayout& Layout = Builder->GetLayout();
	const FVector2D AreaPos(Margin, HeaderHeight + 10.0);
	const FVector2D AreaSize(D.Size.X - 2.0 * Margin, D.Size.Y - HeaderHeight - FooterHeight - 20.0);
	const double Scale = FMath::Min(AreaSize.X / Layout.SizeX, AreaSize.Y / Layout.SizeY);
	const FVector2D Origin = AreaPos + (AreaSize - FVector2D(Layout.SizeX, Layout.SizeY) * Scale) * 0.5;
	auto ToScreen = [&](const FVector2D& Grid) { return Origin + Grid * Scale; };
	const double Time = GetWorld()->GetTimeSeconds();

	// Floor plan: rooms a little lighter than the corridors, holes (core, containers) as solid black
	for (const FHCSpace& Space : Layout.Spaces)
	{
		const AHCPowerSector* Power = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Space.Sector));
		const bool bDark = Power && !Power->IsPowered();
		const FLinearColor Fill = bDark ? FLinearColor(0.12f, 0.03f, 0.03f, 0.9f)
			: Space.bCorridor ? FLinearColor(0.05f, 0.09f, 0.11f, 0.9f) : FLinearColor(0.07f, 0.15f, 0.19f, 0.95f);
		for (const FIntRect& Part : Space.GetRects())
		{
			D.Box(ToScreen(FVector2D(Part.Min)), FVector2D(Part.Size()) * Scale, Fill);
		}
		for (const FIntRect& Hole : Space.Holes)
		{
			D.Box(ToScreen(FVector2D(Hole.Min)), FVector2D(Hole.Size()) * Scale, HCLook::Ink);
		}
	}
	for (const TPair<FVector2D, FVector2D>& Wall : Walls)
	{
		D.Lines({ ToScreen(Wall.Key), ToScreen(Wall.Value) }, HCLook::Alpha(Holo, 0.75f), 1.5f);
	}

	// Rooms: name, and one dot per person; a room without power has no sensors
	const TArray<uint8>* Counts = Table ? &Table->GetRoomCounts() : nullptr;
	const double Dot = FMath::Clamp(Scale * 0.95, 7.0, 16.0);
	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num(); ++SpaceIndex)
	{
		const FHCSpace& Space = Layout.Spaces[SpaceIndex];
		if (Space.bCorridor)
		{
			continue;
		}
		const FVector2D TopLeft = ToScreen(FVector2D(Space.Rect.Min)) + FVector2D(6.0, 5.0);
		D.Text(HCLook::Upper(HCText::RoomShort(Space.Name)), TopLeft, HCLook::Caps(Scale > 11.0 ? 12.f : 10.f, 140), HCLook::Alpha(HCLook::Bone, 0.85f));
		const FVector2D Middle = ToScreen(FVector2D((Space.Rect.Min.X + Space.Rect.Max.X) * 0.5, (Space.Rect.Min.Y + Space.Rect.Max.Y) * 0.5 + 0.6));
		const AHCPowerSector* Power = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Space.Sector));
		if (Power && !Power->IsPowered())
		{
			D.TextCentered(HCLook::Upper(NSLOCTEXT("HC", "AdminNoSensor", "Kein Signal")), Middle.X, Middle.Y - 8.0, HCLook::Caps(11.f, 200), HCLook::Alpha(HCLook::Blood, 0.9f));
			continue;
		}
		const int32 Count = Counts && Counts->IsValidIndex(SpaceIndex) ? (*Counts)[SpaceIndex] : 0;
		const int32 PerRow = 5;
		const double Step = Dot * 1.45;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 Row = Index / PerRow, Column = Index % PerRow;
			const int32 InRow = FMath::Min(PerRow, Count - Row * PerRow);
			const FVector2D At = Middle + FVector2D((Column - (InRow - 1) * 0.5) * Step, (Row - (FMath::DivideAndRoundUp(Count, PerRow) - 1) * 0.5) * Step);
			const float Pulse = 0.85f + 0.15f * FMath::Sin(static_cast<float>(Time) * 3.f + Index);
			D.Arc(At, static_cast<float>(Dot * 0.25), 0.f, 1.f, FLinearColor(1.f, 0.82f, 0.38f, Pulse), static_cast<float>(Dot * 0.5));
		}
	}
	// A faint sweep over the plan, so it reads as a live picture
	const double Sweep = FMath::Fmod(Time * 0.25, 1.0);
	D.Box(Origin + FVector2D(Layout.SizeX * Scale * Sweep, 0.0), FVector2D(2.0, Layout.SizeY * Scale), HCLook::Alpha(Holo, 0.18f));
}
