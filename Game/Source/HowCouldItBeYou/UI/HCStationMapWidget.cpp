#include "UI/HCStationMapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
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

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(20.f, 16.f));
	List = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(List);
	HCUI::AddRow(Box, Panel, 10.f);

	MonsterText = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::AccentRed, true);
	MonsterText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, MonsterText, 6.f, HAlign_Fill);

	UTextBlock* Hint = HCUI::Text(Tree, NSLOCTEXT("HC", "MapHint", "M oder Esc schließt den Plan"), 13, HCUI::MutedColor);
	Hint->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Hint, 0.f, HAlign_Fill);

	HCUI::Place(Root, Box, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 560.f);
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
	if (Key == EKeys::M || Key == EKeys::Escape || Key == EKeys::F1)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
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
