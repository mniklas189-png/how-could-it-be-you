#include "UI/HCStationMapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCKeyBindings.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Core/HCText.h"
#include "EngineUtils.h"
#include "Sabotage/HCDoor.h"
#include "Sabotage/HCPowerSector.h"
#include "Sabotage/HCSabotageManager.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"
#include "UI/HCHUD.h"
#include "UI/HCLook.h"
#include "World/HCMapCatalog.h"
#include "World/HCStationBuilder.h"

namespace HCMapLook
{
	constexpr double Margin = 64.0;
	constexpr double HeaderHeight = 112.0;
	constexpr double FooterHeight = 64.0;
	constexpr double PanelWidth = 430.0;
	constexpr double PanelGap = 44.0;
	constexpr double HeadHeight = 36.0;
	constexpr double SectorRow = 50.0;
	constexpr double DoorRow = 38.0;
	constexpr double TaskRow = 28.0;
	constexpr double RowGap = 6.0;
	constexpr double SectionGap = 16.0;

	/** Text that ends exactly at RightX at every window size (the spacing after the last letter does not count). */
	void PlanTextRight(const FHCDraw& D, const FString& Text, double RightX, double Y, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		const double Trailing = Font.LetterSpacing / 1000.0 * Font.Size;
		D.Text(Text, FVector2D(RightX - D.MeasureExact(Text, Font).X + Trailing, Y), Font, Color);
	}

	/** Text with its middle exactly at CenterX at every window size. */
	void PlanTextCentered(const FHCDraw& D, const FString& Text, double CenterX, double Y, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		const double Trailing = Font.LetterSpacing / 1000.0 * Font.Size;
		D.Text(Text, FVector2D(CenterX - (D.MeasureExact(Text, Font).X - Trailing) * 0.5, Y), Font, Color);
	}

	/** A lightning bolt: the sign for "no power here". */
	void PlanBolt(const FHCDraw& D, const FVector2D& At, double Size, const FLinearColor& Color)
	{
		D.Lines({ At + FVector2D(0.3, -1.0) * Size, At + FVector2D(-0.4, 0.12) * Size, At + FVector2D(0.4, -0.12) * Size, At + FVector2D(-0.3, 1.0) * Size }, Color, 2.f);
	}

	/** The little box of the task list: bone = can be done now, blue outline = not ready yet, green = done. */
	void PlanTaskBox(const FHCDraw& D, const FVector2D& Center, double Size, float Wait, bool bDone)
	{
		const FVector2D Pos = Center - FVector2D(Size, Size) * 0.5;
		const FVector2D Box(Size, Size);
		if (bDone)
		{
			D.Box(Pos, Box, HCLook::Alpha(HCLook::Signal, 0.6f));
		}
		else if (Wait > 0.f)
		{
			D.Frame(Pos, Box, 1.5f, HCLook::Alpha(HCLook::GhostBlue, 0.95f));
		}
		else
		{
			D.Box(Pos, Box, HCLook::Bone);
		}
	}

	/** You: an arrow that points where you look, with a faint beam in front of it. */
	void PlanArrow(const FHCDraw& D, const FVector2D& At, double Yaw, double Size, const FLinearColor& Color)
	{
		const FVector2D Ahead(FMath::Cos(Yaw), FMath::Sin(Yaw));
		const FVector2D Aside(-Ahead.Y, Ahead.X);
		const double Reach = Size * 5.0;
		const FLinearColor Near = HCLook::Alpha(Color, 0.3f);
		const FLinearColor Far = HCLook::Alpha(Color, 0.f);
		D.Fan({ At, At + (Ahead * 0.82 - Aside * 0.57) * Reach, At + Ahead * Reach, At + (Ahead * 0.82 + Aside * 0.57) * Reach }, { Near, Far, Far, Far });
		auto Shape = [&](double Grow, const FLinearColor& Fill)
		{
			const FVector2D Tip = At + Ahead * (Size + Grow * 1.6);
			const FVector2D Notch = At - Ahead * (Size * 0.25 + Grow * 0.4);
			const FVector2D Left = At - Ahead * (Size * 0.7 + Grow) - Aside * (Size * 0.7 + Grow);
			const FVector2D Right = At - Ahead * (Size * 0.7 + Grow) + Aside * (Size * 0.7 + Grow);
			D.Fan({ Tip, Left, Notch }, Fill);
			D.Fan({ Tip, Notch, Right }, Fill);
		};
		Shape(2.5, HCLook::Alpha(HCLook::Ink, 0.92f));
		Shape(0.0, Color);
	}

	/** A small plate with a line of text next to a point of the plan, kept inside the screen. */
	void PlanTag(const FHCDraw& D, const FVector2D& At, const FString& Text, const FLinearColor& Edge)
	{
		const FSlateFontInfo Font = HCLook::Caps(11.f, 160);
		const FVector2D TextSize = D.MeasureExact(Text, Font);
		const FVector2D Size(TextSize.X + 20.0, TextSize.Y + 12.0);
		FVector2D Pos(At.X + 18.0, At.Y - Size.Y - 10.0);
		Pos.X = FMath::Clamp(Pos.X, 8.0, FMath::Max(8.0, D.Size.X - Size.X - 8.0));
		Pos.Y = FMath::Clamp(Pos.Y, 8.0, FMath::Max(8.0, D.Size.Y - Size.Y - 8.0));
		D.Box(Pos, Size, HCLook::Alpha(HCLook::Ink, 0.96f));
		D.Frame(Pos, Size, 1.f, Edge);
		D.Text(Text, Pos + FVector2D(10.0, 6.0), Font, HCLook::Bone);
	}

	/** Head of a part of the side panel: capitals, something on the right, a hairline under both. */
	void PanelHead(const FHCDraw& D, const FVector2D& Pos, double Width, const FString& Title, const FLinearColor& Color, const FString& Right, const FLinearColor& RightColor)
	{
		D.Text(Title, Pos, HCLook::Caps(13.f, 240), Color);
		if (!Right.IsEmpty())
		{
			PlanTextRight(D, Right, Pos.X + Width, Pos.Y + 2.0, HCLook::Mono(11.f, 140), RightColor);
		}
		D.Box(Pos + FVector2D(0.0, 25.0), FVector2D(Width, 1.0), HCLook::Alpha(HCLook::Bone, 0.16f));
	}
}

