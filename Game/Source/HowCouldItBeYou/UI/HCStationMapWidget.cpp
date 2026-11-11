#include "UI/HCStationMapWidget.h"

#include "Core/HCText.h"
#include "Core/HCKeyBindings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "EngineUtils.h"
#include "Sabotage/HCDoor.h"
#include "Sabotage/HCPowerSector.h"
#include "Sabotage/HCSabotageManager.h"
#include "UI/HCHUD.h"
#include "UI/HCUIStyle.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "World/HCStationBuilder.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"

void UHCMapClickProxy::HandleClicked()
{
	if (UHCStationMapWidget* Widget = Owner.Get())
	{
		Widget->RequestSabotage(SabotageType, TargetId);
	}
}

void UHCStationMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.01f, 0.02f, 0.55f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = HCUI::Text(Tree, NSLOCTEXT("HC", "MapTitle", "STATIONSPLAN KEPLER-7"), 26, HCUI::TextColor, true);
	Title->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Title, 6.f, HAlign_Fill);

	InfoText = HCUI::Text(Tree, FText::GetEmpty(), 15, HCUI::MutedColor);
	InfoText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, InfoText, 12.f, HAlign_Fill);

	MapArea = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	MapArea->SetHeightOverride(330.f);
	MapArea->AddChild(HCUI::Panel(Tree, FLinearColor(0.f, 0.f, 0.f, 0.55f), FMargin(0.f)));
	HCUI::AddRow(Box, MapArea, 10.f);

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(20.f, 16.f));
	List = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(List);
	HCUI::AddRow(Box, Panel, 10.f);

	MonsterText = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::AccentRed, true);
	MonsterText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, MonsterText, 6.f, HAlign_Fill);

	UTextBlock* Hint = HCUI::Text(Tree, FText::Format(NSLOCTEXT("HC", "MapHintFmt", "{0} oder Esc schließt den Plan"), HCKeys::ShortName(EHCBinding::Map)), 13, HCUI::MutedColor);
	Hint->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Hint, 0.f, HAlign_Fill);

	HCUI::Place(Root, Box, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 600.f);
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

