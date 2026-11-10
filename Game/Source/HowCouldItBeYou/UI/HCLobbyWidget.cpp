#include "UI/HCLobbyWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Online/HCJoinCode.h"
#include "Online/HCSessionSubsystem.h"
#include "UI/HCHUD.h"
#include "UI/HCUIStyle.h"

void UHCLobbyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.f));

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(28.f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Box);

	HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "LobbyTitle", "LOBBY"), 28, HCUI::TextColor, true), 12.f);
	CodeText = HCUI::Text(Tree, FText::GetEmpty(), 22, FLinearColor(0.9f, 0.9f, 0.6f), true);
	HCUI::AddRow(Box, CodeText, 2.f);
	AddressText = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	HCUI::AddRow(Box, AddressText, 18.f);

	CountText = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::MutedColor);
	HCUI::AddRow(Box, CountText, 6.f);
	PlayerList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, PlayerList, 18.f);

	UTextBlock* StartLabel = nullptr;
	StartButton = HCUI::Button(Tree, NSLOCTEXT("HC", "StartRound", "RUNDE STARTEN"), HCUI::AccentRed, &StartLabel);
	StartButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleStart);
	HCUI::AddRow(Box, StartButton, 6.f);
	HintText = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	HCUI::AddRow(Box, HintText, 14.f);

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UButton* Walk = HCUI::Button(Tree, NSLOCTEXT("HC", "Walk", "HERUMLAUFEN (Tab)"), HCUI::ButtonColor, nullptr, 13);
	Walk->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleWalk);
	HCUI::AddColumn(Row, Walk, 10.f, true);
	UButton* Leave = HCUI::Button(Tree, NSLOCTEXT("HC", "Leave", "VERLASSEN"), HCUI::ButtonColor, nullptr, 13);
	Leave->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleLeave);
	HCUI::AddColumn(Row, Leave, 0.f, true);
	HCUI::AddRow(Box, Row, 10.f);

	MessageText = HCUI::Text(Tree, FText::GetEmpty(), 13, FLinearColor(1.f, 0.4f, 0.3f));
	HCUI::AddRow(Box, MessageText, 0.f);

	HCUI::Place(Root, Panel, FVector2D(0.04f, 0.5f), FVector2D(0.f, 0.5f), 420.f);
	Refresh();
}

void UHCLobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshTimer -= InDeltaTime;
	if (RefreshTimer <= 0.f)
	{
		RefreshTimer = 0.25f;
		Refresh();
	}
}

FReply UHCLobbyWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Tab)
	{
		HandleWalk();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F1)
	{
		if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
		{
			HUD->TogglePauseMenu();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHCLobbyWidget::Refresh()
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!GS)
	{
		return;
	}

	const FString Code = GS->GetJoinCode();
	CodeText->SetText(Code.IsEmpty() ? NSLOCTEXT("HC", "NoCode", "Kein Join-Code (Testmodus)")
		: FText::Format(NSLOCTEXT("HC", "CodeFmt", "Code: {0}"), FText::FromString(FHCJoinCode::Format(Code))));
	AddressText->SetText(FText::Format(NSLOCTEXT("HC", "AddrFmt", "Im selben Netzwerk auch per IP: {0}   ·   Modus: {1}"),
		FText::FromString(GS->GetHostAddress()), FText::FromString(GS->GetModeName())));

	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const int32 Count = GS->PlayerArray.Num();
	CountText->SetText(FText::Format(NSLOCTEXT("HC", "CountFmt", "Spieler {0}/{1}"), Count, Settings->MaxPlayers));

	// Rebuild the list only when it changed
	FString Key;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS);
		Key += FString::Printf(TEXT("%s|%d;"), *PS->GetPlayerName(), HCPS ? HCPS->GetIdentity() : -1);
	}
	if (Key != LastPlayerKey)
	{
		LastPlayerKey = Key;
		PlayerList->ClearChildren();
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS);
			const int32 Identity = HCPS ? FMath::Max(0, HCPS->GetIdentity()) : 0;
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			HCUI::AddColumn(Row, HCUI::Swatch(WidgetTree, HCIdentity::Color(Identity), 16.f), 10.f);
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::FromString(PS->GetPlayerName()), 16, HCUI::TextColor, true), 10.f);
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::Format(NSLOCTEXT("HC", "IdentFmt", "{0} · {1}"),
				HCIdentity::ColorName(Identity), HCIdentity::SymbolName(Identity)), 13, HCUI::MutedColor), 0.f);
			HCUI::AddRow(PlayerList, Row, 6.f);
		}
	}

	const bool bHost = PC && PC->IsHost();
	StartButton->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	StartButton->SetIsEnabled(bHost && Count >= Settings->MinPlayersToStart);
	if (!bHost)
	{
		HintText->SetText(NSLOCTEXT("HC", "WaitHost", "Warte, bis der Host die Runde startet."));
	}
	else if (Count < Settings->MinPlayersToStart)
	{
		HintText->SetText(FText::Format(NSLOCTEXT("HC", "NeedMore", "Mindestens {0} Spieler nötig. Gib deinen Freunden den Code."), Settings->MinPlayersToStart));
	}
	else if (Count < Settings->MinPlayers)
	{
		HintText->SetText(FText::Format(NSLOCTEXT("HC", "TestRound", "Empfohlen sind {0}–{1} Spieler. Mit weniger ist es eine Testrunde."), Settings->MinPlayers, Settings->MaxPlayers));
	}
	else
	{
		HintText->SetText(NSLOCTEXT("HC", "Ready", "Bereit. Ein Wandler wird zufällig ausgewählt."));
	}
	MessageText->SetText(PC ? PC->GetClientMessage() : FText::GetEmpty());
}

void UHCLobbyWidget::HandleStart()
{
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer()))
	{
		PC->ServerRequestStartMatch();
	}
}

void UHCLobbyWidget::HandleWalk()
{
	if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->ToggleLobbyMenu();
	}
}

void UHCLobbyWidget::HandleLeave()
{
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->LeaveToMainMenu();
	}
}