/** One frame of the screen: what is shown and where, for painting and for clicks. */
struct UHCStationMapWidget::FScreen
{
	struct FSectorRow
	{
		uint8 Sector = 0;
		bool bPowered = true;
		int32 Seconds = 0;
		FBox2D Rect = FBox2D(ForceInit);
	};
	struct FDoorRow
	{
		FName Id;
		FString Name;
		bool bLocked = false;
		FBox2D Rect = FBox2D(ForceInit);
		/** The opening on the plan. */
		bool bOnPlan = false;
		FVector2D From = FVector2D::ZeroVector;
		FVector2D To = FVector2D::ZeroVector;
	};
	struct FTaskRow
	{
		FString Name;
		FString Where;
		float Wait = 0.f;
		bool bDone = false;
		/** Not valid if the list was too long for the panel. */
		FBox2D Rect = FBox2D(ForceInit);
		bool bOnPlan = false;
		FVector2D PlanAt = FVector2D::ZeroVector;
	};

	const AHCStationBuilder* Builder = nullptr;
	const FHCStationLayout* Layout = nullptr;
	FVector2D PlanPos = FVector2D::ZeroVector;
	FVector2D PlanSize = FVector2D::ZeroVector;
	FVector2D Origin = FVector2D::ZeroVector;
	double Scale = 1.0;
	FVector2D PanelPos = FVector2D::ZeroVector;

	bool bMonster = false;
	/** Seconds until a sabotage is possible at all, and until each kind is ready again. */
	int32 StartWait = 0;
	int32 PowerWait = 0;
	int32 DoorWait = 0;
	bool bAnyDark = false;
	bool bAnyLocked = false;

	double PowerHeadY = 0.0;
	double NoteY = 0.0;
	double DoorHeadY = -1.0;
	double TaskHeadY = -1.0;
	int32 TasksDone = 0;
	TArray<FSectorRow> Sectors;
	TArray<FDoorRow> Doors;
	TArray<FTaskRow> Tasks;
	/** In the order of the hit test: what lies on top comes first. */
	TArray<FLayoutItem> Items;

	FVector2D ToScreen(const FVector2D& Grid) const { return Origin + Grid * Scale; }
	bool CanCutPower() const { return bMonster && StartWait <= 0 && PowerWait <= 0 && !bAnyDark; }
	bool CanLockDoor() const { return bMonster && StartWait <= 0 && DoorWait <= 0 && !bAnyLocked; }
	bool IsDark(EHCSector Sector) const
	{
		const FSectorRow* Row = Sectors.FindByPredicate([Sector](const FSectorRow& R) { return R.Sector == static_cast<uint8>(Sector); });
		return Row && !Row->bPowered;
	}
};

// ---------------------------------------------------------------------------

void UHCStationMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	const TWeakObjectPtr<UHCStationMapWidget> Weak(this);
	UHCPaintBox* View = HCLook::Control(WidgetTree, FVector2D::ZeroVector,
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCStationMapWidget* Widget = Weak.Get())
			{
				Widget->PaintMap(D, S);
			}
		},
		[Weak](const FVector2D& Local, const FVector2D& Size)
		{
			if (UHCStationMapWidget* Widget = Weak.Get())
			{
				Widget->PressedItem = Widget->HitTest(Local, Size);
				Widget->PressMouse = Local;
			}
		});
	View->OnDragged = [Weak](const FVector2D& Local, const FVector2D&)
	{
		if (UHCStationMapWidget* Widget = Weak.Get())
		{
			Widget->PressMouse = Local;
		}
	};
	// A click counts when the mouse is released over the thing it went down on, as with every button of the game
	View->OnReleased = [Weak, View](bool bInside)
	{
		UHCStationMapWidget* Widget = Weak.Get();
		if (!Widget)
		{
			return;
		}
		const FHCMapHit Pressed = Widget->PressedItem;
		Widget->PressedItem = FHCMapHit();
		if (!bInside || !Pressed.IsClickable() || !(Widget->HitTest(Widget->PressMouse, FVector2D(View->GetCachedGeometry().GetLocalSize())) == Pressed))
		{
			return;
		}
		if (Pressed.Item == EHCMapItem::CutPower)
		{
			Widget->RequestSabotage(static_cast<uint8>(EHCSabotageType::PowerOutage), AHCSabotageManager::SectorTargetId(static_cast<uint8>(Pressed.Index)));
		}
		else
		{
			Widget->RequestSabotage(static_cast<uint8>(EHCSabotageType::DoorLock), Pressed.Id);
		}
	};
	View->WantsHand = [Weak]()
	{
		const UHCStationMapWidget* Widget = Weak.Get();
		return Widget && Widget->HotItem.IsClickable();
	};
	WidgetTree->RootWidget = View;
}

void UHCStationMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	OpenedAt = FPlatformTime::Seconds();
	PressedItem = FHCMapHit();
	HotItem = FHCMapHit();
}

bool UHCStationMapWidget::IsOwnerMonster() const
{
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	// The own role is the only role a client really knows (docs/02 §2.5)
	return PS && PS->IsAlive() && PS->GetRoleInfo().Role == EHCRole::Monster;
}

void UHCStationMapWidget::Close()
{
	const APlayerController* PC = GetOwningPlayer();
	if (AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->CloseStationMap();
	}
}

bool UHCStationMapWidget::RequestSabotage(uint8 SabotageType, FName TargetId)
{
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!PC || !IsOwnerMonster())
	{
		return false;
	}
	PC->ServerRequestSabotage(SabotageType, TargetId);
	Close();
	return true;
}

FReply UHCStationMapWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (HCKeys::Is(EHCBinding::Map, Key) || Key == EKeys::Escape || Key == EKeys::F1)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

const AHCStationBuilder* UHCStationMapWidget::FindBuilder() const
{
	if (!Builder.IsValid())
	{
		for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
		{
			Builder = *It;
		}
	}
	return Builder.Get();
}

// ---------------------------------------------------------------------------
// What is on the screen