int32 UHCStationMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const AHCStationBuilder* Builder = nullptr;
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		Builder = *It;
	}
	if (!MapArea || !Builder || Builder->GetLayout().SizeX <= 0)
	{
		return MaxLayer;
	}
	const FHCStationLayout& Layout = Builder->GetLayout();
	// Map box in the local space of this widget (same approach as the signal curve of the task menu)
	const FGeometry& Area = MapArea->GetPaintSpaceGeometry();
	const FVector2D AreaSize = FVector2D(Area.GetAbsoluteSize()) / FMath::Max(0.01f, AllottedGeometry.Scale);
	if (AreaSize.X < 20.0 || AreaSize.Y < 20.0)
	{
		return MaxLayer; // not laid out yet
	}
	const FVector2D TopLeft = AllottedGeometry.AbsoluteToLocal(FVector2D(Area.GetAbsolutePosition()));
	const double Scale = FMath::Min((AreaSize.X - 24.0) / Layout.SizeX, (AreaSize.Y - 24.0) / Layout.SizeY);
	const FVector2D Origin = TopLeft + (AreaSize - FVector2D(Layout.SizeX, Layout.SizeY) * Scale) * 0.5;

	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);
	const int32 BoxLayer = ++MaxLayer;
	const int32 TextLayer = ++MaxLayer;
	for (const FHCSpace& Space : Layout.Spaces)
	{
		// A sector without power is shown dark red - the same information as the list below
		const AHCPowerSector* Sector = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Space.Sector));
		const bool bDark = Sector && !Sector->IsPowered();
		const FLinearColor Color = bDark ? (Space.bCorridor ? FLinearColor(0.30f, 0.06f, 0.06f, 0.95f) : FLinearColor(0.45f, 0.08f, 0.08f, 0.95f))
			: (Space.bCorridor ? FLinearColor(0.20f, 0.23f, 0.28f, 0.95f) : FLinearColor(0.33f, 0.38f, 0.46f, 0.95f));
		const FVector2D Position = Origin + FVector2D(Space.Rect.Min.X, Space.Rect.Min.Y) * Scale;
		const FVector2D Size(Space.Rect.Width() * Scale - 1.0, Space.Rect.Height() * Scale - 1.0);
		FSlateDrawElement::MakeBox(OutDrawElements, BoxLayer, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), White, ESlateDrawEffect::None, Color);
		if (!Space.bCorridor)
		{
			FSlateDrawElement::MakeText(OutDrawElements, TextLayer, AllottedGeometry.ToPaintGeometry(FVector2D(Size.X, 14.0), FSlateLayoutTransform(Position + FVector2D(3.0, 2.0))),
				HCText::Room(Space.Name).ToString(), Font, ESlateDrawEffect::None, FLinearColor(0.92f, 0.94f, 0.97f, 0.95f));
		}
	}
	// No power: a lightning bolt in every room of the dark sector, and a framed one at the fuse box that brings it back
	{
		const int32 BoltLayer = ++MaxLayer;
		const float Blink = 0.6f + 0.4f * FMath::Sin(GetWorld()->GetTimeSeconds() * 7.f);
		auto DrawBolt = [&](const FVector2D& At, double Size, const FLinearColor& Color)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, BoltLayer, AllottedGeometry.ToPaintGeometry(FVector2D(Size * 1.8, Size * 2.6), FSlateLayoutTransform(At - FVector2D(Size * 0.9, Size * 1.3))),
				White, ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.8f));
			const TArray<FVector2D> Points = { At + FVector2D(0.3, -1.0) * Size, At + FVector2D(-0.4, 0.12) * Size, At + FVector2D(0.4, -0.12) * Size, At + FVector2D(-0.3, 1.0) * Size };
			FSlateDrawElement::MakeLines(OutDrawElements, BoltLayer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 2.5f);
		};
		for (const FHCSpace& Space : Layout.Spaces)
		{
			const AHCPowerSector* Sector = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Space.Sector));
			if (!Space.bCorridor && Sector && !Sector->IsPowered())
			{
				DrawBolt(Origin + FVector2D(Space.Rect.Min.X + Space.Rect.Width() * 0.5, Space.Rect.Min.Y + Space.Rect.Height() * 0.5 + 0.8) * Scale, 11.0, FLinearColor(1.f, 0.85f, 0.15f, Blink));
			}
		}
		for (const FHCFuseBoxDef& Box : Layout.FuseBoxes)
		{
			const AHCPowerSector* Sector = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Box.Sector));
			if (Sector && !Sector->IsPowered())
			{
				const FVector2D At = Origin + Box.Position * Scale;
				const FLinearColor Frame(1.f, 0.55f, 0.1f, Blink);
				DrawBolt(At, 9.0, Frame);
				const TArray<FVector2D> Outline = { At + FVector2D(-11.0, -14.0), At + FVector2D(11.0, -14.0), At + FVector2D(11.0, 14.0), At + FVector2D(-11.0, 14.0), At + FVector2D(-11.0, -14.0) };
				FSlateDrawElement::MakeLines(OutDrawElements, BoltLayer, AllottedGeometry.ToPaintGeometry(), Outline, ESlateDrawEffect::None, Frame, true, 2.f);
				FSlateDrawElement::MakeText(OutDrawElements, BoltLayer, AllottedGeometry.ToPaintGeometry(FVector2D(120.0, 14.0), FSlateLayoutTransform(At + FVector2D(15.0, -7.0))),
					FString(TEXT("Sicherung")), Font, ESlateDrawEffect::None, FLinearColor(1.f, 0.8f, 0.5f, 1.f));
			}
		}
	}
	// Your own open tasks (yellow; dim while a step is not ready yet)
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* OwnState = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	if (const UHCTaskComponent* OwnTasks = OwnState ? OwnState->GetTaskComponent() : nullptr)
	{
		const int32 TaskLayer = ++MaxLayer;
		for (TActorIterator<AHCTaskStation> It(GetWorld()); It; ++It)
		{
			float Wait = 0.f;
			if (OwnTasks->FindPendingIndexAt(*It, &Wait) != INDEX_NONE)
			{
				const FVector At = Builder->GetActorTransform().InverseTransformPosition(It->GetActorLocation()) / 100.0;
				FSlateDrawElement::MakeBox(OutDrawElements, TaskLayer, AllottedGeometry.ToPaintGeometry(FVector2D(8.0, 8.0),
					FSlateLayoutTransform(Origin + FVector2D(At.X, At.Y) * Scale - FVector2D(4.0, 4.0))), White, ESlateDrawEffect::None,
					Wait > 0.f ? FLinearColor(0.6f, 0.5f, 0.15f, 0.95f) : FLinearColor(1.f, 0.85f, 0.2f, 1.f));
			}
		}
	}
	// Your own position
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		const FVector Local = Builder->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation()) / 100.0;
		const FVector2D Me = Origin + FVector2D(Local.X, Local.Y) * Scale;
		FSlateDrawElement::MakeBox(OutDrawElements, ++MaxLayer, AllottedGeometry.ToPaintGeometry(FVector2D(8.0, 8.0), FSlateLayoutTransform(Me - FVector2D(4.0, 4.0))),
			White, ESlateDrawEffect::None, FLinearColor(0.93f, 0.89f, 0.83f, 1.f));
	}
	return MaxLayer;
}

