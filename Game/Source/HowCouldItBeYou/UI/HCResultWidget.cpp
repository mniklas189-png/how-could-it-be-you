#include "UI/HCResultWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerController.h"
#include "UI/HCUIStyle.h"

void UHCResultWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, HCUI::Background);

	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	WinnerText = HCUI::Text(Tree, FText::GetEmpty(), 44, HCUI::TextColor, true);
	WinnerText->SetJustification(ETextJustify::Center);
	WinnerText->SetAutoWrapText(false);
	HCUI::AddRow(Box, WinnerText, 6.f, HAlign_Fill);
	ReasonText = HCUI::Text(Tree, FText::GetEmpty(), 18, HCUI::MutedColor);
	ReasonText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, ReasonText, 30.f, HAlign_Fill);

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(24.f));
	RoleList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(RoleList);
	HCUI::AddRow(Box, Panel, 24.f);

	LobbyButton = HCUI::Button(Tree, NSLOCTEXT("HC", "BackToLobby", "ZURÜCK IN DIE LOBBY"), HCUI::AccentRed);
	LobbyButton->OnClicked.AddDynamic(this, &UHCResultWidget::HandleBackToLobby);
	HCUI::AddRow(Box, LobbyButton, 8.f, HAlign_Center);
	CountdownText = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	CountdownText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, CountdownText, 0.f, HAlign_Fill);

	HCUI::Place(Root, Box, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 620.f);
}

void UHCResultWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	if (ShownRoles != GS->GetRevealedRoles().Num())
	{
		Rebuild();
	}
	const AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	const bool bHost = PC && PC->IsHost();
	LobbyButton->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	CountdownText->SetText(FText::Format(NSLOCTEXT("HC", "LobbyIn", "Zurück in die Lobby in {0} s"), FMath::CeilToInt(GS->GetPhaseRemainingSeconds())));
}

void UHCResultWidget::Rebuild()
{
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	ShownRoles = GS->GetRevealedRoles().Num();

	const bool bCrew = GS->GetWinner() == EHCWinner::Crew;
	WinnerText->SetText(bCrew ? NSLOCTEXT("HC", "CrewWins", "DIE CREW GEWINNT") : NSLOCTEXT("HC", "MonsterWins", "DER WANDLER GEWINNT"));
	WinnerText->SetColorAndOpacity(FSlateColor(bCrew ? FLinearColor(0.4f, 0.85f, 1.f) : FLinearColor(0.9f, 0.12f, 0.1f)));
	ReasonText->SetText(GS->GetWinReason());

	RoleList->ClearChildren();
	for (const FHCRevealedRole& Entry : GS->GetRevealedRoles())
	{
		const bool bMonster = Entry.Role == EHCRole::Monster;
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Row, HCUI::Swatch(WidgetTree, HCIdentity::Color(Entry.Identity), 18.f), 12.f);
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::FromString(Entry.PlayerName), 18, bMonster ? FLinearColor(1.f, 0.3f, 0.25f) : HCUI::TextColor, true), 12.f, true);
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::Format(NSLOCTEXT("HC", "IdentFmt", "{0} · {1}"),
			HCIdentity::ColorName(Entry.Identity), HCIdentity::SymbolName(Entry.Identity)), 13, HCUI::MutedColor), 16.f);
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, bMonster ? NSLOCTEXT("HC", "RoleMonsterUpper", "DER WANDLER") : NSLOCTEXT("HC", "RoleCrewUpper", "Crew"),
			14, bMonster ? FLinearColor(1.f, 0.3f, 0.25f) : HCUI::MutedColor, bMonster), 16.f);
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, Entry.bAlive ? NSLOCTEXT("HC", "Alive", "lebt") : NSLOCTEXT("HC", "Dead", "tot"), 13, HCUI::MutedColor), 0.f);
		HCUI::AddRow(RoleList, Row, 8.f);
	}
}

void UHCResultWidget::HandleBackToLobby()
{
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer()))
	{
		PC->ServerRequestReturnToLobby();
	}
}