void UHCStationMapWidget::BuildScreen(const FVector2D& Size, FScreen& Out) const
{
	using namespace HCMapLook;
	const UWorld* World = GetWorld();
	Out.Builder = World ? FindBuilder() : nullptr;
	if (!Out.Builder || Out.Builder->GetLayout().SizeX <= 0 || Out.Builder->GetLayout().SizeY <= 0)
	{
		Out.Builder = nullptr;
		return;
	}
	const FHCStationLayout& Layout = Out.Builder->GetLayout();
	Out.Layout = &Layout;
	if (!Shape.IsFor(Layout))
	{
		Shape.Build(Layout);
	}
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	const AHCGameState* GS = World->GetGameState<AHCGameState>();
	Out.bMonster = IsOwnerMonster();

	// The plan fills the left, the panel is a fixed column on the right
	Out.PanelPos = FVector2D(Size.X - Margin - PanelWidth, HeaderHeight + 12.0);
	Out.PlanPos = FVector2D(Margin, HeaderHeight + 12.0);
	Out.PlanSize = FVector2D(FMath::Max(100.0, Out.PanelPos.X - PanelGap - Margin), FMath::Max(100.0, Size.Y - HeaderHeight - 12.0 - FooterHeight - 12.0));
	Out.Scale = FMath::Min(Out.PlanSize.X / Layout.SizeX, Out.PlanSize.Y / Layout.SizeY);
	Out.Origin = Out.PlanPos + (Out.PlanSize - FVector2D(Layout.SizeX, Layout.SizeY) * Out.Scale) * 0.5;

	if (Out.bMonster && PS && GS)
	{
		const double Now = GS->GetServerWorldTimeSeconds();
		Out.StartWait = FMath::CeilToInt32(FMath::Max(0.0, PS->GetSabotageEarliestTime() - Now));
		Out.PowerWait = FMath::CeilToInt32(FMath::Max(0.0, FMath::Max(PS->GetSabotageEarliestTime(), PS->GetPowerSabotageReadyTime()) - Now));
		Out.DoorWait = FMath::CeilToInt32(FMath::Max(0.0, FMath::Max(PS->GetSabotageEarliestTime(), PS->GetDoorSabotageReadyTime()) - Now));
	}

	// Power: one row per sector
	double Y = Out.PanelPos.Y;
	Out.PowerHeadY = Y;
	Y += HeadHeight;
	TArray<const AHCPowerSector*> Sectors;
	for (TActorIterator<AHCPowerSector> It(World); It; ++It)
	{
		Sectors.Add(*It);
	}
	Sectors.Sort([](const AHCPowerSector& A, const AHCPowerSector& B) { return A.GetSectorIndex() < B.GetSectorIndex(); });
	for (const AHCPowerSector* Sector : Sectors)
	{
		FScreen::FSectorRow Row;
		Row.Sector = Sector->GetSectorIndex();
		Row.bPowered = Sector->IsPowered();
		Row.Seconds = Row.bPowered ? 0 : FMath::CeilToInt32(Sector->GetSecondsUntilAutoRestore());
		Row.Rect = FBox2D(FVector2D(Out.PanelPos.X, Y), FVector2D(Out.PanelPos.X + PanelWidth, Y + SectorRow));
		Out.bAnyDark |= !Row.bPowered;
		Out.Sectors.Add(Row);
		Y += SectorRow + RowGap;
	}
	Out.NoteY = Y + 2.0;
	Y += 24.0 + SectionGap;

	// Doors: only the Wandler sees the doors that can be locked
	if (Out.bMonster)
	{
		TArray<const AHCDoor*> Doors;
		for (TActorIterator<AHCDoor> It(World); It; ++It)
		{
			if (It->bSabotageable)
			{
				Doors.Add(*It);
			}
		}
		Doors.Sort([](const AHCDoor& A, const AHCDoor& B) { return A.DoorId.LexicalLess(B.DoorId); });
		if (!Doors.IsEmpty())
		{
			Out.DoorHeadY = Y;
			Y += HeadHeight;
		}
		for (const AHCDoor* Door : Doors)
		{
			FScreen::FDoorRow Row;
			Row.Id = Door->DoorId;
			Row.Name = HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapDoor", "Tür {0}"), Door->GetDisplayName()));
			Row.bLocked = Door->GetDoorState() == EHCDoorState::Locked;
			Row.Rect = FBox2D(FVector2D(Out.PanelPos.X, Y), FVector2D(Out.PanelPos.X + PanelWidth, Y + DoorRow));
			if (const FHCPlanDoor* OnPlan = Shape.FindDoor(Row.Id))
			{
				Row.bOnPlan = true;
				Row.From = Out.ToScreen(OnPlan->From);
				Row.To = Out.ToScreen(OnPlan->To);
			}
			Out.bAnyLocked |= Row.bLocked;
			Out.Doors.Add(Row);
			Y += DoorRow + RowGap;
		}
		if (!Doors.IsEmpty())
		{
			Y += SectionGap - RowGap;
		}
	}

	// Your own tasks (the Wandler's are the ones it pretends to do), as many as the panel has room for
	if (const UHCTaskComponent* Tasks = PS ? PS->GetTaskComponent() : nullptr)
	{
		const UHCGameSettings* Settings = UHCGameSettings::Get();
		const double Bottom = Size.Y - FooterHeight;
		const bool bRoom = !Tasks->GetTasks().IsEmpty() && Y + HeadHeight + TaskRow <= Bottom;
		if (bRoom)
		{
			Out.TaskHeadY = Y;
			Y += HeadHeight;
		}
		for (const FHCTaskEntry& Entry : Tasks->GetTasks())
		{
			FScreen::FTaskRow Row;
			const FHCTaskDefinition* Def = Settings->FindTask(Entry.TaskId);
			Row.Name = Def ? Def->DisplayName.ToString() : Entry.TaskId.ToString();
			Row.bDone = Entry.bCompleted;
			Row.Wait = Row.bDone ? 0.f : Tasks->GetWaitSeconds(Entry);
			if (!Row.bDone && Entry.NumSteps > 1)
			{
				Row.Name += FString::Printf(TEXT("   %d/%d"), Entry.CurrentStep + 1, Entry.NumSteps);
			}
			if (!Row.bDone && Def && Def->Steps.IsValidIndex(Entry.CurrentStep))
			{
				Row.Where = HCLook::Upper(HCText::Room(Def->Steps[Entry.CurrentStep].Room));
				if (Row.Wait > 0.f)
				{
					Row.Where += FString::Printf(TEXT("  ·  %d S"), FMath::CeilToInt32(Row.Wait));
				}
			}
			if (bRoom && Y + TaskRow <= Bottom)
			{
				Row.Rect = FBox2D(FVector2D(Out.PanelPos.X, Y), FVector2D(Out.PanelPos.X + PanelWidth, Y + TaskRow));
				Y += TaskRow;
			}
			Out.TasksDone += Row.bDone ? 1 : 0;
			Out.Tasks.Add(Row);
		}
		// Where the next step of each open task is
		for (TActorIterator<AHCTaskStation> It(World); It; ++It)
		{
			const int32 Index = Tasks->FindPendingIndexAt(*It);
			if (Out.Tasks.IsValidIndex(Index))
			{
				const FVector At = Out.Builder->GetActorTransform().InverseTransformPosition(It->GetActorLocation()) / 100.0;
				Out.Tasks[Index].bOnPlan = true;
				Out.Tasks[Index].PlanAt = Out.ToScreen(FVector2D(At.X, At.Y));
			}
		}
	}

	// Hit test: the marks on the plan first, then the rows
	auto AddItem = [&Out](EHCMapItem Item, int32 Index, FName Id, const FBox2D& Rect)
	{
		FLayoutItem Entry;
		Entry.Hit.Item = Item;
		Entry.Hit.Index = Index;
		Entry.Hit.Id = Id;
		Entry.Rect = Rect;
		Out.Items.Add(Entry);
	};
	for (int32 Index = 0; Index < Out.Tasks.Num(); ++Index)
	{
		if (Out.Tasks[Index].bOnPlan)
		{
			AddItem(EHCMapItem::Task, Index, NAME_None, FBox2D(Out.Tasks[Index].PlanAt - FVector2D(13.0, 13.0), Out.Tasks[Index].PlanAt + FVector2D(13.0, 13.0)));
		}
	}
	for (const FScreen::FDoorRow& Door : Out.Doors)
	{
		if (Door.bOnPlan)
		{
			AddItem(!Door.bLocked && Out.CanLockDoor() ? EHCMapItem::Door : EHCMapItem::DoorLook, INDEX_NONE, Door.Id, FBox2D(FVector2D::Min(Door.From, Door.To) - FVector2D(12.0, 12.0), FVector2D::Max(Door.From, Door.To) + FVector2D(12.0, 12.0)));
		}
	}
	for (const FScreen::FSectorRow& Sector : Out.Sectors)
	{
		// A click cuts the power only while that is possible; otherwise the row just shows its rooms
		AddItem(Sector.bPowered && Out.CanCutPower() ? EHCMapItem::CutPower : EHCMapItem::Sector, Sector.Sector, NAME_None, Sector.Rect);
	}
	for (const FScreen::FDoorRow& Door : Out.Doors)
	{
		AddItem(!Door.bLocked && Out.CanLockDoor() ? EHCMapItem::Door : EHCMapItem::DoorLook, INDEX_NONE, Door.Id, Door.Rect);
	}
	for (int32 Index = 0; Index < Out.Tasks.Num(); ++Index)
	{
		if (Out.Tasks[Index].Rect.bIsValid)
		{
			AddItem(EHCMapItem::Task, Index, NAME_None, Out.Tasks[Index].Rect);
		}
	}
}