void UHCStationMapWidget::Rebuild()
{
	List->ClearChildren();
	Proxies.Reset();
	const bool bMonster = IsOwnerMonster();

	auto AddButton = [this](UHorizontalBox* Row, const FText& Label, EHCSabotageType Type, FName Target)
	{
		UButton* Button = HCUI::Button(WidgetTree, Label, HCUI::AccentRed, nullptr, 12);
		UHCMapClickProxy* Proxy = NewObject<UHCMapClickProxy>(this);
		Proxy->SabotageType = static_cast<uint8>(Type);
		Proxy->TargetId = Target;
		Proxy->Owner = this;
		Proxies.Add(Proxy);
		Button->OnClicked.AddDynamic(Proxy, &UHCMapClickProxy::HandleClicked);
		HCUI::AddColumn(Row, Button, 0.f);
	};

	TArray<const AHCPowerSector*> Sectors;
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		Sectors.Add(*It);
	}
	Sectors.Sort([](const AHCPowerSector& A, const AHCPowerSector& B) { return A.GetSectorIndex() < B.GetSectorIndex(); });
	for (const AHCPowerSector* Sector : Sectors)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Row, HCUI::Swatch(WidgetTree, Sector->IsPowered() ? HCUI::AccentGreen : HCUI::AccentRed, 16.f), 12.f);
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::Format(NSLOCTEXT("HC", "MapSector", "Sektor {0}"), AHCPowerSector::GetDisplayName(Sector->GetSectorIndex())), 17, HCUI::TextColor, true), 12.f, true);
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, Sector->IsPowered() ? NSLOCTEXT("HC", "MapPowerOn", "Strom an") : NSLOCTEXT("HC", "MapPowerOff", "STROMAUSFALL"),
			14, Sector->IsPowered() ? HCUI::MutedColor : HCUI::AccentRed), bMonster ? 14.f : 0.f);
		if (bMonster && Sector->IsPowered())
		{
			AddButton(Row, NSLOCTEXT("HC", "MapCutPower", "STROM KAPPEN"), EHCSabotageType::PowerOutage, AHCSabotageManager::SectorTargetId(Sector->GetSectorIndex()));
		}
		HCUI::AddRow(List, Row, 9.f);
	}
	HCUI::AddRow(List, HCUI::Text(WidgetTree, NSLOCTEXT("HC", "MapCommand", "Kommandozentrale: eigener Notstrom, fällt nie aus"), 13, HCUI::MutedColor), bMonster ? 14.f : 0.f);

	if (bMonster)
	{
		// Only the Wandler sees the doors that can be locked
		TArray<const AHCDoor*> Doors;
		for (TActorIterator<AHCDoor> It(GetWorld()); It; ++It)
		{
			if (It->bSabotageable)
			{
				Doors.Add(*It);
			}
		}
		Doors.Sort([](const AHCDoor& A, const AHCDoor& B) { return A.DoorId.LexicalLess(B.DoorId); });
		for (const AHCDoor* Door : Doors)
		{
			const bool bLocked = Door->GetDoorState() == EHCDoorState::Locked;
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::Format(NSLOCTEXT("HC", "MapDoor", "Tür {0}"), Door->GetDisplayName()), 15, HCUI::TextColor), 12.f, true);
			if (bLocked)
			{
				HCUI::AddColumn(Row, HCUI::Text(WidgetTree, NSLOCTEXT("HC", "MapDoorLocked", "VERRIEGELT"), 13, HCUI::AccentRed), 0.f);
			}
			else
			{
				AddButton(Row, NSLOCTEXT("HC", "MapLockDoor", "VERRIEGELN"), EHCSabotageType::DoorLock, Door->DoorId);
			}
			HCUI::AddRow(List, Row, 7.f);
		}
	}
}

void UHCStationMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	const bool bMonster = IsOwnerMonster();
	uint32 Signature = bMonster ? 31 : 17;
	FText Outage;
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		Signature = HashCombine(Signature, GetTypeHash(It->GetSectorIndex() * 2 + (It->IsPowered() ? 1 : 0)));
		if (!It->IsPowered())
		{
			Outage = FText::Format(NSLOCTEXT("HC", "MapOutage", "Stromausfall in Sektor {0} – Sicherungskasten im Gang, sonst Notstrom in {1} s"),
				AHCPowerSector::GetDisplayName(It->GetSectorIndex()), FMath::CeilToInt(It->GetSecondsUntilAutoRestore()));
		}
	}
	for (TActorIterator<AHCDoor> It(GetWorld()); bMonster && It; ++It)
	{
		if (It->bSabotageable)
		{
			Signature = HashCombine(Signature, GetTypeHash(It->DoorId));
			Signature = HashCombine(Signature, GetTypeHash(static_cast<uint8>(It->GetDoorState() == EHCDoorState::Locked ? 1 : 0)));
		}
	}
	if (Signature != ShownSignature)
	{
		ShownSignature = Signature;
		Rebuild();
	}
	InfoText->SetText(Outage.IsEmpty() ? NSLOCTEXT("HC", "MapAllFine", "Alle Sektoren haben Strom.") : Outage);
	InfoText->SetColorAndOpacity(FSlateColor(Outage.IsEmpty() ? HCUI::MutedColor : HCUI::AccentRed));

	FText Monster;
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* PS = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	if (bMonster && PS)
	{
		const double Now = GS->GetServerWorldTimeSeconds();
		const int32 Wait = FMath::CeilToInt(FMath::Max(0.0, PS->GetSabotageEarliestTime() - Now));
		const int32 Power = FMath::CeilToInt(FMath::Max(0.0, FMath::Max(PS->GetSabotageEarliestTime(), PS->GetPowerSabotageReadyTime()) - Now));
		const int32 Door = FMath::CeilToInt(FMath::Max(0.0, FMath::Max(PS->GetSabotageEarliestTime(), PS->GetDoorSabotageReadyTime()) - Now));
		Monster = Wait > 0 ? FText::Format(NSLOCTEXT("HC", "MapWait", "Sabotage erst in {0} s möglich"), Wait)
			: FText::Format(NSLOCTEXT("HC", "MapReady", "Strom: {0}  ·  Tür: {1}"),
				Power > 0 ? FText::Format(NSLOCTEXT("HC", "MapIn", "in {0} s"), Power) : NSLOCTEXT("HC", "MapNow", "bereit"),
				Door > 0 ? FText::Format(NSLOCTEXT("HC", "MapIn", "in {0} s"), Door) : NSLOCTEXT("HC", "MapNow", "bereit"));
	}
	MonsterText->SetText(Monster);
}