FHCMapHit UHCStationMapWidget::HitTest(const FVector2D& Local, const FVector2D& Size) const
{
	FScreen Screen;
	BuildScreen(Size, Screen);
	for (const FLayoutItem& Item : Screen.Items)
	{
		if (Item.Rect.IsInside(Local))
		{
			return Item.Hit;
		}
	}
	return FHCMapHit();
}

// ---------------------------------------------------------------------------
// Painting

void UHCStationMapWidget::PaintMap(const FHCDraw& D, const FHCPaintState& S) const
{
	using namespace HCMapLook;
	// Everything is drawn with one painter, so the layers stay in order; it fades in when the plan opens
	FHCDraw F = D;
	F.Opacity *= HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - OpenedAt), 0.f, 0.18f);
	const FVector2D Size = F.Size;
	const UWorld* World = GetWorld();
	const double Time = World ? World->GetTimeSeconds() : 0.0;

	FScreen Screen;
	BuildScreen(Size, Screen);
	FHCMapHit Hot;
	if (S.bHovered || S.bPressed)
	{
		for (const FLayoutItem& Item : Screen.Items)
		{
			if (Item.Rect.IsInside(S.Mouse))
			{
				Hot = Item.Hit;
				break;
			}
		}
	}
	HotItem = Hot;
#if !UE_BUILD_SHIPPING
	// -HCMapPick=task|door|sector: as if the mouse were on the first of them, for the pictures of the test rounds
	static const FString Pick = []
	{
		FString Value;
		FParse::Value(FCommandLine::Get(), TEXT("HCMapPick="), Value);
		return Value;
	}();
	for (const FLayoutItem& Item : Screen.Items)
	{
		const bool bMatch = Pick == TEXT("task") ? Item.Hit.Item == EHCMapItem::Task
			: Pick == TEXT("door") ? (Item.Hit.Item == EHCMapItem::Door || Item.Hit.Item == EHCMapItem::DoorLook)
			: Pick == TEXT("sector") && (Item.Hit.Item == EHCMapItem::Sector || Item.Hit.Item == EHCMapItem::CutPower);
		if (bMatch && Hot.Item == EHCMapItem::None)
		{
			Hot = Item.Hit;
		}
	}
#endif

	// The room behind the plan stays visible, dimmed: you are standing somewhere while you read it
	F.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Ink, 0.84f));
	F.Gradient(FVector2D(0.0, Size.Y * 0.7), FVector2D(Size.X, Size.Y * 0.3), HCLook::Alpha(HCLook::Ink, 0.f), HCLook::Alpha(HCLook::Ink, 0.7f), false);
	F.Brackets(FVector2D(28.0, 22.0), Size - FVector2D(56.0, 44.0), 30.f, 2.f, HCLook::Alpha(HCLook::Bone, 0.3f));

	// Head: which station, what the power is doing, where you are
	FText Place = NSLOCTEXT("HC", "MapKepler7Place", "Kepler-7");
	if (Screen.Layout)
	{
		Place = HCMaps::ForLayout(Screen.Layout->MapName).Place;
	}
	F.Box(FVector2D(Margin, 44.0), FVector2D(9.0, 9.0), Screen.bAnyDark ? HCLook::Blood : HCLook::Bone);
	F.Text(HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapTitleFmt", "PLAN  ·  {0}"), Place)), FVector2D(Margin + 22.0, 34.0), HCLook::Caps(24.f, 220), HCLook::Bone);
	FText Status = NSLOCTEXT("HC", "MapAllFine", "Alle Sektoren haben Strom.");
	for (const FScreen::FSectorRow& Sector : Screen.Sectors)
	{
		if (!Sector.bPowered)
		{
			Status = FText::Format(NSLOCTEXT("HC", "MapOutage", "Stromausfall in Sektor {0} – Sicherungskasten im Gang, sonst Notstrom in {1} s"),
				AHCPowerSector::GetDisplayName(Sector.Sector), Sector.Seconds);
		}
	}
	F.Text(Status.ToString(), FVector2D(Margin, 72.0), HCLook::Body(14.f), Screen.bAnyDark ? HCLook::Blood : HCLook::BoneMuted);
	F.Box(FVector2D(Margin, HeaderHeight - 12.0), FVector2D(Size.X - 2.0 * Margin, 1.0), HCLook::Alpha(HCLook::Bone, 0.25f));
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Screen.Builder && Pawn)
	{
		const FName Here = Screen.Builder->GetSpaceNameAt(Pawn->GetActorLocation());
		if (Screen.Layout->FindSpace(Here))
		{
			PlanTextRight(F, HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapWhereFmt", "Ort  ·  {0}"), HCText::Room(Here))), Size.X - Margin, 40.0, HCLook::Caps(15.f, 220), HCLook::Bone);
		}
	}

	if (Screen.Builder)
	{
		PaintPlan(F, Screen, Hot, Time);
		PaintPanel(F, S, Screen, Hot);
	}

	// What the station announces to everyone (a door was locked, the power is out): the HUD is hidden under the plan
	const AHCGameState* GS = World ? World->GetGameState<AHCGameState>() : nullptr;
	const FText Announcement = GS ? GS->GetAnnouncement() : FText::GetEmpty();
	if (!Announcement.IsEmpty())
	{
		const FSlateFontInfo Font = HCLook::Caps(14.f, 220);
		const FString Text = HCLook::Upper(Announcement);
		const FVector2D TextSize = F.MeasureExact(Text, Font);
		const FVector2D BoxSize(TextSize.X + 44.0, TextSize.Y + 18.0);
		const FVector2D Pos((Size.X - BoxSize.X) * 0.5, 30.0);
		F.Box(Pos, BoxSize, HCLook::Alpha(HCLook::Ink, 0.96f));
		F.Frame(Pos, BoxSize, 1.f, HCLook::Blood);
		F.Box(Pos, FVector2D(4.0, BoxSize.Y), HCLook::Blood);
		PlanTextCentered(F, Text, Pos.X + BoxSize.X * 0.5 + 2.0, Pos.Y + 9.0, Font, HCLook::Bone);
	}

	const FString Hint = HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapHintFmt", "{0} oder Esc schließt den Plan"), HCKeys::ShortName(EHCBinding::Map)));
	F.Text(Hint, FVector2D(Margin, Size.Y - FooterHeight + 20.0), HCLook::Mono(12.f, 160), HCLook::BoneMuted);
	F.ScanLines(0.1f);
	D.Layer = F.Layer;
}

void UHCStationMapWidget::PaintPlan(const FHCDraw& D, const FScreen& Screen, const FHCMapHit& Hot, double Time) const
{
	using namespace HCMapLook;
	const FHCStationLayout& Layout = *Screen.Layout;
	const double Scale = Screen.Scale;
	const float Blink = 0.6f + 0.4f * FMath::Sin(static_cast<float>(Time) * 7.f);
	const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Time) * 5.f);
	const APlayerController* PC = GetOwningPlayer();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FName Here = Pawn ? Screen.Builder->GetSpaceNameAt(Pawn->GetActorLocation()) : NAME_None;
	// The sector whose row the mouse is on: its rooms light up, in blood if a click would cut their power
	const bool bHotSector = Hot.Item == EHCMapItem::Sector || Hot.Item == EHCMapItem::CutPower;

	// Floors: rooms lighter than corridors, the space you stand in lighter still, a sector without power in blood
	for (int32 SpaceIndex = 0; SpaceIndex < Layout.Spaces.Num() && SpaceIndex < Shape.Floors.Num(); ++SpaceIndex)
	{
		const FHCSpace& Space = Layout.Spaces[SpaceIndex];
		const bool bDark = Screen.IsDark(Space.Sector);
		const bool bHot = bHotSector && static_cast<int32>(Space.Sector) == Hot.Index;
		const FLinearColor Fill = bDark ? HCLook::Alpha(HCLook::Blood, (Space.bCorridor ? 0.2f : 0.34f) + (bHot ? 0.12f : 0.f))
			: bHot && Hot.Item == EHCMapItem::CutPower ? HCLook::Alpha(HCLook::Blood, (Space.bCorridor ? 0.22f : 0.36f) + 0.1f * Pulse)
			: bHot ? HCLook::Alpha(HCLook::Bone, Space.bCorridor ? 0.26f : 0.38f)
			: Space.Name == Here ? HCLook::Alpha(HCLook::Bone, Space.bCorridor ? 0.2f : 0.28f)
			: HCLook::Alpha(HCLook::BoneMuted, Space.bCorridor ? 0.11f : 0.22f);
		for (const FIntRect& Part : Shape.Floors[SpaceIndex])
		{
			D.Box(Screen.ToScreen(FVector2D(Part.Min)), FVector2D(Part.Size()) * Scale, Fill);
		}
	}
	// Walls as thin strokes; the doors are the gaps in them
	const FLinearColor WallColor = HCLook::Alpha(HCLook::Bone, 0.55f);
	constexpr double WallWidth = 2.0;
	for (const TPair<FVector2D, FVector2D>& Wall : Shape.Walls)
	{
		const FVector2D A = Screen.ToScreen(Wall.Key), B = Screen.ToScreen(Wall.Value);
		D.Box(A - FVector2D(WallWidth, WallWidth) * 0.5, B - A + FVector2D(WallWidth, WallWidth), WallColor);
	}

	// Room names, with the power sector of the room under them
	for (const FHCSpace& Space : Layout.Spaces)
	{
		if (Space.bCorridor)
		{
			continue;
		}
		const FVector2D Min = Screen.ToScreen(FVector2D(Space.Rect.Min));
		const FVector2D RoomSize = FVector2D(Space.Rect.Size()) * Scale;
		const bool bDark = Screen.IsDark(Space.Sector);
		const bool bHere = Space.Name == Here;
		FSlateFontInfo Font = HCLook::Caps(12.f, 180);
		FString Name = HCLook::Upper(HCText::Room(Space.Name));
		if (D.MeasureExact(Name, Font).X > RoomSize.X - 10.0)
		{
			Name = HCLook::Upper(HCText::RoomShort(Space.Name));
		}
		if (D.MeasureExact(Name, Font).X > RoomSize.X - 10.0)
		{
			Font = HCLook::Caps(10.f, 60);
		}
		// In the middle of the room, like the name on its floor: the task stations stand at the walls.
		// A room without power gets the bolt above its name if it is high enough for both.
		const FString Sector = HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapSector", "Sektor {0}"), AHCPowerSector::GetDisplayName(static_cast<uint8>(Space.Sector))));
		const FSlateFontInfo SectorFont = HCLook::Mono(9.f, 140);
		constexpr double BoltRoom = 30.0;
		bool bSectorLine = RoomSize.Y >= 62.0 && D.MeasureExact(Sector, SectorFont).X <= RoomSize.X - 10.0;
		if (bDark && bSectorLine && RoomSize.Y < 35.0 + BoltRoom + 12.0)
		{
			bSectorLine = false;
		}
		const double BlockHeight = bSectorLine ? 35.0 : 18.0;
		const bool bBolt = bDark && RoomSize.Y >= BlockHeight + BoltRoom + 8.0;
		const double CenterX = Min.X + RoomSize.X * 0.5;
		const double Top = Min.Y + (RoomSize.Y - BlockHeight + (bBolt ? BoltRoom : 0.0)) * 0.5;
		PlanTextCentered(D, Name, CenterX, Top, Font, bHere || bDark ? HCLook::Bone : HCLook::Alpha(HCLook::Bone, 0.72f));
		if (bSectorLine)
		{
			PlanTextCentered(D, Sector, CenterX, Top + 20.0, SectorFont, bDark ? HCLook::Alpha(HCLook::Blood, 0.95f) : HCLook::Alpha(HCLook::BoneMuted, 0.6f));
		}
		if (bBolt)
		{
			PlanBolt(D, FVector2D(CenterX, Top - BoltRoom * 0.5 - 2.0), 11.0, HCLook::Alpha(HCLook::Bone, Blink));
		}
	}
	// Where the power comes back: the fuse box of a dark sector, framed and blinking
	for (const FHCFuseBoxDef& Box : Layout.FuseBoxes)
	{
		if (Screen.IsDark(Box.Sector))
		{
			const FVector2D At = Screen.ToScreen(Box.Position);
			const FLinearColor Edge = HCLook::Alpha(HCLook::Bone, Blink);
			D.Box(At - FVector2D(12.0, 15.0), FVector2D(24.0, 30.0), HCLook::Alpha(HCLook::Ink, 0.9f));
			D.Frame(At - FVector2D(12.0, 15.0), FVector2D(24.0, 30.0), 1.5f, Edge);
			PlanBolt(D, At, 9.0, Edge);
			const FSlateFontInfo Font = HCLook::Caps(10.f, 180);
			const FString Label = HCLook::Upper(NSLOCTEXT("HC", "MapFuse", "Sicherung"));
			const FVector2D LabelSize = FHCDraw::Measure(Label, Font);
			D.Box(At + FVector2D(16.0, -LabelSize.Y * 0.5 - 3.0), FVector2D(LabelSize.X + 10.0, LabelSize.Y + 6.0), HCLook::Alpha(HCLook::Ink, 0.85f));
			D.Text(Label, At + FVector2D(21.0, -LabelSize.Y * 0.5), Font, HCLook::Bone);
		}
	}

	// The Wandler's doors: a blood bar across the opening, solid while the door is locked
	for (const FScreen::FDoorRow& Door : Screen.Doors)
	{
		if (!Door.bOnPlan)
		{
			continue;
		}
		const bool bHot = (Hot.Item == EHCMapItem::Door || Hot.Item == EHCMapItem::DoorLook) && Hot.Id == Door.Id;
		const FVector2D Min = FVector2D::Min(Door.From, Door.To), Max = FVector2D::Max(Door.From, Door.To);
		const double Thick = Door.bLocked || bHot ? 8.0 : 5.0;
		const float A = Door.bLocked ? 1.f : bHot ? 0.75f + 0.25f * Pulse : Screen.CanLockDoor() ? 0.8f : 0.4f;
		D.Box(Min - FVector2D(Thick, Thick) * 0.5, Max - Min + FVector2D(Thick, Thick), HCLook::Alpha(HCLook::Blood, A));
		if (bHot)
		{
			D.Brackets(Min - FVector2D(12.0, 12.0), Max - Min + FVector2D(24.0, 24.0), 8.f, 2.f, HCLook::Bone);
		}
	}

	// Your own open tasks, and nobody else's
	for (int32 Index = 0; Index < Screen.Tasks.Num(); ++Index)
	{
		const FScreen::FTaskRow& Task = Screen.Tasks[Index];
		if (!Task.bOnPlan)
		{
			continue;
		}
		D.Box(Task.PlanAt - FVector2D(7.5, 7.5), FVector2D(15.0, 15.0), HCLook::Alpha(HCLook::Ink, 0.92f));
		PlanTaskBox(D, Task.PlanAt, 11.0, Task.Wait, false);
		if (Hot.Item == EHCMapItem::Task && Hot.Index == Index)
		{
			const double Grow = 13.0 + 3.0 * Pulse;
			D.Brackets(Task.PlanAt - FVector2D(Grow, Grow), FVector2D(Grow, Grow) * 2.0, 7.f, 2.f, Task.Wait > 0.f ? HCLook::GhostBlue : HCLook::Bone);
		}
	}

	// You
	if (Pawn)
	{
		const FVector Local = Screen.Builder->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation()) / 100.0;
		const double Yaw = FMath::DegreesToRadians(PC->GetControlRotation().Yaw - Screen.Builder->GetActorRotation().Yaw);
		PlanArrow(D, Screen.ToScreen(FVector2D(Local.X, Local.Y)), Yaw, 11.0, HCLook::Bone);
	}

	// What the mouse is on, in words
	if (Hot.Item == EHCMapItem::Task && Screen.Tasks.IsValidIndex(Hot.Index) && Screen.Tasks[Hot.Index].bOnPlan)
	{
		const FScreen::FTaskRow& Task = Screen.Tasks[Hot.Index];
		PlanTag(D, Task.PlanAt, Task.Name.ToUpper(), HCLook::Alpha(Task.Wait > 0.f ? HCLook::GhostBlue : HCLook::Bone, 0.8f));
	}
	else if (Hot.Item == EHCMapItem::Door || Hot.Item == EHCMapItem::DoorLook)
	{
		const FScreen::FDoorRow* Door = Screen.Doors.FindByPredicate([&Hot](const FScreen::FDoorRow& Row) { return Row.Id == Hot.Id; });
		if (Door && Door->bOnPlan)
		{
			const FText Does = Door->bLocked ? NSLOCTEXT("HC", "MapDoorLocked", "VERRIEGELT") : NSLOCTEXT("HC", "MapLockDoor", "VERRIEGELN");
			PlanTag(D, (Door->From + Door->To) * 0.5, Hot.Item == EHCMapItem::Door || Door->bLocked ? FString::Printf(TEXT("%s  ·  %s"), *Door->Name, *HCLook::Upper(Does)) : Door->Name, HCLook::Blood);
		}
	}

	// Legend: in the footer line, on the right
	{
		const FSlateFontInfo Font = HCLook::Caps(10.f, 180);
		const FLinearColor Color = HCLook::Alpha(HCLook::BoneMuted, 0.9f);
		constexpr double SignWidth = 24.0;
		constexpr double Gap = 26.0;
		TArray<TPair<FString, TFunction<void(const FVector2D&)>>> Entries;
		Entries.Emplace(HCLook::Upper(NSLOCTEXT("HC", "MapLegendYou", "Du")), [&D](const FVector2D& At)
		{
			D.Fan({ At + FVector2D(7.0, 0.0), At + FVector2D(-5.0, -5.5), At + FVector2D(-2.0, 0.0) }, HCLook::Bone);
			D.Fan({ At + FVector2D(7.0, 0.0), At + FVector2D(-2.0, 0.0), At + FVector2D(-5.0, 5.5) }, HCLook::Bone);
		});
		if (!Screen.Tasks.IsEmpty())
		{
			Entries.Emplace(HCLook::Upper(NSLOCTEXT("HC", "MapLegendTask", "Aufgabe")), [&D](const FVector2D& At) { PlanTaskBox(D, At, 10.0, 0.f, false); });
			Entries.Emplace(HCLook::Upper(NSLOCTEXT("HC", "MapLegendLater", "Noch nicht bereit")), [&D](const FVector2D& At) { PlanTaskBox(D, At, 10.0, 1.f, false); });
		}
		Entries.Emplace(HCLook::Upper(NSLOCTEXT("HC", "MapLegendNoPower", "Kein Strom")), [&D](const FVector2D& At)
		{
			D.Box(At - FVector2D(8.0, 7.0), FVector2D(16.0, 14.0), HCLook::Alpha(HCLook::Blood, 0.4f));
			PlanBolt(D, At, 6.0, HCLook::Bone);
		});
		if (!Screen.Doors.IsEmpty())
		{
			Entries.Emplace(HCLook::Upper(NSLOCTEXT("HC", "MapLegendDoor", "Tür zum Verriegeln")), [&D](const FVector2D& At) { D.Box(At - FVector2D(8.0, 2.5), FVector2D(16.0, 5.0), HCLook::Blood); });
		}
		double Total = -Gap;
		for (const TPair<FString, TFunction<void(const FVector2D&)>>& Entry : Entries)
		{
			Total += SignWidth + D.MeasureExact(Entry.Key, Font).X + Gap;
		}
		const double Y = D.Size.Y - FooterHeight + 21.0;
		double X = D.Size.X - Margin - Total;
		for (const TPair<FString, TFunction<void(const FVector2D&)>>& Entry : Entries)
		{
			Entry.Value(FVector2D(X + 8.0, Y + 7.0));
			X += SignWidth;
			D.Text(Entry.Key, FVector2D(X, Y), Font, Color);
			X += D.MeasureExact(Entry.Key, Font).X + Gap;
		}
	}
}

void UHCStationMapWidget::PaintPanel(const FHCDraw& D, const FHCPaintState& S, const FScreen& Screen, const FHCMapHit& Hot) const
{
	using namespace HCMapLook;
	const double X = Screen.PanelPos.X;
	const double Time = S.Time;
	const float Blink = 0.6f + 0.4f * FMath::Sin(static_cast<float>(Time) * 7.f);
	// On the right of a head, for the Wandler: when this kind of sabotage is ready (nothing while one is running)
	auto Ready = [](int32 Wait, bool bRunning)
	{
		return Wait > 0 ? HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapIn", "in {0} s"), Wait)) : bRunning ? FString() : HCLook::Upper(NSLOCTEXT("HC", "MapNow", "bereit"));
	};
	// The chip on the right of a row the Wandler can click: what the click does
	auto Chip = [&D](const FBox2D& Row, const FString& Label, bool bEnabled, bool bHot, bool bPressed)
	{
		const FSlateFontInfo Font = HCLook::Caps(10.f, 200);
		const FVector2D TextSize = D.MeasureExact(Label, Font);
		const FVector2D Size(TextSize.X + 22.0, 24.0);
		const FVector2D Pos(Row.Max.X - 12.0 - Size.X, (Row.Min.Y + Row.Max.Y - Size.Y) * 0.5);
		const FLinearColor Edge = bEnabled ? HCLook::Blood : HCLook::Alpha(HCLook::BoneMuted, 0.5f);
		if (bEnabled && bHot)
		{
			D.Box(Pos, Size, HCLook::Alpha(HCLook::Blood, bPressed ? 0.78f : 1.f));
		}
		else
		{
			D.Frame(Pos, Size, 1.f, HCLook::Alpha(Edge, 0.85f));
			D.Box(Pos + FVector2D(0.0, Size.Y - 2.0), FVector2D(Size.X, 2.0), HCLook::Alpha(Edge, 0.85f));
		}
		PlanTextCentered(D, Label, Pos.X + Size.X * 0.5, Pos.Y + (Size.Y - TextSize.Y) * 0.5, Font, bEnabled && bHot ? HCLook::Bone : Edge);
	};

	// Power
	PanelHead(D, FVector2D(X, Screen.PowerHeadY), PanelWidth, HCLook::Upper(NSLOCTEXT("HC", "MapPower", "Strom")), HCLook::Bone,
		Screen.bMonster ? Ready(Screen.PowerWait, Screen.bAnyDark) : FString(), Screen.CanCutPower() ? HCLook::Blood : HCLook::BoneMuted);
	for (const FScreen::FSectorRow& Sector : Screen.Sectors)
	{
		const FVector2D Pos = Sector.Rect.Min, Size = Sector.Rect.GetSize();
		const bool bHot = (Hot.Item == EHCMapItem::Sector || Hot.Item == EHCMapItem::CutPower) && Hot.Index == Sector.Sector;
		const bool bCut = bHot && Hot.Item == EHCMapItem::CutPower;
		const bool bPressed = bCut && S.bPressed && PressedItem == Hot;
		D.Box(Pos, Size, HCLook::Alpha(HCLook::Ink, 0.6f));
		if (bHot)
		{
			D.Box(Pos, Size, bCut ? HCLook::Alpha(HCLook::Blood, bPressed ? 0.22f : 0.12f) : HCLook::Alpha(HCLook::Bone, 0.06f));
		}
		D.Frame(Pos, Size, 1.f, bCut ? HCLook::Blood : bHot ? HCLook::Alpha(HCLook::Bone, 0.6f) : HCLook::Alpha(HCLook::BoneMuted, 0.3f));
		D.Box(Pos, FVector2D(4.0, Size.Y), Sector.bPowered ? HCLook::Alpha(HCLook::Signal, 0.85f) : HCLook::Alpha(HCLook::Blood, Blink));
		D.Text(HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapSector", "Sektor {0}"), AHCPowerSector::GetDisplayName(Sector.Sector))), Pos + FVector2D(18.0, 8.0), HCLook::Caps(13.f, 200), HCLook::Bone);
		const FString State = Sector.bPowered ? HCLook::Upper(NSLOCTEXT("HC", "MapPowerOn", "Strom an"))
			: HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MapNoPowerFmt", "Stromausfall  ·  Notstrom in {0} s"), Sector.Seconds));
		D.Text(State, Pos + FVector2D(18.0, 29.0), HCLook::Mono(10.f, 140), Sector.bPowered ? HCLook::BoneMuted : HCLook::Blood);
		if (Screen.bMonster && Sector.bPowered)
		{
			Chip(Sector.Rect, HCLook::Upper(NSLOCTEXT("HC", "MapCutPower", "STROM KAPPEN")), Screen.CanCutPower(), bCut, bPressed);
		}
	}
	if (Screen.Layout)
	{
		const FSlateFontInfo NoteFont = HCLook::Body(12.f);
		const FString Note = FText::Format(NSLOCTEXT("HC", "MapCommandFmt", "{0}: eigener Notstrom, fällt nie aus"), HCText::Room(Screen.Layout->SpawnRoom)).ToString();
		D.Text(FHCDraw::Fit(Note, NoteFont, PanelWidth), FVector2D(X, Screen.NoteY), NoteFont, HCLook::Alpha(HCLook::BoneMuted, 0.85f));
	}

	// Doors (Wandler)
	if (Screen.DoorHeadY >= 0.0)
	{
		PanelHead(D, FVector2D(X, Screen.DoorHeadY), PanelWidth, HCLook::Upper(NSLOCTEXT("HC", "MapDoors", "Türen")), HCLook::Bone,
			Ready(Screen.DoorWait, Screen.bAnyLocked), Screen.CanLockDoor() ? HCLook::Blood : HCLook::BoneMuted);
		for (const FScreen::FDoorRow& Door : Screen.Doors)
		{
			const FVector2D Pos = Door.Rect.Min, Size = Door.Rect.GetSize();
			const bool bLook = Hot.Item == EHCMapItem::DoorLook && Hot.Id == Door.Id;
			const bool bHot = Hot.Item == EHCMapItem::Door && Hot.Id == Door.Id;
			const bool bPressed = bHot && S.bPressed && PressedItem == Hot;
			D.Box(Pos, Size, HCLook::Alpha(HCLook::Ink, 0.6f));
			if (bHot || bLook)
			{
				D.Box(Pos, Size, bHot ? HCLook::Alpha(HCLook::Blood, bPressed ? 0.22f : 0.12f) : HCLook::Alpha(HCLook::Bone, 0.06f));
			}
			D.Frame(Pos, Size, 1.f, bHot || Door.bLocked ? HCLook::Blood : bLook ? HCLook::Alpha(HCLook::Bone, 0.6f) : HCLook::Alpha(HCLook::BoneMuted, 0.3f));
			D.Box(Pos, FVector2D(4.0, Size.Y), HCLook::Alpha(HCLook::Blood, Door.bLocked ? 1.f : 0.45f));
			const FSlateFontInfo Font = HCLook::Caps(12.f, 180);
			const FString Name = FHCDraw::Fit(Door.Name, Font, PanelWidth - 180.0);
			D.Text(Name, Pos + FVector2D(18.0, (Size.Y - FHCDraw::Measure(Name, Font).Y) * 0.5), Font, HCLook::Bone);
			if (Door.bLocked)
			{
				const FSlateFontInfo StateFont = HCLook::Caps(10.f, 200);
				const FString State = HCLook::Upper(NSLOCTEXT("HC", "MapDoorLocked", "VERRIEGELT"));
				PlanTextRight(D, State, Door.Rect.Max.X - 14.0, Pos.Y + (Size.Y - FHCDraw::Measure(State, StateFont).Y) * 0.5, StateFont, HCLook::Blood);
			}
			else
			{
				Chip(Door.Rect, HCLook::Upper(NSLOCTEXT("HC", "MapLockDoor", "VERRIEGELN")), Screen.CanLockDoor(), bHot, bPressed);
			}
		}
	}

	// Tasks: the list of the HUD, and the mouse on a row shows where it is
	if (Screen.TaskHeadY >= 0.0)
	{
		const bool bAllDone = Screen.TasksDone >= Screen.Tasks.Num();
		PanelHead(D, FVector2D(X, Screen.TaskHeadY), PanelWidth, HCLook::Upper(NSLOCTEXT("HC", "HudTasksTitle", "AUFGABEN")), HCLook::Bone,
			FString::Printf(TEXT("%d/%d"), Screen.TasksDone, Screen.Tasks.Num()), bAllDone ? HCLook::Signal : HCLook::BoneMuted);
		for (int32 Index = 0; Index < Screen.Tasks.Num(); ++Index)
		{
			const FScreen::FTaskRow& Task = Screen.Tasks[Index];
			if (!Task.Rect.bIsValid)
			{
				continue;
			}
			const FVector2D Pos = Task.Rect.Min, Size = Task.Rect.GetSize();
			const bool bHot = Hot.Item == EHCMapItem::Task && Hot.Index == Index;
			if (bHot && !Task.bDone)
			{
				D.Box(Pos - FVector2D(8.0, 0.0), Size + FVector2D(16.0, 0.0), HCLook::Alpha(HCLook::Bone, 0.08f));
			}
			const FLinearColor Color = Task.bDone ? HCLook::Alpha(HCLook::BoneMuted, 0.6f) : Task.Wait > 0.f ? HCLook::GhostBlue : HCLook::Bone;
			PlanTaskBox(D, Pos + FVector2D(5.0, Size.Y * 0.5), 9.0, Task.Wait, Task.bDone);
			const FSlateFontInfo WhereFont = HCLook::Caps(10.f, 160);
			const double WhereWidth = Task.Where.IsEmpty() ? 0.0 : D.MeasureExact(Task.Where, WhereFont).X;
			const FSlateFontInfo NameFont = HCLook::Body(13.f);
			const FString Name = FHCDraw::Fit(Task.Name, NameFont, PanelWidth - 22.0 - WhereWidth - 16.0);
			const FVector2D NameSize = FHCDraw::Measure(Name, NameFont);
			D.Text(Name, Pos + FVector2D(22.0, (Size.Y - NameSize.Y) * 0.5), NameFont, Color);
			if (Task.bDone)
			{
				// Crossed off
				D.Box(Pos + FVector2D(20.0, Size.Y * 0.5), FVector2D(NameSize.X + 4.0, 1.0), HCLook::Alpha(HCLook::BoneMuted, 0.55f));
			}
			else if (!Task.Where.IsEmpty())
			{
				PlanTextRight(D, Task.Where, Task.Rect.Max.X, Pos.Y + (Size.Y - FHCDraw::Measure(Task.Where, WhereFont).Y) * 0.5, WhereFont, Task.Wait > 0.f ? HCLook::GhostBlue : HCLook::BoneMuted);
			}
		}
	}
}
